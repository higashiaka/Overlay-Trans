#include "drafted_translator.h"

#include "text/korean_style.h"

#include <algorithm>
#include <utility>

namespace overlay_trans {

namespace {

// 로컬 모델의 번역문이 초안보다 이 비율보다 짧으면 원문 일부를 빠뜨린 것으로 본다.
// 합친 긴 문장에서 뒷부분을 통째로 빼먹는 일이 있었다. ("…楽しいんだけど一生仲間撃ってる" -> "진짜 서툴지만 재밌어.")
constexpr double MIN_LENGTH_RATIO = 0.6;
// 초안이 이보다 짧으면 비교하지 않는다. 짧은 말은 길이 차이가 자연스러운 표현 차이일 때가 많다.
constexpr size_t MIN_DRAFT_LENGTH = 10;
// 로컬 모델의 번역문이 초안(짧으면 MIN_DRAFT_LENGTH)보다 이 비율보다 길면 같은 말을 되풀이하고 있는 것으로 본다.
// "おいおいおい…"를 "이봐, 이봐, …"로 출력 한도까지 되풀이하고 뒷부분을 빠뜨린 일이 있었다.
constexpr double MAX_LENGTH_RATIO = 2.5;

// 공백과 문장 부호를 뺀 글자 수. UTF-8 글자 하나를 한 글자로 센다.
size_t content_length(std::string_view text) {
    size_t length = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if ((byte & 0xC0) == 0x80) {
            continue;  // 여러 바이트 글자의 뒷부분
        }
        if (byte < 0x80 && (byte == ' ' || std::string_view(".,?!~'\"()-").find(text[i]) != std::string_view::npos)) {
            continue;
        }
        ++length;
    }
    return length;
}

}  // namespace

DraftedTranslator::DraftedTranslator(std::unique_ptr<Translator> draft_source,
                                     std::unique_ptr<Translator> translator, const std::string& target_language, bool preview_draft)
    : draft_source_(std::move(draft_source)), translator_(std::move(translator)), target_language_(target_language),
      preview_draft_(preview_draft) {}

DraftedTranslator::~DraftedTranslator() = default;

void DraftedTranslator::set_stream_info(const std::string& info) {
    draft_source_->set_stream_info(info);
    translator_->set_stream_info(info);
}

void DraftedTranslator::set_compare_mode(bool enabled) {
    compare_mode_.store(enabled, std::memory_order_relaxed);
}

std::string DraftedTranslator::translate(std::string_view text, std::span<const std::string> chat,
                                         bool replace_previous, const PartialCallback& on_partial) {
    const bool compare_mode = compare_mode_.load(std::memory_order_relaxed);
    // CLI 보정 모드에서는 초안을 먼저 보여 주고, 완성된 보정문으로 교체한다.
    const std::string draft = draft_source_->translate(text, {}, replace_previous, {});
    const std::string comparison_prefix = "DeepL: " + (draft.empty() ? std::string("[연결 실패]") : draft) +
                                          "\nHybrid: ";
    PartialCallback hybrid_partial = on_partial;
    if (preview_draft_ && !compare_mode && on_partial && !draft.empty()) {
        on_partial(draft);
        // 짧은 토큰마다 전체 초안을 지웠다 다시 그리지 않도록 보정 완료까지 초안을 유지한다.
        hybrid_partial = {};
    }
    if (compare_mode && on_partial) {
        hybrid_partial = [&, comparison_prefix](std::string_view partial) {
            on_partial(comparison_prefix + std::string(partial));
        };
    }
    std::string translation = translator_->translate_with_draft(text, chat, replace_previous, hybrid_partial, draft);

    // 초안보다 눈에 띄게 짧으면 빠뜨린 부분이 있고, 지나치게 길면 같은 말을 되풀이하고 있는 것이므로 초안을 쓴다.
    // 초안은 딱딱해도 빠뜨리거나 되풀이하지 않는다.
    const auto draft_length = static_cast<double>(content_length(draft));
    const auto translation_length = static_cast<double>(content_length(translation));
    if (!draft.empty()) {
        const bool dropped = translation.empty() ||
            (draft_length >= MIN_DRAFT_LENGTH && translation_length < MIN_LENGTH_RATIO * draft_length);
        const bool repeating =
            translation_length > MAX_LENGTH_RATIO * std::max(draft_length, static_cast<double>(MIN_DRAFT_LENGTH));
        if (dropped || repeating) {
            translation = draft;
        }
    }
    // 초안의 존댓말을 모델이 따라 쓰는 경우가 있어 최종 번역문도 반말로 맞춘다.
    const std::string final_translation = target_language_ == "Korean" ? to_casual_korean(translation) : translation;
    return compare_mode ? comparison_prefix + final_translation : final_translation;
}

}  // namespace overlay_trans
