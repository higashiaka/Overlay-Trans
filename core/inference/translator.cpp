#include "translator.h"

#include <llama.h>

#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>

namespace overlay_trans {

namespace {

constexpr unsigned int MAX_THREADS = 8;
constexpr int PIECE_BUFFER_SIZE = 256;

int default_thread_count() {
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, MAX_THREADS));
}

// llama.cpp는 모델 로딩 과정을 INFO 로그로 길게 남기므로 경고와 오류만 출력한다.
void log_warnings_only(ggml_log_level level, const char* text, void*) {
    if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) {
        std::fputs(text, stderr);
    }
}

std::string build_instruction(std::string_view target_language, std::string_view text) {
    std::string instruction = "You are a real-time subtitle translator for live streams. ";
    instruction += "Translate the following speech-recognition text into natural, colloquial ";
    instruction += target_language;
    instruction += ". Output only the translation, with no explanations or quotes.\n\n";
    instruction += text;
    return instruction;
}

}  // namespace

struct Translator::Impl {
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    llama_sampler* sampler = nullptr;
    const llama_vocab* vocab = nullptr;
    TranslatorConfig config;

    std::string apply_chat_template(const std::string& instruction) const;
    std::vector<llama_token> tokenize(const std::string& prompt) const;
};

std::string Translator::Impl::apply_chat_template(const std::string& instruction) const {
    const llama_chat_message message{"user", instruction.c_str()};
    const char* chat_template = llama_model_chat_template(model, nullptr);

    std::string prompt(instruction.size() + 256, '\0');
    int32_t length = llama_chat_apply_template(chat_template, &message, 1, true, prompt.data(),
                                               static_cast<int32_t>(prompt.size()));
    if (length > static_cast<int32_t>(prompt.size())) {
        prompt.resize(static_cast<size_t>(length));
        length = llama_chat_apply_template(chat_template, &message, 1, true, prompt.data(),
                                           static_cast<int32_t>(prompt.size()));
    }
    prompt.resize(static_cast<size_t>(std::max(length, 0)));

    // Qwen 계열처럼 답하기 전에 생각 과정을 출력하는 모델은, 빈 생각 블록을 미리 넣어 바로 답하게 한다.
    if (chat_template != nullptr && std::string_view(chat_template).find("enable_thinking") != std::string_view::npos) {
        prompt += "<think>\n\n</think>\n\n";
    }
    return prompt;
}

std::vector<llama_token> Translator::Impl::tokenize(const std::string& prompt) const {
    std::vector<llama_token> tokens(prompt.size() + 8);
    const int32_t count = llama_tokenize(vocab, prompt.data(), static_cast<int32_t>(prompt.size()), tokens.data(),
                                         static_cast<int32_t>(tokens.size()), true, true);
    tokens.resize(static_cast<size_t>(std::max(count, 0)));
    return tokens;
}

Translator::Translator() : impl_(std::make_unique<Impl>()) {}

Translator::~Translator() {
    if (impl_->sampler != nullptr) {
        llama_sampler_free(impl_->sampler);
    }
    if (impl_->context != nullptr) {
        llama_free(impl_->context);
    }
    if (impl_->model != nullptr) {
        llama_model_free(impl_->model);
    }
}

bool Translator::init(const std::filesystem::path& model_path, const TranslatorConfig& config) {
    llama_log_set(log_warnings_only, nullptr);
    llama_backend_init();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.gpu_layers;
    // GPU가 여러 개여도 지정한 장치 하나만 쓴다. (내장 GPU로 나뉘면 느려진다.)
    model_params.split_mode = LLAMA_SPLIT_MODE_NONE;
    model_params.main_gpu = config.gpu_device;

    impl_->model = llama_model_load_from_file(model_path.string().c_str(), model_params);
    if (impl_->model == nullptr) {
        return false;
    }
    impl_->vocab = llama_model_get_vocab(impl_->model);

    llama_context_params context_params = llama_context_default_params();
    context_params.n_ctx = config.context_size;
    context_params.n_threads = default_thread_count();
    context_params.n_threads_batch = default_thread_count();

    impl_->context = llama_init_from_model(impl_->model, context_params);
    if (impl_->context == nullptr) {
        return false;
    }

    // 같은 입력에 항상 같은 번역이 나오도록 가장 확률이 높은 토큰만 고른다.
    impl_->sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(impl_->sampler, llama_sampler_init_greedy());

    impl_->config = config;
    return true;
}

std::string Translator::translate(std::string_view text) {
    const std::string prompt =
        impl_->apply_chat_template(build_instruction(impl_->config.target_language, text));
    std::vector<llama_token> tokens = impl_->tokenize(prompt);
    if (tokens.empty()) {
        return {};
    }

    // 문장마다 독립적으로 번역하므로 이전 문장의 상태를 지운다.
    llama_memory_clear(llama_get_memory(impl_->context), true);

    if (llama_decode(impl_->context, llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()))) != 0) {
        return {};
    }

    std::string translation;
    for (int i = 0; i < impl_->config.max_output_tokens; ++i) {
        llama_token token = llama_sampler_sample(impl_->sampler, impl_->context, -1);
        if (llama_vocab_is_eog(impl_->vocab, token)) {
            break;
        }

        char piece[PIECE_BUFFER_SIZE];
        const int32_t length = llama_token_to_piece(impl_->vocab, token, piece, sizeof(piece), 0, false);
        if (length > 0) {
            translation.append(piece, static_cast<size_t>(length));
        }

        if (llama_decode(impl_->context, llama_batch_get_one(&token, 1)) != 0) {
            break;
        }
    }

    // 모델이 앞뒤에 붙이는 공백과 줄바꿈을 제거한다.
    const auto first = translation.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = translation.find_last_not_of(" \n\r\t");
    return translation.substr(first, last - first + 1);
}

}  // namespace overlay_trans
