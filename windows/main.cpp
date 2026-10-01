#include "capture/loopback_capture.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

int main() {
    using namespace overlay_trans;

    const CaptureConfig config;
    std::atomic<uint64_t> captured_frames{0};

    LoopbackCapture capture;
    const bool started = capture.start(config, [&](const float*, uint32_t frame_count) {
        captured_frames.fetch_add(frame_count, std::memory_order_relaxed);
    });
    if (!started) {
        std::fprintf(stderr, "Failed to start loopback capture: %s\n", capture.last_error().c_str());
        return 1;
    }

    std::printf("Capturing system audio (%u Hz, %u ch). Press Enter to stop.\n", config.sample_rate,
                config.channels);
    std::getchar();

    capture.stop();
    std::printf("Captured %.2f seconds of audio.\n",
                static_cast<double>(captured_frames.load()) / config.sample_rate);
    return 0;
}
