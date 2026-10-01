#include "speech_recognizer.h"

#include "whisper_log.h"

#include <whisper.h>

#include <algorithm>
#include <thread>

namespace overlay_trans {

namespace {

// 스레드를 물리 코어 수 이상으로 늘려도 whisper.cpp는 빨라지지 않는다.
constexpr unsigned int MAX_THREADS = 8;

int default_thread_count() {
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, MAX_THREADS));
}

}  // namespace

struct SpeechRecognizer::Impl {
    whisper_context* context = nullptr;
    SttConfig config;
};

SpeechRecognizer::SpeechRecognizer() : impl_(std::make_unique<Impl>()) {}

SpeechRecognizer::~SpeechRecognizer() {
    if (impl_->context != nullptr) {
        whisper_free(impl_->context);
    }
}

bool SpeechRecognizer::init(const std::filesystem::path& model_path, const SttConfig& config) {
    quiet_whisper_log();

    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = config.use_gpu;
    params.gpu_device = config.gpu_device;
    impl_->context = whisper_init_from_file_with_params(model_path.string().c_str(), params);
    if (impl_->context == nullptr) {
        return false;
    }

    impl_->config = config;
    return true;
}

std::string SpeechRecognizer::transcribe(std::span<const float> samples) {
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = impl_->config.language.c_str();
    params.n_threads = default_thread_count();
    // 발화 구간 하나를 독립된 한 문장으로 처리한다.
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.print_progress = false;

    if (whisper_full(impl_->context, params, samples.data(), static_cast<int>(samples.size())) != 0) {
        return {};
    }

    std::string text;
    const int segment_count = whisper_full_n_segments(impl_->context);
    for (int i = 0; i < segment_count; ++i) {
        text += whisper_full_get_segment_text(impl_->context, i);
    }

    // whisper.cpp는 텍스트 앞에 공백을 붙여 돌려준다.
    text.erase(0, text.find_first_not_of(' '));
    return text;
}

}  // namespace overlay_trans
