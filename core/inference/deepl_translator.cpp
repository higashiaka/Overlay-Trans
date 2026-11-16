#include "deepl_translator.h"

#include "text/katakana.h"
#include "text/korean_style.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <utility>
#include <vector>

namespace overlay_trans {

namespace {

// 무료 키는 끝이 ":fx"이고 주소가 다르다.
constexpr const char* FREE_API_HOST = "api-free.deepl.com";
constexpr const char* PRO_API_HOST = "api.deepl.com";
constexpr const char* API_PATH = "/v2/translate";
constexpr std::string_view FREE_KEY_SUFFIX = ":fx";
// 맥락으로 보내는 앞 문장 수. 맥락은 번역 글자 수에 포함되지 않는다.
constexpr size_t CONTEXT_LINES = 3;

// DeepL의 대상 언어 코드. 지원하지 않는 언어면 빈 문자열을 반환한다.
std::string target_language_code(const std::string& language) {
    if (language == "Korean") {
        return "KO";
    }
    if (language == "English") {
        return "EN-US";
    }
    if (language == "Japanese") {
        return "JA";
    }
    return {};
}

}  // namespace

struct DeepLTranslator::Impl {
    std::unique_ptr<HttpsClient> client;
    TranslatorConfig config;
    DeepLTranslatorConfig deepl_config;
    std::string host;
    std::vector<std::string> history;  // 앞서 번역한 원문들
};

DeepLTranslator::DeepLTranslator(std::unique_ptr<HttpsClient> client, const TranslatorConfig& config,
                                 const DeepLTranslatorConfig& deepl_config)
    : impl_(std::make_unique<Impl>()) {
    impl_->client = std::move(client);
    impl_->config = config;
    impl_->deepl_config = deepl_config;
    const std::string& key = deepl_config.api_key;
    const bool is_free = key.size() >= FREE_KEY_SUFFIX.size() &&
                         std::string_view(key).substr(key.size() - FREE_KEY_SUFFIX.size()) == FREE_KEY_SUFFIX;
    impl_->host = is_free ? FREE_API_HOST : PRO_API_HOST;
}

DeepLTranslator::~DeepLTranslator() = default;

void DeepLTranslator::set_stream_info(const std::string&) {}

std::string DeepLTranslator::translate(std::string_view text, std::span<const std::string>, bool replace_previous,
                                       const PartialCallback&) {
    const std::string source(text);
    if (replace_previous && !impl_->history.empty()) {
        impl_->history.pop_back();
    }

    nlohmann::json body = {
        {"text", {source}},
        {"target_lang", target_language_code(impl_->config.target_language)},
        {"formality", "prefer_less"},
    };
    if (!impl_->deepl_config.source_language.empty()) {
        body["source_lang"] = impl_->deepl_config.source_language;
    }
    if (!impl_->history.empty()) {
        std::string context;
        const size_t first = impl_->history.size() > CONTEXT_LINES ? impl_->history.size() - CONTEXT_LINES : 0;
        for (size_t i = first; i < impl_->history.size(); ++i) {
            context += impl_->history[i] + "\n";
        }
        body["context"] = context;
    }

    impl_->history.push_back(source);
    if (impl_->history.size() > CONTEXT_LINES) {
        impl_->history.erase(impl_->history.begin());
    }

    const HttpsRequest request{
        .host = impl_->host,
        .path = API_PATH,
        .headers = {{"content-type", "application/json"},
                    {"authorization", "DeepL-Auth-Key " + impl_->deepl_config.api_key}},
        .body = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
    };
    std::string response;
    const int status = impl_->client->post(request, [&](std::string_view data) { response.append(data); });
    if (status == 0) {
        std::fprintf(stderr, "DeepL: could not connect.\n");
        return {};
    }
    const nlohmann::json parsed = nlohmann::json::parse(response, nullptr, false);
    if (status != 200) {
        const std::string message = parsed.is_object() ? parsed.value("message", "") : std::string();
        std::fprintf(stderr, "DeepL: HTTP %d %s\n", status, message.c_str());
        return {};
    }
    if (!parsed.is_object() || !parsed.contains("translations") || parsed["translations"].empty()) {
        return {};
    }

    std::string translation = parsed["translations"][0].value("text", "");
    if (impl_->config.target_language == "Korean") {
        // DeepL이 옮기지 않고 남긴 가타카나는 소리 나는 대로 한글로 바꾼다.
        translation = to_casual_korean(katakana_to_hangul(translation));
    }
    return translation;
}

}  // namespace overlay_trans
