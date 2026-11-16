#pragma once

#include "inference/translator.h"

#include <span>
#include <string>
#include <string_view>

namespace overlay_trans {

// 로컬 번역 모델(LlamaTranslator)에 넣는 번역 지시문과 예시.

struct TranslationExample {
    const char* source;
    const char* draft;  // 기계 번역 초안. TranslatorConfig::reference_drafts가 켜져 있을 때만 쓴다.
    const char* translation;
};

std::string build_system_prompt(const TranslatorConfig& config, const std::string& stream_info);

// 대화 앞에 넣을 번역 예시. 대상 언어에 맞는 예시가 없으면 비어 있다.
std::span<const TranslationExample> translation_examples(const std::string& target_language);

// 번역할 문장을 모델에 넣을 내용으로 만든다. 채팅이나 기계 번역 초안이 있으면 앞에 붙인다.
// 지시문에서 이 형식을 설명한다.
std::string build_user_message(const std::string& source, std::span<const std::string> chat,
                               const std::string& draft = {});

}  // namespace overlay_trans
