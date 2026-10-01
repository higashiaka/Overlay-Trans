#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace overlay_trans {

struct VadConfig {
    float threshold = 0.5f;          // 이 확률 이상이면 음성으로 본다.
    uint32_t min_speech_ms = 250;    // 이보다 짧은 발화는 버린다.
    uint32_t min_silence_ms = 500;   // 이만큼 무음이 이어지면 발화가 끝난 것으로 본다.
    uint32_t speech_pad_ms = 200;    // 발화 앞뒤에 남겨 두는 여유 구간.
    uint32_t max_speech_ms = 15000;  // 이보다 길어지면 강제로 끊는다.
};

// 16kHz 모노 오디오 스트림에서 Silero VAD로 발화 구간을 잘라낸다.
class VadSegmenter {
public:
    using SegmentCallback = std::function<void(std::span<const float> samples)>;

    VadSegmenter();
    ~VadSegmenter();

    VadSegmenter(const VadSegmenter&) = delete;
    VadSegmenter& operator=(const VadSegmenter&) = delete;

    bool init(const std::filesystem::path& model_path, const VadConfig& config, SegmentCallback on_segment);

    // 샘플을 이어서 넣는다. 발화가 끝나면 process를 호출한 스레드에서 콜백이 호출된다.
    void process(const float* samples, size_t count);

    // 진행 중인 발화를 즉시 마무리한다. 입력이 끊겼을 때 호출한다.
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
