#pragma once

#include "inference/translator.h"
#include "net/https_client.h"

#include <memory>
#include <string>

namespace overlay_trans {

struct ApiTranslatorConfig {
    std::string api_key;
    std::string model = "claude-haiku-5-5";
};

// Claude API로 인식된 문장을 번역한다. 지시문과 예시는 로컬 모델과 같은 것을 쓴다.
class ApiTranslator final : public Translator {
public:
    ApiTranslator(std::unique_ptr<HttpsClient> client, const TranslatorConfig& config,
                  const ApiTranslatorConfig& api_config);
    ~ApiTranslator() override;

    void set_stream_info(const std::string& info) override;
    std::string translate(std::string_view text, std::span<const std::string> chat, bool replace_previous,
                          const PartialCallback& on_partial) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
