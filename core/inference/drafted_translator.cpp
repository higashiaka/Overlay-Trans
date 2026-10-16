#include "drafted_translator.h"

#include "text/korean_style.h"

#include <utility>

namespace overlay_trans {

namespace {

// 로컬 모델의 번역문이 초안보다 이 비율보다 짧으면 원문 일부를 빠뜨린 것으로 본다.
// 합친 긴 문장에서 뒷부분을 통째로 빼먹는 일이 있었다. ("…楽しいんだけど一生仲間撃ってる" -> "진짜 서툴지만 재밌어.")
constexpr double MIN_LENGTH_RATIO = 0.6;
// 초안이 이보다 짧으면 비교하지 않는다. 짧은 말은 길이 차이가 자연스러운 표현 차이일 때가 많다.
constexpr size_t MIN_DRAFT_LENGTH = 10;

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
                                     std::unique_ptr<LlamaTranslator> translator, const std::string& target_language)
    : draft_source_(std::move(draft_source)), translator_(std::move(translator)), target_language_(target_language) {}

DraftedTranslator::~DraftedTranslator() = default;

void DraftedTranslator::set_stream_info(const std::string& info) {
    draft_source_->set_stream_info(info);
    translator_->set_stream_info(info);
}

std::string DraftedTranslator::translate(std::string_view text, std::span<const std::string> chat,
                                         bool replace_previous, const PartialCallback& on_partial) {
    // 초안은 중간 결과 없이 한 번에 받는다. 자막에는 로컬 모델이 만드는 번역문만 보낸다.
    const std::string draft = draft_source_->translate(text, {}, replace_previous, {});
    std::string translation = translator_->translate(text, chat, replace_previous, on_partial, draft);

    // 초안보다 눈에 띄게 짧으면 빠뜨린 부분이 있는 것이므로 초안을 쓴다. 초안은 딱딱해도 빠뜨리지 않는다.
    const size_t draft_length = content_length(draft);
    if (draft_length >= MIN_DRAFT_LENGTH &&
        static_cast<double>(content_length(translation)) < MIN_LENGTH_RATIO * static_cast<double>(draft_length)) {
        translation = draft;
    }
    // 초안의 존댓말을 모델이 따라 쓰는 경우가 있어 최종 번역문도 반말로 맞춘다.
    return target_language_ == "Korean" ? to_casual_korean(translation) : translation;
}

}  // namespace overlay_trans
