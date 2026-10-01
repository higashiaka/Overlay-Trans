#include "audio/ring_buffer.h"
#include "audio/wav_writer.h"
#include "capture/loopback_capture.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    using namespace overlay_trans;

    const CaptureConfig config;
    const size_t samples_per_second = static_cast<size_t>(config.sample_rate) * config.channels;

    // --dump-wav <path>: 캡처한 오디오를 디버깅용 WAV 파일로 저장한다.
    WavWriter wav_dump;
    if (argc == 3 && std::string_view(argv[1]) == "--dump-wav") {
        if (!wav_dump.open(argv[2], config.sample_rate, static_cast<uint16_t>(config.channels))) {
            std::fprintf(stderr, "Failed to open WAV file: %s\n", argv[2]);
            return 1;
        }
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

    uint64_t consumed_samples = 0;
    std::jthread consumer([&](std::stop_token stop) {
        std::vector<float> chunk(samples_per_second / 10);
        while (true) {
            const size_t read_count = ring.read(chunk.data(), chunk.size());
            if (read_count == 0) {
                // 종료 요청을 받아도 버퍼에 남은 샘플은 모두 처리한 뒤 끝낸다.
                if (stop.stop_requested()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            consumed_samples += read_count;
            if (wav_dump.is_open()) {
                wav_dump.write(chunk.data(), read_count);
            }
        }
    });

    std::printf("Capturing system audio (%u Hz, %u ch). Press Enter to stop.\n", config.sample_rate,
                config.channels);
    std::getchar();

    capture.stop();
    consumer.request_stop();
    consumer.join();
    wav_dump.close();

    std::printf("Captured %.2f seconds of audio, dropped %llu samples.\n",
                static_cast<double>(consumed_samples) / static_cast<double>(samples_per_second),
                static_cast<unsigned long long>(ring.dropped_samples()));
    return 0;
}
