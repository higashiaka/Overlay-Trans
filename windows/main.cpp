#include "audio/ring_buffer.h"
#include "audio/vad_segmenter.h"
#include "audio/wav_writer.h"
#include "capture/audio_file_reader.h"
#include "capture/loopback_capture.h"
#include "inference/speech_recognizer.h"
#include "inference/translator.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace overlay_trans;
using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path vad_model = "core/models/ggml-silero-v6.2.0.bin";
    std::filesystem::path stt_model = "core/models/ggml-base.bin";
    std::filesystem::path llm_model = "core/models/gemma-3-4b-it-Q4_K_M.gguf";
    std::filesystem::path input_file;  // 비어 있으면 시스템 오디오를 캡처한다.
    std::filesystem::path dump_wav;    // 비어 있으면 덤프하지 않는다.
    std::string language = "auto";
    std::string stt_hint;  // STT에 미리 알려 줄 이름과 용어
    bool use_gpu = true;  // Vulkan 프리셋으로 빌드한 경우에만 효과가 있다.
    int gpu_device = 0;
};

// 콘솔 출력 코드 페이지(UTF-8)에 맞춰 경로를 문자열로 바꾼다.
std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

Options parse_options(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring_view name = argv[i];
        const std::wstring_view value = argv[i + 1];
        if (name == L"--vad-model") {
            options.vad_model = value;
        } else if (name == L"--stt-model") {
            options.stt_model = value;
        } else if (name == L"--llm-model") {
            options.llm_model = value;
        } else if (name == L"--input") {
            options.input_file = value;
        } else if (name == L"--dump-wav") {
            options.dump_wav = value;
        } else if (name == L"--language") {
            options.language = to_utf8(value);
        } else if (name == L"--stt-hint") {
            options.stt_hint = to_utf8(value);
        } else if (name == L"--device") {
            options.use_gpu = value != L"cpu";
        } else if (name == L"--gpu-device") {
            options.gpu_device = static_cast<int>(std::wcstol(argv[i + 1], nullptr, 10));
        }
    }
    return options;
}

double to_seconds(Clock::duration duration) {
    return std::chrono::duration<double>(duration).count();
}

// 파일을 처음부터 끝까지 가능한 한 빠르게 처리한다.
int run_file(const Options& options, const CaptureConfig& config, VadSegmenter& vad) {
    AudioFileReader reader;
    if (!reader.open(options.input_file, config.sample_rate, config.channels)) {
        std::fprintf(stderr, "Failed to open audio file: %s (%s)\n", to_utf8(options.input_file).c_str(),
                     reader.last_error().c_str());
        return 1;
    }

    const size_t chunk_frames = config.sample_rate / 10;
    std::vector<float> chunk(chunk_frames * config.channels);
    uint64_t total_frames = 0;
    const auto started_at = Clock::now();

    while (true) {
        const size_t frame_count = reader.read(chunk.data(), chunk_frames);
        if (frame_count == 0) {
            break;
        }
        total_frames += frame_count;
        vad.process(chunk.data(), frame_count * config.channels);
    }
    vad.flush();

    std::printf("Processed %.1f s of audio in %.1f s.\n",
                static_cast<double>(total_frames) / static_cast<double>(config.sample_rate),
                to_seconds(Clock::now() - started_at));
    return 0;
}

// 시스템 오디오를 캡처해 Enter를 누를 때까지 처리한다.
int run_live(const Options& options, const CaptureConfig& config, const VadConfig& vad_config, VadSegmenter& vad) {
    const size_t samples_per_second = static_cast<size_t>(config.sample_rate) * config.channels;

    WavWriter wav_dump;
    if (!options.dump_wav.empty() &&
        !wav_dump.open(options.dump_wav, config.sample_rate, static_cast<uint16_t>(config.channels))) {
        std::fprintf(stderr, "Failed to open WAV file: %s\n", to_utf8(options.dump_wav).c_str());
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

}  // namespace

// 한글/일본어가 들어간 파일 경로를 받을 수 있도록 유니코드 인자를 사용한다.
int wmain(int argc, wchar_t** argv) {
    // 인식 결과(UTF-8)가 콘솔에서 깨지지 않게 한다.
    SetConsoleOutputCP(CP_UTF8);

    const Options options = parse_options(argc, argv);
    const CaptureConfig config;
    const VadConfig vad_config;

    SpeechRecognizer recognizer;
    const SttConfig stt_config{
        .language = options.language,
        .vocabulary_hint = options.stt_hint,
        .use_gpu = options.use_gpu,
        .gpu_device = options.gpu_device,
    };
    if (!recognizer.init(options.stt_model, stt_config)) {
        std::fprintf(stderr, "Failed to load STT model: %s\n", to_utf8(options.stt_model).c_str());
        return 1;
    }

    Translator translator;
    const TranslatorConfig translator_config{
        .use_gpu = options.use_gpu,
        .gpu_device = options.gpu_device,
    };
    if (!translator.init(options.llm_model, translator_config)) {
        std::fprintf(stderr, "Failed to load translation model: %s\n", to_utf8(options.llm_model).c_str());
        return 1;
    }

    VadSegmenter vad;
    const bool vad_ready = vad.init(options.vad_model, vad_config, [&](std::span<const float> samples) {
        const auto to_ms = [](Clock::duration duration) {
            return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
        };

        const auto stt_started_at = Clock::now();
        const std::string text = recognizer.transcribe(samples);
        const auto translation_started_at = Clock::now();
        const std::string translation = text.empty() ? std::string() : translator.translate(text);
        const auto finished_at = Clock::now();

        std::printf("[%.2f s audio | STT %lld ms | translation %lld ms]\n  %s\n  %s\n",
                    static_cast<double>(samples.size()) / static_cast<double>(config.sample_rate * config.channels),
                    to_ms(translation_started_at - stt_started_at), to_ms(finished_at - translation_started_at),
                    text.c_str(), translation.c_str());
        std::fflush(stdout);
    });
    if (!vad_ready) {
        std::fprintf(stderr, "Failed to load VAD model: %s\n", to_utf8(options.vad_model).c_str());
        return 1;
    }

    if (!options.input_file.empty()) {
        return run_file(options, config, vad);
    }
    return run_live(options, config, vad_config, vad);
}
