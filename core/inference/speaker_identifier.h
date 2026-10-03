#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace overlay_trans {

struct SpeakerConfig {
    // 목소리 특징(임베딩)의 코사인 유사도가 이 값 이상이면 같은 화자로 본다.
    float same_speaker_similarity = 0.4f;
    // 이보다 짧은 구간은 목소리를 구분하기 어려워 화자를 정하지 않는다.
    float min_seconds = 0.8f;
    // 기억해 둘 화자 수. 넘으면 진행자를 빼고 가장 오래전에 들은 화자를 잊고 그 자리에 새 화자를 둔다.
    size_t max_speakers = 8;
};

// 발화 구간이 누구의 목소리인지 구분한다. 처음 듣는 목소리면 새 번호를 매긴다.
// sherpa-onnx의 화자 임베딩 모델(CAM++ 등)을 쓴다.
class SpeakerIdentifier {
public:
    // 화자를 정하지 못했을 때의 값.
    static constexpr int UNKNOWN = -1;

    SpeakerIdentifier();
    ~SpeakerIdentifier();

    SpeakerIdentifier(const SpeakerIdentifier&) = delete;
    SpeakerIdentifier& operator=(const SpeakerIdentifier&) = delete;

    bool init(const std::filesystem::path& model_path, const SpeakerConfig& config);

    // 16kHz 모노 샘플의 화자 번호(0부터)를 반환한다. 구간이 너무 짧으면 UNKNOWN.
    // similarity에는 고른 화자와의 유사도를 돌려준다. (새 화자면 가장 비슷했던 화자와의 유사도)
    int identify(std::span<const float> samples, float& similarity);

    // 지금까지 가장 오래 말한 화자. 방송에서는 보통 진행자다. 아직 없으면 UNKNOWN.
    int main_speaker() const;

    // 기억한 화자를 모두 잊는다. 다른 방송으로 옮겼을 때 호출한다.
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
