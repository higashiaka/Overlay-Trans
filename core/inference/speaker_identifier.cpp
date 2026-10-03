#include "speaker_identifier.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace overlay_trans {

namespace {

constexpr int SAMPLE_RATE = 16000;

void normalize(std::vector<float>& vector) {
    float norm = 0.0f;
    for (const float value : vector) {
        norm += value * value;
    }
    norm = std::sqrt(norm);
    if (norm > 0.0f) {
        for (float& value : vector) {
            value /= norm;
        }
    }
}

float dot(const std::vector<float>& a, const std::vector<float>& b) {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

}  // namespace

struct SpeakerIdentifier::Impl {
    struct Speaker {
        std::vector<float> centroid;  // 지금까지 들은 구간들의 평균 특징 (길이 1로 맞춤)
        size_t segment_count = 0;
        double seconds = 0.0;  // 이 화자로 판단한 발화의 총 길이
        size_t last_heard = 0;  // 마지막으로 이 화자로 판단한 순번
    };

    const SherpaOnnxSpeakerEmbeddingExtractor* extractor = nullptr;
    SpeakerConfig config;
    std::vector<Speaker> speakers;
    size_t heard_count = 0;  // 지금까지 화자를 판단한 횟수 (last_heard의 기준)

    bool extract(std::span<const float> samples, std::vector<float>& embedding) const;
};

bool SpeakerIdentifier::Impl::extract(std::span<const float> samples, std::vector<float>& embedding) const {
    const SherpaOnnxOnlineStream* stream = SherpaOnnxSpeakerEmbeddingExtractorCreateStream(extractor);
    if (stream == nullptr) {
        return false;
    }
    SherpaOnnxOnlineStreamAcceptWaveform(stream, SAMPLE_RATE, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxOnlineStreamInputFinished(stream);

    bool extracted = false;
    if (SherpaOnnxSpeakerEmbeddingExtractorIsReady(extractor, stream)) {
        const float* values = SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(extractor, stream);
        if (values != nullptr) {
            const auto dimension = static_cast<size_t>(SherpaOnnxSpeakerEmbeddingExtractorDim(extractor));
            embedding.assign(values, values + dimension);
            SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(values);
            extracted = true;
        }
    }
    SherpaOnnxDestroyOnlineStream(stream);
    return extracted;
}

SpeakerIdentifier::SpeakerIdentifier() : impl_(std::make_unique<Impl>()) {}

SpeakerIdentifier::~SpeakerIdentifier() {
    if (impl_->extractor != nullptr) {
        SherpaOnnxDestroySpeakerEmbeddingExtractor(impl_->extractor);
    }
}

bool SpeakerIdentifier::init(const std::filesystem::path& model_path, const SpeakerConfig& config) {
    const std::string model = model_path.string();
    SherpaOnnxSpeakerEmbeddingExtractorConfig extractor_config{};
    extractor_config.model = model.c_str();
    // 모델이 작아서 CPU 하나로도 구간당 수십 ms면 끝난다. GPU는 인식과 번역에 남겨 둔다.
    extractor_config.num_threads = 1;
    extractor_config.provider = "cpu";

    impl_->extractor = SherpaOnnxCreateSpeakerEmbeddingExtractor(&extractor_config);
    impl_->config = config;
    return impl_->extractor != nullptr;
}

int SpeakerIdentifier::identify(std::span<const float> samples, float& similarity) {
    similarity = 0.0f;
    const double seconds = static_cast<double>(samples.size()) / SAMPLE_RATE;
    if (seconds < impl_->config.min_seconds) {
        return UNKNOWN;
    }

    std::vector<float> embedding;
    if (!impl_->extract(samples, embedding)) {
        return UNKNOWN;
    }
    normalize(embedding);

    int best = UNKNOWN;
    float best_similarity = -1.0f;
    for (size_t i = 0; i < impl_->speakers.size(); ++i) {
        const float value = dot(embedding, impl_->speakers[i].centroid);
        if (value > best_similarity) {
            best_similarity = value;
            best = static_cast<int>(i);
        }
    }
    similarity = std::max(best_similarity, 0.0f);

    ++impl_->heard_count;
    if (best == UNKNOWN || best_similarity < impl_->config.same_speaker_similarity) {
        Impl::Speaker speaker{embedding, 1, seconds, impl_->heard_count};
        if (impl_->speakers.size() < impl_->config.max_speakers) {
            impl_->speakers.push_back(std::move(speaker));
            return static_cast<int>(impl_->speakers.size() - 1);
        }
        // 자리가 없으면 진행자를 빼고 가장 오래전에 들은 화자를 새 화자로 바꾼다.
        // 비슷하지 않은 목소리를 억지로 기존 화자에 붙이면 그 화자의 평균 특징까지 흐려진다.
        const int main = main_speaker();
        size_t oldest = 0;
        for (size_t i = 0; i < impl_->speakers.size(); ++i) {
            if (static_cast<int>(oldest) == main ||
                (static_cast<int>(i) != main && impl_->speakers[i].last_heard < impl_->speakers[oldest].last_heard)) {
                oldest = i;
            }
        }
        impl_->speakers[oldest] = std::move(speaker);
        return static_cast<int>(oldest);
    }

    // 평균 특징을 갱신한다. 오래 들은 화자일수록 한 구간에 덜 흔들린다.
    Impl::Speaker& speaker = impl_->speakers[static_cast<size_t>(best)];
    const auto weight = static_cast<float>(speaker.segment_count);
    for (size_t i = 0; i < speaker.centroid.size() && i < embedding.size(); ++i) {
        speaker.centroid[i] = speaker.centroid[i] * weight + embedding[i];
    }
    normalize(speaker.centroid);
    ++speaker.segment_count;
    speaker.seconds += seconds;
    speaker.last_heard = impl_->heard_count;
    return best;
}

void SpeakerIdentifier::reset() {
    impl_->speakers.clear();
    impl_->heard_count = 0;
}

int SpeakerIdentifier::main_speaker() const {
    const auto& speakers = impl_->speakers;
    const auto longest = std::max_element(speakers.begin(), speakers.end(),
                                          [](const auto& a, const auto& b) { return a.seconds < b.seconds; });
    return longest == speakers.end() ? UNKNOWN : static_cast<int>(longest - speakers.begin());
}

}  // namespace overlay_trans
