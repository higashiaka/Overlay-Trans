#include "audio/ring_buffer.h"
#include "capture/loopback_capture.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    using namespace overlay_trans;

    const CaptureConfig config;
    const size_t samples_per_second = static_cast<size_t>(config.sample_rate) * config.channels;

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
        while (!stop.stop_requested()) {
            const size_t read_count = ring.read(chunk.data(), chunk.size());
            consumed_samples += read_count;
            if (read_count == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });

    std::printf("Capturing system audio (%u Hz, %u ch). Press Enter to stop.\n", config.sample_rate,
                config.channels);
    std::getchar();

    capture.stop();
    consumer.request_stop();
    consumer.join();

    const uint64_t captured_samples = consumed_samples + ring.available();
    std::printf("Captured %.2f seconds of audio, dropped %llu samples.\n",
                static_cast<double>(captured_samples) / static_cast<double>(samples_per_second),
                static_cast<unsigned long long>(ring.dropped_samples()));
    return 0;
}
