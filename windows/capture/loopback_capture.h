#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace overlay_trans {

struct CaptureConfig {
    uint32_t sample_rate = 16000;  // whisper.cpp 입력 샘플레이트
    uint32_t channels = 2;         // 화자 분리를 위해 스테레오 유지
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
