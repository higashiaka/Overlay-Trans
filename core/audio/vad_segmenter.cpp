#include "vad_segmenter.h"

#include "inference/whisper_log.h"

#include <whisper.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace overlay_trans {

namespace {

constexpr size_t SAMPLE_RATE = 16000;
// Silero VAD가 16kHz에서 한 번에 처리하는 창 크기(32ms).
constexpr size_t WINDOW_SAMPLES = 512;
// 발화 중에는 threshold보다 이만큼 낮아져야 무음으로 본다. (경계에서 끊기는 것을 방지)
constexpr float SILENCE_THRESHOLD_MARGIN = 0.15f;

constexpr size_t ms_to_samples(uint32_t ms) {
    return SAMPLE_RATE * ms / 1000;
}

constexpr uint32_t samples_to_ms(size_t samples) {
    return static_cast<uint32_t>(samples * 1000 / SAMPLE_RATE);
}

}  // namespace

struct VadSegmenter::Impl {
    whisper_vad_context* context = nullptr;
    VadConfig config;
    VadCallbacks callbacks;

    std::vector<float> pending;   // 아직 창 크기를 채우지 못한 샘플
    std::vector<float> pre_roll;  // 발화 시작 전 구간 (speech_pad_ms 분량)
    std::vector<float> segment;   // 진행 중인 발화
    std::vector<float> speech_probabilities;  // segment에서 pre_roll 뒤에 이어진 창들의 음성 확률
    size_t lead_samples = 0;      // segment 앞에 붙인 pre_roll 길이
    size_t silence_samples = 0;   // 발화 끝에 이어진 무음 길이
    size_t paused_samples = 0;    // on_pause로 미리 알린 구간의 길이. 알리지 않았으면 0
    bool in_speech = false;

    void on_window(const float* window, float probability);
    void cancel_pause();
    void end_segment();
    void cut_long_segment();
};

void VadSegmenter::Impl::on_window(const float* window, float probability) {
    if (!in_speech) {
        if (probability < config.threshold) {
            pre_roll.insert(pre_roll.end(), window, window + WINDOW_SAMPLES);
            const size_t pad_samples = ms_to_samples(config.speech_pad_ms);
            if (pre_roll.size() > pad_samples) {
                pre_roll.erase(pre_roll.begin(), pre_roll.end() - static_cast<std::ptrdiff_t>(pad_samples));
            }
            return;
        }

        in_speech = true;
        silence_samples = 0;
        lead_samples = pre_roll.size();
        segment = std::move(pre_roll);
        pre_roll.clear();
    }

    segment.insert(segment.end(), window, window + WINDOW_SAMPLES);
    speech_probabilities.push_back(probability);

    if (probability < config.threshold - SILENCE_THRESHOLD_MARGIN) {
        silence_samples += WINDOW_SAMPLES;
    } else {
        silence_samples = 0;
        cancel_pause();
    }

    // 발화가 길어질수록 더 짧은 쉼에서도 끊는다.
    const size_t speech_length = segment.size() - lead_samples;
    const bool is_long = speech_length >= ms_to_samples(config.long_speech_ms);
    const uint32_t required_silence_ms = is_long ? config.long_speech_silence_ms : config.min_silence_ms;

    if (silence_samples >= ms_to_samples(required_silence_ms)) {
        end_segment();
    } else if (speech_length >= ms_to_samples(config.max_speech_ms)) {
        cut_long_segment();
    } else if (!is_long && paused_samples == 0 && config.early_silence_ms != 0 && callbacks.on_pause &&
               silence_samples >= ms_to_samples(config.early_silence_ms) &&
               speech_length - silence_samples >= ms_to_samples(config.min_speech_ms)) {
        // 긴 발화는 확정까지 기다리는 시간이 이미 짧아 미리 알려서 얻는 것이 없다.
        paused_samples = segment.size();
        callbacks.on_pause(std::span<const float>(segment.data(), paused_samples));
    }
}

void VadSegmenter::Impl::cancel_pause() {
    if (paused_samples == 0) {
        return;
    }
    paused_samples = 0;
    if (callbacks.on_resume) {
        callbacks.on_resume();
    }
}

