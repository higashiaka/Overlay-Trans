#pragma once

#include "inference/translator.h"
#include "server/subtitle_server.h"

#include <deque>
#include <memory>
#include <utility>

namespace overlay_trans {

// ChatGPT 구독 연결과 Claude CLI 로그인을 관리한다.
class CliAccounts {
public:
    CliAccounts();
    ~CliAccounts();
    CliAccountStatus status(const std::string& provider);
    bool connect(const std::string& provider);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class CliTranslator final : public Translator {
public:
    CliTranslator(std::string provider, std::string model, TranslatorConfig config);
    ~CliTranslator() override;
    void set_stream_info(const std::string& info) override;
    std::string translate(std::string_view text, std::span<const std::string> chat, bool replace_previous,
                          const PartialCallback& on_partial) override;
    std::string translate_with_draft(std::string_view text, std::span<const std::string> chat, bool replace_previous,
                          const PartialCallback& on_partial, const std::string& draft) override;

private:
    struct Session;
    std::unique_ptr<Session> session_;
    std::string provider_;
    std::string model_;
    TranslatorConfig config_;
    std::string stream_info_;
    std::string chatgpt_account_;
    std::deque<std::pair<std::string, std::string>> history_;
};

}  // namespace overlay_trans
