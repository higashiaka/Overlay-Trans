#pragma once

#include "inference/translator.h"

#include <atomic>
#include <memory>
#include <string>

namespace overlay_trans {

// 기계 번역(draft_source)으로 초안을 먼저 받고, 그 초안을 참고해 로컬 모델이나 CLI로 번역한다.
// 기계 번역은 낱말 뜻이 정확하지만 끊긴 말을 끊긴 대로 옮겨 읽기 어렵고, 로컬 모델은 문장을 자연스럽게 다듬지만
// 낱말을 틀리기 쉽다. 초안을 받지 못하면 로컬 모델만으로 번역한다.
class DraftedTranslator final : public Translator {
public:
    // translator는 TranslatorConfig::reference_drafts를 켜고 init한 것이어야 한다.
    // target_language가 "Korean"이면 번역문의 존댓말을 반말로 바꾼다.
    DraftedTranslator(std::unique_ptr<Translator> draft_source, std::unique_ptr<Translator> translator,
                      const std::string& target_language, bool preview_draft = false);
    ~DraftedTranslator() override;

    void set_stream_info(const std::string& info) override;
    void set_compare_mode(bool enabled);
    std::string translate(std::string_view text, std::span<const std::string> chat, bool replace_previous,
                          const PartialCallback& on_partial) override;

private:
    std::unique_ptr<Translator> draft_source_;
    std::unique_ptr<Translator> translator_;
    std::string target_language_;
    bool preview_draft_ = false;
    std::atomic<bool> compare_mode_{false};
};

}  // namespace overlay_trans