// 쉼 없이 이어지는 말을 강제로 끊는다. 단어 중간이 잘리는 것을 줄이기 위해,
// 최근 구간에서 말소리가 가장 약한 지점을 골라 그 앞까지만 내보내고 나머지는 다음 발화로 넘긴다.
void VadSegmenter::Impl::cut_long_segment() {
    const size_t window_count = speech_probabilities.size();
    const size_t lookback = std::min(window_count - 1, ms_to_samples(config.forced_cut_lookback_ms) / WINDOW_SAMPLES);
    const auto weakest = std::min_element(speech_probabilities.end() - static_cast<std::ptrdiff_t>(lookback),
                                          speech_probabilities.end());
    const auto cut_window = static_cast<size_t>(weakest - speech_probabilities.begin()) + 1;
    const size_t cut_samples = lead_samples + cut_window * WINDOW_SAMPLES;

    cancel_pause();
    callbacks.on_segment(std::span<const float>(segment.data(), cut_samples),
                         samples_to_ms(segment.size() - cut_samples));

    segment.erase(segment.begin(), segment.begin() + static_cast<std::ptrdiff_t>(cut_samples));
    speech_probabilities.erase(speech_probabilities.begin(),
                               speech_probabilities.begin() + static_cast<std::ptrdiff_t>(cut_window));
    lead_samples = 0;
    silence_samples = 0;
}

void VadSegmenter::Impl::end_segment() {
    // 끝에 붙은 무음은 speech_pad_ms 분량만 남긴다.
    // on_pause로 미리 알린 구간이 있으면, 받은 쪽이 그 처리 결과를 그대로 쓸 수 있게 똑같은 구간을 내보낸다.
    const size_t pad_samples = ms_to_samples(config.speech_pad_ms);
    const size_t trimmed = silence_samples > pad_samples ? silence_samples - pad_samples : 0;
    const size_t length = paused_samples != 0 ? paused_samples : segment.size() - trimmed;

    const size_t speech_samples = segment.size() - lead_samples - silence_samples;
    if (speech_samples >= ms_to_samples(config.min_speech_ms)) {
        callbacks.on_segment(std::span<const float>(segment.data(), length), samples_to_ms(silence_samples));
    }

    segment.clear();
    speech_probabilities.clear();
    silence_samples = 0;
    paused_samples = 0;
    in_speech = false;
    whisper_vad_reset_state(context);
}

VadSegmenter::VadSegmenter() : impl_(std::make_unique<Impl>()) {}

VadSegmenter::~VadSegmenter() {
    if (impl_->context != nullptr) {
        whisper_vad_free(impl_->context);
    }
}

bool VadSegmenter::init(const std::filesystem::path& model_path, const VadConfig& config, VadCallbacks callbacks) {
    quiet_whisper_log();

    whisper_vad_context_params params = whisper_vad_default_context_params();
    params.n_threads = 1;  // 모델이 작아 스레드를 늘려도 이득이 없다.

    impl_->context = whisper_vad_init_from_file_with_params(model_path.string().c_str(), params);
    if (impl_->context == nullptr) {
        return false;
    }
    // 모델 내부 상태는 생성 직후 초기화되어 있지 않다. 지우지 않으면 실행할 때마다 결과가 달라진다.
    whisper_vad_reset_state(impl_->context);

    impl_->config = config;
    impl_->callbacks = std::move(callbacks);
    return true;
}

void VadSegmenter::process(const float* samples, size_t count) {
    auto& pending = impl_->pending;
    pending.insert(pending.end(), samples, samples + count);

    const size_t window_count = pending.size() / WINDOW_SAMPLES;
    if (window_count == 0) {
        return;
    }

    const size_t usable = window_count * WINDOW_SAMPLES;
    if (!whisper_vad_detect_speech_no_reset(impl_->context, pending.data(), static_cast<int>(usable))) {
        pending.clear();
        return;
    }

    const float* probabilities = whisper_vad_probs(impl_->context);
    for (size_t i = 0; i < window_count; ++i) {
        impl_->on_window(pending.data() + i * WINDOW_SAMPLES, probabilities[i]);
    }

    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(usable));
}

void VadSegmenter::flush() {
    if (impl_->in_speech) {
        impl_->end_segment();
    }
}

}  // namespace overlay_trans
