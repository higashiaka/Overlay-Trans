#pragma once

#include "inference/translator.h"
#include "net/https_client.h"

#include <memory>
#include <string>

namespace overlay_trans {

struct DeepLTranslatorConfig {
    std::string api_key;
    // 원문 언어 코드(예: "JA"). 비우면 DeepL이 감지한다.
    std::string source_language;
};

// DeepL API로 인식된 문장을 번역한다. 앞 문장들은 번역하지 않는 참고 맥락으로 함께 보낸다.
// 한국어로 번역할 때는 존댓말 어미를 반말로 바꾼다. 방송인은 반말로 말하는데 DeepL은 존댓말로 옮기는 경우가 많다.
class DeepLTranslator final : public Translator {
public:
    DeepLTranslator(std::unique_ptr<HttpsClient> client, const TranslatorConfig& config,
                    const DeepLTranslatorConfig& deepl_config);
    ~DeepLTranslator() override;

    // DeepL에는 지시문이 없으므로 방송 정보는 쓰지 않는다.
    void set_stream_info(const std::string& info) override;
    std::string translate(std::string_view text, std::span<const std::string> chat, bool replace_previous,
                          const PartialCallback& on_partial) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
