#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace overlay_trans {

struct CaptureConfig {
    // whisper.cpp 입력 포맷(16kHz 모노)에 맞춘다.
    uint32_t sample_rate = 16000;
    uint32_t channels = 1;
};

// 기본 출력 장치에서 재생 중인 시스템 오디오를 WASAPI 루프백으로 캡처한다.
class LoopbackCapture {
public:
    // frames는 인터리브된 float32 샘플(frame_count * channels 개)이다.
    // 오디오 스레드에서 호출되므로 블로킹 작업을 하면 안 된다.
    using FrameCallback = std::function<void(const float* frames, uint32_t frame_count)>;

    LoopbackCapture();
    ~LoopbackCapture();

    LoopbackCapture(const LoopbackCapture&) = delete;
    LoopbackCapture& operator=(const LoopbackCapture&) = delete;

    bool start(const CaptureConfig& config, FrameCallback callback);
    void stop();

    const std::string& last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
