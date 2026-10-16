#include "translation_prompt.h"

namespace overlay_trans {

namespace {

// 번역 형식과 말투를 잡아 주는 예시. 방송에서 자주 틀리던 구어 표현을 골랐다.
// (やばい를 "위험해"로, 끝이 올라가는 〜くない?를 부정으로, いくぞ를 "힘내"로 옮기는 일이 있었다.)
// 초안은 기계 번역이 흔히 내는 직역투와 오역을 담아, 초안을 그대로 옮기지 않는 모습을 보여 준다.
constexpr TranslationExample KOREAN_EXAMPLES[] = {
    {"え、なんで？どうしたの？", "어, 왜? 무슨 일이야?", "어, 왜? 무슨 일이야?"},
    {"今日はちょっと早めに終わるかも", "오늘은 조금 일찍 끝날지도 몰라", "오늘은 좀 일찍 끝날지도 몰라."},
    {"やばい、これ面白くない？", "위험해, 이거 재미없어?", "헐, 이거 재밌지 않아?"},
    {"よし、いくぞー！", "좋아, 가겠다!", "좋아, 간다!"},
    {"あと何だっけ", "그리고 뭐였지", "또 뭐였더라"},
};

}  // namespace

std::string build_system_prompt(const TranslatorConfig& config, const std::string& stream_info) {
    std::string prompt = "You are a subtitle translator for a live stream. ";
    prompt += "Each user message is one line of speech-recognition text. ";
    prompt += "Reply with only its natural, colloquial " + config.target_language + " translation. ";
    prompt += "Stay faithful to the original: do not add, explain, or answer anything. ";
    prompt += "Earlier lines are context for understanding the current one. ";
    // 채팅 맥락이 붙은 경우의 형식. build_user_message에서 만드는 형식과 맞아야 한다.
    prompt += "A message may begin with a [Chat] section listing recent viewer chat messages that the streamer ";
    prompt += "may be reacting to. It is context only: never translate or answer it. ";
    prompt += "Translate only the text after [Streamer]. ";
    if (config.reference_drafts) {
        // 초안을 그대로 옮기면 기계 번역의 끊긴 문장과 직역투가 남는다. 뜻만 참고하게 한다.
        prompt += "A message may also include a [Draft] section with a machine translation of the line. ";
        prompt += "Use it only to understand word meanings: it may be wrong, stiff, or polite. ";
        prompt += "Write the reply in your own natural casual words. ";
        // 완결된 문장을 요구하면 한두 낱말짜리 조각에 말을 지어 붙였다. ("今日はね まず" -> "우선 말해봐")
        prompt += "If the line is an unfinished fragment, translate it as a fragment: ";
        prompt += "never add words to complete it, and never add anything that is not in the line. ";
        // 덧붙이지 말라고만 하면 긴 문장의 뒷부분을 잘라 버렸다.
        prompt += "Translate every part of the line, including the end of long lines: do not drop anything. ";
    }
    // 방송 용어는 외래어가 많아서, 뜻을 추측해 옮기면 틀리기 쉽다. 애매하면 소리 나는 대로 적게 한다.
    prompt += "Katakana words are usually loanwords, game terms, or names: unless you are sure of the meaning, ";
    prompt += "transliterate them by sound instead of guessing a translation. ";
    prompt += "Transliterate people's names by sound as well. ";
    prompt += "A question ending in くない? with rising tone asks for agreement (isn't it ~?), not a negative.";

    if (config.target_language == "Korean") {
        prompt += " Examples: スパイク -> 스파이크, エイム -> 에임, ボックス -> 박스, ビンゴ -> 빙고, ミントさん -> 민트 님.";
        prompt += " Write the reply in Hangul only: no Chinese characters and no Japanese kana.";
    }

    if (!stream_info.empty()) {
        prompt += "\n\nAbout this stream: " + stream_info;
    }
    if (!config.glossary.empty()) {
        prompt += "\n\nGlossary (always use these translations):\n" + config.glossary;
    }
    return prompt;
}

std::span<const TranslationExample> translation_examples(const std::string& target_language) {
    if (target_language == "Korean") {
        return KOREAN_EXAMPLES;
    }
    return {};
}

std::string build_user_message(const std::string& source, std::span<const std::string> chat,
                               const std::string& draft) {
    if (chat.empty() && draft.empty()) {
        return source;
    }
    std::string message;
    if (!chat.empty()) {
        message += "[Chat]\n";
        for (const std::string& line : chat) {
            message += line + "\n";
        }
    }
    if (!draft.empty()) {
        message += "[Draft]\n" + draft + "\n";
    }
    message += "[Streamer]\n" + source;
    return message;
}

}  // namespace overlay_trans
