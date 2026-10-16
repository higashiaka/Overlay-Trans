#pragma once

#include "inference/translator.h"

#include <memory>
#include <string>

namespace overlay_trans {

// 기계 번역(draft_source)으로 초안을 먼저 받고, 그 초안을 참고해 로컬 모델(LlamaTranslator)로 번역한다.
// 기계 번역은 낱말 뜻이 정확하지만 끊긴 말을 끊긴 대로 옮겨 읽기 어렵고, 로컬 모델은 문장을 자연스럽게 다듬지만
// 낱말을 틀리기 쉽다. 초안을 받지 못하면 로컬 모델만으로 번역한다.
class DraftedTranslator final : public Translator {
public:
    // translator는 TranslatorConfig::reference_drafts를 켜고 init한 것이어야 한다.
    // target_language가 "Korean"이면 번역문의 존댓말을 반말로 바꾼다.
    DraftedTranslator(std::unique_ptr<Translator> draft_source, std::unique_ptr<LlamaTranslator> translator,
                      const std::string& target_language);
    ~DraftedTranslator() override;

    void set_stream_info(const std::string& info) override;
    std::string translate(std::string_view text, std::span<const std::string> chat, bool replace_previous,
                          const PartialCallback& on_partial) override;

private:
    std::unique_ptr<Translator> draft_source_;
    std::unique_ptr<LlamaTranslator> translator_;
    std::string target_language_;
};

}  // namespace overlay_trans
