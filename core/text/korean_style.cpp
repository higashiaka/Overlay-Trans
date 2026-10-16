#include "korean_style.h"

namespace overlay_trans {

namespace {

struct Ending {
    std::string_view polite;
    std::string_view casual;
};

// 앞에서부터 처음 맞는 것 하나만 바꾸므로 긴 어미를 먼저 둔다.
constexpr Ending ENDINGS[] = {
    {"감사합니다", "고마워"},
    {"고맙습니다", "고마워"},
    {"했습니다", "했어"},
    {"였습니다", "였어"},
    {"었습니다", "었어"},
    {"았습니다", "았어"},
    {"합시다", "하자"},
    {"갑시다", "가자"},
    {"봅시다", "보자"},
    {"합니다", "해"},
    // "~ㅂ니다"는 받침을 풀어 모음을 합쳐야 해서 자주 나오는 것만 둔다.
    {"넘칩니다", "넘쳐"},
    {"됩니다", "돼"},
    {"갑니다", "가"},
    {"옵니다", "와"},
    {"봅니다", "봐"},
    {"줍니다", "줘"},
    {"납니다", "나"},
    {"씁니다", "써"},
    {"입니다", "이야"},
    {"습니다", "어"},
    {"습니까", "어"},
    {"안녕하세요", "안녕"},
    {"안녕히 주무세요", "잘 자"},
    {"안녕히 계세요", "잘 있어"},
    {"안녕히 가세요", "잘 가"},
    {"않으세요", "않아"},
    {"하세요", "해"},
    {"오세요", "와"},
    {"가세요", "가"},
    {"주세요", "줘"},
    {"보세요", "봐"},
    {"거예요", "거야"},
    {"이에요", "이야"},
    {"예요", "야"},
    {"에요", "야"},
    {"게요", "게"},
    {"지요", "지"},
    {"죠", "지"},
};

// 그 밖의 "~세요"는 "요"만 떼면 문장이 깨지므로 그대로 둔다. (앉으세요 -> 앉으세)
constexpr std::string_view SEYO = "세요";

// 낮춤말 1인칭. 조사가 붙은 형태만 바꾼다. "저"만으로는 "저기", "저 사람"의 "저"와 구분할 수 없다.
constexpr Ending PRONOUNS[] = {
    {"저는", "나는"}, {"저도", "나도"}, {"저를", "나를"}, {"저한테", "나한테"}, {"저한테는", "나한테는"},
    {"저는요", "나는"}, {"저에게", "나에게"},
    {"제가", "내가"}, {"제게", "나한테"}, {"저희", "우리"}, {"제", "내"},
};

// "요"로 끝나지만 어미가 아닌 낱말.
constexpr std::string_view NOT_ENDINGS[] = {"필요", "중요", "요요", "월요", "화요", "수요", "목요", "금요", "토요", "일요"};

constexpr std::string_view YO = "요";
constexpr std::string_view ELLIPSIS = "…";

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// 문장 끝을 나타내는 문장 부호가 text[i]에서 시작하면 그 길이를, 아니면 0을 반환한다.
size_t punctuation_length(std::string_view text, size_t i) {
    switch (text[i]) {
        case '.':
        case '?':
        case '!':
        case '~':
        case ',':
            return 1;
        default:
            return text.substr(i, ELLIPSIS.size()) == ELLIPSIS ? ELLIPSIS.size() : 0;
    }
}

// 문장 하나(문장 부호 앞까지)의 끝 어미를 바꾼다.
std::string casual_sentence(std::string_view sentence) {
    // 대답 "네"만 있는 문장은 "응"으로 바꾼다. (そう -> "네, ...")
    const size_t first = sentence.find_first_not_of(' ');
    if (first != std::string_view::npos && (sentence.substr(first) == "네" || sentence.substr(first) == "예")) {
        return std::string(sentence.substr(0, first)) + "응";
    }
    for (const Ending& ending : ENDINGS) {
        if (ends_with(sentence, ending.polite)) {
            return std::string(sentence.substr(0, sentence.size() - ending.polite.size())) +
                   std::string(ending.casual);
        }
    }
    if (ends_with(sentence, SEYO)) {
        return std::string(sentence);
    }
    // 그 밖의 해요체는 "요"만 떼면 반말이 된다. (같아요, 먹었어요, 건가요, 하네요 등)
    if (ends_with(sentence, YO) && sentence.size() > YO.size()) {
        for (std::string_view word : NOT_ENDINGS) {
            if (ends_with(sentence, word)) {
                return std::string(sentence);
            }
        }
        return std::string(sentence.substr(0, sentence.size() - YO.size()));
    }
    return std::string(sentence);
}

// 낱말이 끝나는 곳인지. 공백, 문장 부호, 문자열 끝이면 낱말이 끝난 것으로 본다.
bool is_word_end(std::string_view text, size_t i) {
    return i == text.size() || text[i] == ' ' || punctuation_length(text, i) > 0;
}

// 낱말 단위로 1인칭을 바꾼다. "제"는 띄어 쓴 경우("제 방")만 바꾼다.
std::string casual_pronouns(std::string_view text) {
    std::string result;
    size_t i = 0;
    while (i < text.size()) {
        const bool word_start = i == 0 || text[i - 1] == ' ' || punctuation_length(text, i - 1) > 0;
        bool replaced = false;
        if (word_start) {
            for (const Ending& pronoun : PRONOUNS) {
                const size_t end = i + pronoun.polite.size();
                if (end > text.size() || text.substr(i, pronoun.polite.size()) != pronoun.polite) {
                    continue;
                }
                const bool whole_word = pronoun.polite == "제" ? end < text.size() && text[end] == ' '
                                                               : is_word_end(text, end);
                if (whole_word) {
                    result += pronoun.casual;
                    i = end;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            result += text[i];
            ++i;
        }
    }
    return result;
}

}  // namespace

std::string to_casual_korean(std::string_view input) {
    const std::string pronouns_replaced = casual_pronouns(input);
    const std::string_view text = pronouns_replaced;
    std::string result;
    size_t sentence_start = 0;
    for (size_t i = 0; i <= text.size();) {
        const size_t length = i < text.size() ? punctuation_length(text, i) : 0;
        if (i < text.size() && length == 0) {
            ++i;
            continue;
        }
        // 문장 부호나 문자열 끝에 닿으면 그 앞까지를 문장 하나로 보고, 끝의 공백을 뺀 부분의 어미를 바꾼다.
        size_t sentence_end = i;
        while (sentence_end > sentence_start && text[sentence_end - 1] == ' ') {
            --sentence_end;
        }
        result += casual_sentence(text.substr(sentence_start, sentence_end - sentence_start));
        result.append(text.substr(sentence_end, i + length - sentence_end));
        if (i == text.size()) {
            break;
        }
        i += length;
        sentence_start = i;
    }
    return result;
}

}  // namespace overlay_trans
