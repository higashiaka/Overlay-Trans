#include "audio/ring_buffer.h"
#include "audio/vad_segmenter.h"
#include "audio/wav_writer.h"
#include "capture/loopback_capture.h"
#include "inference/speech_recognizer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string vad_model = "core/models/ggml-silero-v6.2.0.bin";
    std::string stt_model = "core/models/ggml-base.bin";
    std::string language = "auto";
    std::string dump_wav;  // 비어 있으면 덤프하지 않는다.
    bool use_gpu = true;   // Vulkan 프리셋으로 빌드한 경우에만 효과가 있다.
    int gpu_device = 0;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view name = argv[i];
        if (name == "--vad-model") {
            options.vad_model = argv[i + 1];
        } else if (name == "--stt-model") {
            options.stt_model = argv[i + 1];
        } else if (name == "--language") {
            options.language = argv[i + 1];
        } else if (name == "--dump-wav") {
            options.dump_wav = argv[i + 1];
        } else if (name == "--device") {
            options.use_gpu = std::string_view(argv[i + 1]) != "cpu";
        } else if (name == "--gpu-device") {
            options.gpu_device = std::atoi(argv[i + 1]);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace overlay_trans;

    // 인식 결과(UTF-8)가 콘솔에서 깨지지 않게 한다.
    SetConsoleOutputCP(CP_UTF8);

    const Options options = parse_options(argc, argv);
    const CaptureConfig config;
    const VadConfig vad_config;
    const size_t samples_per_second = static_cast<size_t>(config.sample_rate) * config.channels;

    WavWriter wav_dump;
    if (!options.dump_wav.empty() &&
        !wav_dump.open(options.dump_wav, config.sample_rate, static_cast<uint16_t>(config.channels))) {
        std::fprintf(stderr, "Failed to open WAV file: %s\n", options.dump_wav.c_str());
        return 1;
    }

    SpeechRecognizer recognizer;
    const SttConfig stt_config{
        .language = options.language,
        .use_gpu = options.use_gpu,
        .gpu_device = options.gpu_device,
    };
    if (!recognizer.init(options.stt_model, stt_config)) {
        std::fprintf(stderr, "Failed to load STT model: %s\n", options.stt_model.c_str());
        return 1;
    }

    VadSegmenter vad;
    const bool vad_ready = vad.init(options.vad_model, vad_config, [&](std::span<const float> samples) {
        const auto started_at = std::chrono::steady_clock::now();
        const std::string text = recognizer.transcribe(samples);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);

        std::printf("[%.2f s audio, %lld ms] %s\n",
                    static_cast<double>(samples.size()) / static_cast<double>(samples_per_second),
                    static_cast<long long>(elapsed.count()), text.c_str());
        std::fflush(stdout);
    });
    if (!vad_ready) {
        std::fprintf(stderr, "Failed to load VAD model: %s\n", options.vad_model.c_str());
        return 1;
    }

    // 소비 스레드가 잠시 멈춰도 버틸 수 있도록 10초 분량을 확보한다.
    RingBuffer ring(samples_per_second * 10);

    LoopbackCapture capture;
    const bool started = capture.start(config, [&](const float* frames, uint32_t frame_count) {
        ring.write(frames, static_cast<size_t>(frame_count) * config.channels);
    });
    if (!started) {
        std::fprintf(stderr, "Failed to start loopback capture: %s\n", capture.last_error().c_str());
        return 1;
    }

    std::jthread consumer([&](std::stop_token stop) {
        constexpr auto IDLE_SLEEP = std::chrono::milliseconds(10);
        std::vector<float> chunk(samples_per_second / 10);
        auto idle_time = std::chrono::milliseconds(0);

        while (true) {
            const size_t read_count = ring.read(chunk.data(), chunk.size());
            if (read_count == 0) {
                // 종료 요청을 받아도 버퍼에 남은 샘플은 모두 처리한 뒤 끝낸다.
                if (stop.stop_requested()) {
                    break;
                }

                // 재생 중인 소리가 없으면 루프백 입력이 끊기므로, 진행 중인 발화를 여기서 마무리한다.
                std::this_thread::sleep_for(IDLE_SLEEP);
                idle_time += IDLE_SLEEP;
                if (idle_time >= std::chrono::milliseconds(vad_config.min_silence_ms)) {
                    vad.flush();
                    idle_time = std::chrono::milliseconds(0);
                }
                continue;
            }

            idle_time = std::chrono::milliseconds(0);
            if (wav_dump.is_open()) {
                wav_dump.write(chunk.data(), read_count);
            }
            vad.process(chunk.data(), read_count);
        }

        vad.flush();
    });

    std::printf("Capturing system audio (%u Hz, %u ch). Press Enter to stop.\n", config.sample_rate,
                config.channels);
    std::getchar();

    capture.stop();
    consumer.request_stop();
    consumer.join();
    wav_dump.close();

    std::printf("Dropped %llu samples.\n", static_cast<unsigned long long>(ring.dropped_samples()));
    return 0;
}
