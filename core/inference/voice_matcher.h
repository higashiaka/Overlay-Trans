#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace overlay_trans {

// 두 발화가 같은 사람의 목소리인지 비교한다. sherpa-onnx의 화자 임베딩 모델(3D-Speaker CAM++)로
// 목소리 특징을 뽑아 코사인 유사도를 잰다.
//
// 누가 누구인지 번호를 매기는 방식(화자 분리)은 여러 사람이 빠르게 주고받는 방송에서 거의 맞지 않았다.
// 한 구간 안에 여러 사람이 섞여 들어가기 때문이다. 바로 앞뒤 구간의 맞닿은 부분만 비교하는 편이 훨씬 안정적이다.
class VoiceMatcher {
public:
    VoiceMatcher();
    ~VoiceMatcher();

    VoiceMatcher(const VoiceMatcher&) = delete;
    VoiceMatcher& operator=(const VoiceMatcher&) = delete;

    bool init(const std::filesystem::path& model_path);

    // 16kHz 모노 샘플의 목소리 특징(길이 1인 벡터). 너무 짧아 뽑을 수 없으면 빈 벡터를 반환한다.
    std::vector<float> embed(std::span<const float> samples) const;

    // 두 목소리 특징의 코사인 유사도. 같은 사람이면 대개 0.5 이상이다.
    static float similarity(const std::vector<float>& a, const std::vector<float>& b);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
