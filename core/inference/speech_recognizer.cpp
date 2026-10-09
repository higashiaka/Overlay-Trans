#include "speech_recognizer.h"

#include "whisper_log.h"

#include <ggml-backend.h>
#include <whisper.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>
#include <utility>

namespace overlay_trans {

namespace {

// 스레드를 물리 코어 수 이상으로 늘려도 whisper.cpp는 빨라지지 않는다.
constexpr unsigned int MAX_THREADS = 8;

constexpr double SAMPLE_RATE = 16000.0;
// whisper 인코더 출력 한 칸에 해당하는 샘플 수(16kHz에서 20ms).
constexpr size_t SAMPLES_PER_AUDIO_CONTEXT = 320;

// 발화 하나에서 나올 수 있다고 보는 토큰 수의 한도. 실제 방송 음성에서 정상적으로 인식된 발화는
// 1초에 많아야 8토큰이었고, 이 한도의 40%를 넘지 않았다.
constexpr int MAX_TOKENS_BASE = 16;
constexpr double MAX_TOKENS_PER_SECOND = 10.0;

// 문장 부호를 뺀 형태로 적는다. (remove_punctuation의 결과와 비교한다.)
constexpr std::string_view KNOWN_HALLUCINATIONS[] = {
    "ご視聴ありがとうございました",
    "ご視聴ありがとうございます",
    "ご清聴ありがとうございました",
    "チャンネル登録お願いします",
    "チャンネル登録よろしくお願いします",
};

int default_thread_count() {
    const unsigned int cores = std::thread::hardware_concurrency();
#if defined(_M_ARM64) || defined(__aarch64__)
    // ARM 프로세서는 코어 하나에 스레드가 하나라 절반으로 줄일 이유가 없다. 다만 전부 쓰면 오히려 느려져 두 개를 남긴다.
    // (스냅드래곤 X 8코어에서 같은 음성을 처리한 시간: 4개 59초, 6개 51초, 8개 105초)
    return static_cast<int>(std::clamp(cores > 2 ? cores - 2 : 1u, 1u, MAX_THREADS));
#else
    return static_cast<int>(std::clamp(cores / 2, 1u, MAX_THREADS));
#endif
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
    struct AbortState {
        std::stop_token stop;
        std::chrono::steady_clock::time_point deadline;
        int max_tokens;
        bool timed_out = false;
        bool repeating = false;
    };
    const uint32_t time_limit_ms = impl_->config.time_limit_ms;
    const double seconds = static_cast<double>(samples.size()) / SAMPLE_RATE;
    AbortState abort_state{stop,
                           time_limit_ms > 0
                               ? std::chrono::steady_clock::now() + std::chrono::milliseconds(time_limit_ms)
                               : std::chrono::steady_clock::time_point::max(),
                           MAX_TOKENS_BASE + static_cast<int>(MAX_TOKENS_PER_SECOND * seconds)};
    // 말소리가 뚜렷하지 않은 구간에서는 whisper가 같은 글자를 한도(220토큰)까지 되풀이하고, 결과가 이상하다고 보고
    // 설정을 바꿔 여러 번 다시 시도한다. 1초 남짓한 구간에 10초 넘게 걸리는 일이 있었다.
    // 발화 길이에 비해 토큰이 지나치게 많아지면 되풀이하는 것으로 보고 그 구간의 인식을 그만둔다.
    params.logits_filter_callback = [](whisper_context*, whisper_state*, const whisper_token_data*, int token_count,
                                       float*, void* data) {
        auto* state = static_cast<AbortState*>(data);
        if (token_count >= state->max_tokens) {
            state->repeating = true;
        }
    };
    params.logits_filter_callback_user_data = &abort_state;
    params.abort_callback = [](void* data) {
        auto* state = static_cast<AbortState*>(data);
        if (std::chrono::steady_clock::now() >= state->deadline) {
            state->timed_out = true;
        }
        const bool aborted = state->timed_out || state->repeating || state->stop.stop_requested();
        if (aborted) {
            // 요청받은 중단, 시간 초과, 되풀이는 오류가 아니다. whisper.cpp가 중단하면서 남기는 실패 로그를 숨긴다.
            mute_whisper_log_on_this_thread(true);
        }
        return aborted;
    };
    params.abort_callback_user_data = &abort_state;

    const int result = whisper_full(impl_->context, params, samples.data(), static_cast<int>(samples.size()));
    mute_whisper_log_on_this_thread(false);
    if (abort_state.timed_out && !stop.stop_requested()) {
        std::fprintf(stderr, "Speech recognition took longer than %u ms and was stopped.\n", time_limit_ms);
    }
    if (result != 0 || abort_state.timed_out || abort_state.repeating || stop.stop_requested()) {
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
