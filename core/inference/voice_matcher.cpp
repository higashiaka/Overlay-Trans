#include "voice_matcher.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <cmath>
#include <string>

namespace overlay_trans {

namespace {

constexpr int SAMPLE_RATE = 16000;
// 이보다 짧은 소리로는 목소리 특징을 믿을 수 없다.
constexpr size_t MIN_SAMPLES = SAMPLE_RATE / 2;

}  // namespace

struct VoiceMatcher::Impl {
    const SherpaOnnxSpeakerEmbeddingExtractor* extractor = nullptr;
};

VoiceMatcher::VoiceMatcher() : impl_(std::make_unique<Impl>()) {}

VoiceMatcher::~VoiceMatcher() {
    if (impl_->extractor != nullptr) {
        SherpaOnnxDestroySpeakerEmbeddingExtractor(impl_->extractor);
    }
}

bool VoiceMatcher::init(const std::filesystem::path& model_path) {
    const std::string model = model_path.string();
    SherpaOnnxSpeakerEmbeddingExtractorConfig config{};
    config.model = model.c_str();
    // 모델이 작아서 CPU 하나로도 1.5초 분량에 20ms 안팎이면 끝난다. GPU는 인식과 번역에 남겨 둔다.
    config.num_threads = 1;
    config.provider = "cpu";

    impl_->extractor = SherpaOnnxCreateSpeakerEmbeddingExtractor(&config);
    return impl_->extractor != nullptr;
}

std::vector<float> VoiceMatcher::embed(std::span<const float> samples) const {
    if (samples.size() < MIN_SAMPLES) {
        return {};
    }
    const SherpaOnnxOnlineStream* stream = SherpaOnnxSpeakerEmbeddingExtractorCreateStream(impl_->extractor);
    if (stream == nullptr) {
        return {};
    }
    SherpaOnnxOnlineStreamAcceptWaveform(stream, SAMPLE_RATE, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxOnlineStreamInputFinished(stream);

    std::vector<float> embedding;
    if (SherpaOnnxSpeakerEmbeddingExtractorIsReady(impl_->extractor, stream)) {
        if (const float* values = SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(impl_->extractor, stream)) {
            const auto dimension = static_cast<size_t>(SherpaOnnxSpeakerEmbeddingExtractorDim(impl_->extractor));
            embedding.assign(values, values + dimension);
            SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(values);
        }
    }
    SherpaOnnxDestroyOnlineStream(stream);

    float norm = 0.0f;
    for (const float value : embedding) {
        norm += value * value;
    }
    norm = std::sqrt(norm);
    if (norm > 0.0f) {
        for (float& value : embedding) {
            value /= norm;
        }
    }
    return embedding;
}

float VoiceMatcher::similarity(const std::vector<float>& a, const std::vector<float>& b) {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

}  // namespace overlay_trans
