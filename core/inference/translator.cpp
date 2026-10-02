#include "translator.h"

#include <llama.h>

#include <algorithm>
#include <cstdio>
#include <thread>
#include <utility>
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

std::string build_system_prompt(const TranslatorConfig& config) {
    std::string prompt = "You are a subtitle translator for a live stream. ";
    prompt += "Each user message is one line of speech-recognition text. ";
    prompt += "Reply with only its natural, colloquial " + config.target_language + " translation. ";
    prompt += "Stay faithful to the original: do not add, explain, or answer anything. ";
    prompt += "Earlier lines are context for understanding the current one.";

    if (!config.glossary.empty()) {
        prompt += "\n\nGlossary (always use these translations):\n" + config.glossary;
    }
    return prompt;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \n\r\t");
    return text.substr(first, last - first + 1);
}

}  // namespace

struct Translator::Impl {
    struct Line {
        std::string source;
        std::string translation;
    };

    llama_model* model = nullptr;
    llama_context* context = nullptr;
    llama_sampler* sampler = nullptr;
    const llama_vocab* vocab = nullptr;
    TranslatorConfig config;

    std::string system_prompt;
    std::vector<Line> history;         // 모델의 메모리에 들어 있는 이전 문장들
    size_t formatted_history_size = 0;  // history까지를 대화 형식으로 만든 문자열의 길이

    // history 뒤에 current_source를 붙인 대화를 모델의 형식으로 만든다.
    // add_reply_start가 true면 모델이 답을 시작할 위치까지 포함한다.
    std::string format_chat(const std::string* current_source, bool add_reply_start) const;
    std::vector<llama_token> tokenize(const std::string& text, bool is_first) const;
    bool fits_in_context(size_t new_token_count) const;
    void forget_old_lines();
    std::string generate();
};

std::string Translator::Impl::format_chat(const std::string* current_source, bool add_reply_start) const {
    std::vector<llama_chat_message> messages;
    messages.push_back({"system", system_prompt.c_str()});
    for (const Line& line : history) {
        messages.push_back({"user", line.source.c_str()});
        messages.push_back({"assistant", line.translation.c_str()});
    }
    if (current_source != nullptr) {
        messages.push_back({"user", current_source->c_str()});
    }

    const char* chat_template = llama_model_chat_template(model, nullptr);
    std::string formatted(1024, '\0');
    int32_t length = llama_chat_apply_template(chat_template, messages.data(), messages.size(), add_reply_start,
                                               formatted.data(), static_cast<int32_t>(formatted.size()));
    if (length > static_cast<int32_t>(formatted.size())) {
        formatted.resize(static_cast<size_t>(length));
        length = llama_chat_apply_template(chat_template, messages.data(), messages.size(), add_reply_start,
                                           formatted.data(), static_cast<int32_t>(formatted.size()));
    }
    formatted.resize(static_cast<size_t>(std::max(length, 0)));

    // Qwen 계열처럼 답하기 전에 생각 과정을 출력하는 모델은, 빈 생각 블록을 미리 넣어 바로 답하게 한다.
    if (add_reply_start && chat_template != nullptr &&
        std::string_view(chat_template).find("enable_thinking") != std::string_view::npos) {
        formatted += "<think>\n\n</think>\n\n";
    }
    return formatted;
}

std::vector<llama_token> Translator::Impl::tokenize(const std::string& text, bool is_first) const {
    std::vector<llama_token> tokens(text.size() + 8);
    const int32_t count = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()), tokens.data(),
                                         static_cast<int32_t>(tokens.size()), is_first, true);
    tokens.resize(static_cast<size_t>(std::max(count, 0)));
    return tokens;
}

bool Translator::Impl::fits_in_context(size_t new_token_count) const {
    const auto used = static_cast<size_t>(llama_memory_seq_pos_max(llama_get_memory(context), 0) + 1);
    return used + new_token_count + static_cast<size_t>(config.max_output_tokens) <= llama_n_ctx(context);
}

void Translator::Impl::forget_old_lines() {
    if (history.size() > config.context_lines) {
        history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(config.context_lines));
    }
    llama_memory_clear(llama_get_memory(context), true);
    formatted_history_size = 0;
}

std::string Translator::Impl::generate() {
    std::string text;
    for (int i = 0; i < config.max_output_tokens; ++i) {
        llama_token token = llama_sampler_sample(sampler, context, -1);
        if (llama_vocab_is_eog(vocab, token)) {
            break;
        }

        char piece[PIECE_BUFFER_SIZE];
        const int32_t length = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
        if (length > 0) {
            text.append(piece, static_cast<size_t>(length));
        }

        if (llama_decode(context, llama_batch_get_one(&token, 1)) != 0) {
            break;
        }
    }
    return trim(text);
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
    impl_->system_prompt = build_system_prompt(config);
    return true;
}

std::string Translator::translate(std::string_view text) {
    const std::string source(text);

    // 이전 문장들은 이미 모델의 메모리에 있으므로, 새로 늘어난 부분만 이어서 넣는다.
    std::string formatted = impl_->format_chat(&source, true);
    std::vector<llama_token> tokens =
        impl_->tokenize(formatted.substr(impl_->formatted_history_size), impl_->formatted_history_size == 0);

    // 메모리가 차면 오래된 문장을 버리고 최근 문장들만으로 다시 시작한다.
    if (!impl_->fits_in_context(tokens.size())) {
        impl_->forget_old_lines();
        formatted = impl_->format_chat(&source, true);
        tokens = impl_->tokenize(formatted, true);
    }
    if (tokens.empty()) {
        return {};
    }

    if (llama_decode(impl_->context, llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()))) != 0) {
        impl_->history.clear();
        impl_->forget_old_lines();
        return {};
    }

    std::string translation = impl_->generate();

    impl_->history.push_back({source, translation});
    impl_->formatted_history_size = impl_->format_chat(nullptr, false).size();
    return translation;
}

}  // namespace overlay_trans
