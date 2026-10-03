#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace overlay_trans {

struct VadConfig {
    // 이 확률 이상이면 음성으로 본다. 방송에서는 배경 음악이나 게임 소리 위로 작게 말하는 일이 많아 낮게 둔다.
    // (0.5에서는 방송 자막의 35%가 말소리로 잡히지 않았고, 0.25에서 15%로 줄었다. 엉뚱한 소리를 잡는 일은 늘지 않았다.)
    float threshold = 0.25f;
    uint32_t min_speech_ms = 250;    // 이보다 짧은 발화는 버린다.
    // 이만큼 무음이 이어지면 발화가 끝난 것으로 본다. 짧을수록 자막이 빨리 뜨고 문장은 잘게 끊기지만,
    // 끊긴 문장은 이어서 합쳐 다시 번역하므로(windows/main.cpp) 짧게 둔다.
    uint32_t min_silence_ms = 320;
    uint32_t speech_pad_ms = 200;    // 발화 앞뒤에 남겨 두는 여유 구간.

    // 길게 이어지는 말은 끝날 때까지 기다리면 자막이 너무 늦어지므로 중간에 끊는다.
    uint32_t long_speech_ms = 2500;          // 발화가 이보다 길어지면 짧은 쉼에서도 끊는다.
    uint32_t long_speech_silence_ms = 192;   // 긴 발화를 끊는 데 필요한 무음 길이.
    uint32_t max_speech_ms = 6000;           // 쉼이 없어도 이 길이에서는 강제로 끊는다.
    uint32_t forced_cut_lookback_ms = 1500;  // 강제로 끊을 때, 이 범위 안에서 말소리가 가장 약한 지점을 고른다.

    // 무음이 이만큼 이어지면 발화가 끝났다고 확정하기 전에 on_pause로 미리 알린다. 0이면 알리지 않는다.
    uint32_t early_silence_ms = 160;
};

// 잘라낸 발화 구간 하나.
struct SpeechSegment {
    std::span<const float> samples;
    // 구간의 마지막 샘플이 들어온 뒤 끝났다고 판단하기까지 흐른 시간.
    uint32_t waited_ms = 0;
    // 직전 구간의 말소리가 끝난 뒤 이 구간의 말소리가 시작하기까지 이어진 무음의 길이.
    // 짧을수록 직전 구간에서 이어진 말일 가능성이 높다. 직전 구간이 없으면 NO_PREVIOUS_SEGMENT다.
    uint32_t silence_before_ms = NO_PREVIOUS_SEGMENT;

    static constexpr uint32_t NO_PREVIOUS_SEGMENT = UINT32_MAX;
};

// 모든 콜백은 process나 flush를 호출한 스레드에서 호출된다.
struct VadCallbacks {
    // 발화가 끝났다.
    std::function<void(const SpeechSegment& segment)> on_segment;

    // 말이 잠깐 멈췄다. 여기서 발화가 끝날 수 있으므로 받은 쪽은 이 구간의 처리를 미리 시작해도 된다.
    // 그대로 끝나면 똑같은 구간으로 on_segment가, 말이 다시 이어지면 on_resume이 호출된다. (비워 두어도 된다.)
    std::function<void(std::span<const float> samples)> on_pause;
    std::function<void()> on_resume;
};

// 16kHz 모노 오디오 스트림에서 Silero VAD로 발화 구간을 잘라낸다.
class VadSegmenter {
public:
    VadSegmenter();
    ~VadSegmenter();

    VadSegmenter(const VadSegmenter&) = delete;
    VadSegmenter& operator=(const VadSegmenter&) = delete;

    bool init(const std::filesystem::path& model_path, const VadConfig& config, VadCallbacks callbacks);

    // 샘플을 이어서 넣는다.
    void process(const float* samples, size_t count);

    // 진행 중인 발화를 즉시 마무리한다. 입력이 끊겼을 때 호출한다.
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
