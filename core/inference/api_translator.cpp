#include "api_translator.h"

#include "inference/translation_prompt.h"
#include "text/katakana.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

namespace overlay_trans {

namespace {

constexpr const char* API_HOST = "api.anthropic.com";
constexpr const char* API_PATH = "/v1/messages";
constexpr const char* API_VERSION = "2023-06-01";
// 자막 한 줄에는 넉넉한 길이. 실제로 만든 만큼만 요금이 든다.
constexpr int MAX_OUTPUT_TOKENS = 512;
// 만들어지는 중인 번역문을 알리는 최소 간격. 로컬 모델과 같게 둔다.
constexpr auto PARTIAL_INTERVAL = std::chrono::milliseconds(120);
// 맥락으로 보내는 이전 문장이 이만큼 쌓이면 최근 context_lines개만 남긴다.
// 한 문장씩 밀어내면 대화의 앞부분이 매번 달라져서 API의 프롬프트 캐시를 쓰지 못한다.
constexpr size_t MAX_HISTORY_LINES = 24;

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \n\r\t");
    return text.substr(first, last - first + 1);
}

}  // namespace

struct ApiTranslator::Impl {
    struct Line {
        std::string message;      // API에 보낸 내용 (채팅 맥락이 붙어 있을 수 있다)
        std::string translation;  // 번역하지 못했으면 비어 있다
    };

    std::unique_ptr<HttpsClient> client;
    TranslatorConfig config;
    ApiTranslatorConfig api_config;
    std::string system_prompt;
    std::vector<Line> history;

    std::string build_request(const std::string& message) const;
    // 응답 스트림(SSE)을 읽어 번역문을 만든다. 실패하면 빈 문자열을 반환한다.
    std::string request(const std::string& message, const PartialCallback& on_partial);
};

std::string ApiTranslator::Impl::build_request(const std::string& message) const {
    nlohmann::json messages = nlohmann::json::array();
    for (const TranslationExample& example : translation_examples(config.target_language)) {
        messages.push_back({{"role", "user"}, {"content", example.source}});
        messages.push_back({{"role", "assistant"}, {"content", example.translation}});
    }
    for (const Line& line : history) {
        // 빈 답은 보낼 수 없으므로 번역하지 못한 문장은 맥락에서 뺀다.
        if (line.translation.empty()) {
            continue;
        }
        messages.push_back({{"role", "user"}, {"content", line.message}});
        messages.push_back({{"role", "assistant"}, {"content", line.translation}});
    }
    messages.push_back({{"role", "user"}, {"content", message}});

    const nlohmann::json body = {
        {"model", api_config.model},
        {"max_tokens", MAX_OUTPUT_TOKENS},
        {"stream", true},
        // 번역은 생각 과정 없이 바로 답하는 편이 빠르다. 자막은 늦으면 쓸모가 없다.
        {"thinking", {{"type", "disabled"}}},
        {"output_config", {{"effort", "low"}}},
        // 지시문, 예시, 이전 문장은 요청마다 같으므로 캐시해서 다시 읽는다.
        {"cache_control", {{"type", "ephemeral"}}},
        {"system", system_prompt},
        {"messages", std::move(messages)},
    };
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string ApiTranslator::Impl::request(const std::string& message, const PartialCallback& on_partial) {
    const HttpsRequest http_request{
        .host = API_HOST,
        .path = API_PATH,
        .headers = {{"content-type", "application/json"},
                    {"x-api-key", api_config.api_key},
                    {"anthropic-version", API_VERSION}},
        .body = build_request(message),
    };

    std::string received;  // 아직 줄 단위로 처리하지 않은 응답
    std::string body;      // 오류 응답의 본문
    std::string text;
    std::string stop_reason;
    std::string error;
    std::chrono::steady_clock::time_point last_partial_at;

    const auto handle_event = [&](const nlohmann::json& event) {
        const std::string type = event.value("type", "");
        if (type == "content_block_delta") {
            const nlohmann::json delta = event.value("delta", nlohmann::json::object());
            if (delta.value("type", "") != "text_delta") {
                return;
            }
            text += delta.value("text", "");
            if (on_partial && std::chrono::steady_clock::now() - last_partial_at >= PARTIAL_INTERVAL) {
                const std::string partial = trim(text);
                if (!partial.empty()) {
                    on_partial(partial);
                    last_partial_at = std::chrono::steady_clock::now();
                }
            }
        } else if (type == "message_delta") {
            stop_reason = event.value("delta", nlohmann::json::object()).value("stop_reason", "");
        } else if (type == "error") {
            error = event.value("error", nlohmann::json::object()).value("message", "");
        }
    };

    const int status = client->post(http_request, [&](std::string_view data) {
        body.append(data);
        received.append(data);
        size_t line_end;
        while ((line_end = received.find('\n')) != std::string::npos) {
            std::string line = received.substr(0, line_end);
            received.erase(0, line_end + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.rfind("data: ", 0) != 0) {
                continue;
            }
            const nlohmann::json event = nlohmann::json::parse(line.substr(6), nullptr, false);
            if (event.is_object()) {
                handle_event(event);
            }
        }
    });

    if (status == 0) {
        std::fprintf(stderr, "Translation API: could not connect.\n");
        return {};
    }
    if (status != 200) {
        const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
        if (parsed.is_object()) {
            error = parsed.value("error", nlohmann::json::object()).value("message", "");
        }
        std::fprintf(stderr, "Translation API: HTTP %d %s\n", status, error.c_str());
        return {};
    }
    if (!error.empty()) {
        std::fprintf(stderr, "Translation API: %s\n", error.c_str());
        return {};
    }
    // 안전 분류기가 거절한 답은 중간까지 만들어졌어도 쓰지 않는다.
    if (stop_reason == "refusal") {
        return {};
    }
    return trim(text);
}

ApiTranslator::ApiTranslator(std::unique_ptr<HttpsClient> client, const TranslatorConfig& config,
                             const ApiTranslatorConfig& api_config)
    : impl_(std::make_unique<Impl>()) {
    impl_->client = std::move(client);
    impl_->config = config;
    impl_->api_config = api_config;
    impl_->system_prompt = build_system_prompt(config, {});
}

ApiTranslator::~ApiTranslator() = default;

void ApiTranslator::set_stream_info(const std::string& info) {
    impl_->system_prompt = build_system_prompt(impl_->config, info);
}

std::string ApiTranslator::translate(std::string_view text, std::span<const std::string> chat,
                                     bool replace_previous, const PartialCallback& on_partial) {
    const std::string message = build_user_message(std::string(text), chat);

    if (replace_previous && !impl_->history.empty()) {
        impl_->history.pop_back();
    }
    if (impl_->history.size() >= MAX_HISTORY_LINES) {
        impl_->history.erase(impl_->history.begin(),
                             impl_->history.end() - static_cast<std::ptrdiff_t>(impl_->config.context_lines));
    }

    std::string translation = impl_->request(message, on_partial);
    // 모델이 옮기지 않고 남긴 가타카나는 소리 나는 대로 한글로 바꾼다.
    if (impl_->config.target_language == "Korean") {
        translation = katakana_to_hangul(translation);
    }

    impl_->history.push_back({message, translation});
    return translation;
}

}  // namespace overlay_trans
