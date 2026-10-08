#include "speech_recognizer.h"

#include "whisper_log.h"

#include <ggml-backend.h>
#include <whisper.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <thread>
#include <utility>

namespace overlay_trans {

namespace {

// 스레드를 물리 코어 수 이상으로 늘려도 whisper.cpp는 빨라지지 않는다.
constexpr unsigned int MAX_THREADS = 8;

// whisper 인코더 출력 한 칸에 해당하는 샘플 수(16kHz에서 20ms).
constexpr size_t SAMPLES_PER_AUDIO_CONTEXT = 320;

// 문장 부호를 뺀 형태로 적는다. (remove_punctuation의 결과와 비교한다.)
constexpr std::string_view KNOWN_HALLUCINATIONS[] = {
    "ご視聴ありがとうございました",
    "ご視聴ありがとうございます",
    "ご清聴ありがとうございました",
    "チャンネル登録お願いします",
    "チャンネル登録よろしくお願いします",
};

int default_thread_count() {
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, MAX_THREADS));
}

// 비교를 위해 공백과 문장 부호(ASCII 부호, 、 。)를 뺀 문자열을 만든다.
std::string remove_punctuation(std::string_view text) {
    constexpr std::string_view IDEOGRAPHIC_COMMA = "、";
    constexpr std::string_view IDEOGRAPHIC_PERIOD = "。";

    std::string result;
    for (size_t i = 0; i < text.size();) {
        const std::string_view rest = text.substr(i);
        if (rest.starts_with(IDEOGRAPHIC_COMMA) || rest.starts_with(IDEOGRAPHIC_PERIOD)) {
            i += IDEOGRAPHIC_COMMA.size();
            continue;
        }
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80 || std::isalnum(c) != 0) {
            result += text[i];
        }
        ++i;
    }
    return result;
}

// whisper.cpp에 넘길 GPU 번호를 구한다. wanted는 외장 GPU들 중에서의 순번이다.
//
// whisper.cpp는 외장 GPU와 내장 GPU를 구분하지 않고 발견한 순서대로 번호를 매기는데, 이 순서는 고정되어 있지 않다.
// (같은 PC에서도 원격 접속 여부에 따라 내장 GPU가 0번이 되는 경우가 있었다.)
// 내장 GPU가 선택되면 인식이 십여 배 느려지므로, 외장 GPU가 몇 번인지 직접 찾는다.
int find_whisper_gpu_index(int wanted) {
    int index = 0;
    int discrete_count = 0;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        const auto type = ggml_backend_dev_type(ggml_backend_dev_get(i));
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU) {
            if (discrete_count == wanted) {
                return index;
            }
            ++discrete_count;
        }
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
            ++index;
        }
    }
    // 외장 GPU가 없으면 처음 발견한 GPU(내장 GPU)를 쓴다.
    return 0;
}

// GPU가 OpenCL 백엔드(스냅드래곤 X의 Adreno GPU)로 잡혀 있는지 확인한다.
// whisper.cpp는 이 백엔드에 모델을 올리다가 비정상 종료하므로, 이때는 음성 인식을 CPU로 돌린다.
bool gpu_is_opencl() {
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        const ggml_backend_dev_t device = ggml_backend_dev_get(i);
        const auto type = ggml_backend_dev_type(device);
        if ((type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU) &&
            std::string_view(ggml_backend_reg_name(ggml_backend_dev_backend_reg(device))) == "OpenCL") {
            return true;
        }
    }
    return false;
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
    params.use_gpu = config.use_gpu && !gpu_is_opencl();
    params.gpu_device = find_whisper_gpu_index(config.gpu_device);
    impl_->context = whisper_init_from_file_with_params(model_path.string().c_str(), params);
    if (impl_->context == nullptr) {
        return false;
    }

    impl_->config = config;
    return true;
}

std::string SpeechRecognizer::transcribe(std::span<const float> samples, std::stop_token stop) {
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = impl_->config.language.c_str();
    if (!impl_->config.vocabulary_hint.empty()) {
        params.initial_prompt = impl_->config.vocabulary_hint.c_str();
    }
    params.n_threads = default_thread_count();
    // 발화 구간 하나를 독립된 한 문장으로 처리한다.
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.print_progress = false;
    if (impl_->config.audio_context > 0) {
        const int needed = static_cast<int>(samples.size() / SAMPLES_PER_AUDIO_CONTEXT) + 1;
        params.audio_ctx = std::min(std::max(impl_->config.audio_context, needed),
                                    whisper_model_n_audio_ctx(impl_->context));
    }
    params.abort_callback = [](void* data) {
        const bool stop_requested = static_cast<std::stop_token*>(data)->stop_requested();
        if (stop_requested) {
            // 요청받은 중단은 오류가 아니다. whisper.cpp가 중단하면서 남기는 실패 로그를 숨긴다.
            mute_whisper_log_on_this_thread(true);
        }
        return stop_requested;
    };
    params.abort_callback_user_data = &stop;

    const int result = whisper_full(impl_->context, params, samples.data(), static_cast<int>(samples.size()));
    mute_whisper_log_on_this_thread(false);
    if (result != 0 || stop.stop_requested()) {
        return {};
    }

    std::string text;
    const int segment_count = whisper_full_n_segments(impl_->context);
    for (int i = 0; i < segment_count; ++i) {
        text += whisper_full_get_segment_text(impl_->context, i);
    }

    // whisper.cpp는 텍스트 앞에 공백을 붙여 돌려준다.
    text.erase(0, text.find_first_not_of(' '));

    // 말소리가 뚜렷하지 않은 구간에서는 whisper가 어휘 힌트의 단어를 그대로 되풀이하는 일이 있다.
    // 결과가 힌트의 일부와 똑같으면 인식 실패로 본다.
    const std::string spoken = remove_punctuation(text);
    if (!spoken.empty() && remove_punctuation(impl_->config.vocabulary_hint).find(spoken) != std::string::npos) {
        return {};
    }

    // 말소리가 없는 짧은 구간에서 whisper가 지어내는 것으로 알려진 문장들. 학습에 쓰인 영상의 맺음말이다.
    for (const std::string_view phrase : KNOWN_HALLUCINATIONS) {
        if (spoken == phrase) {
            return {};
        }
    }
    return text;
}

}  // namespace overlay_trans
