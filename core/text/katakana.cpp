#include "katakana.h"

#include <array>
#include <utility>

namespace overlay_trans {

namespace {

constexpr char32_t KATAKANA_FIRST = U'ァ';
constexpr char32_t KATAKANA_LAST = U'ヺ';
constexpr char32_t LONG_VOWEL_MARK = U'ー';
constexpr char32_t SYLLABIC_N = U'ン';
constexpr char32_t SMALL_TSU = U'ッ';

constexpr char32_t HANGUL_FIRST = U'가';
constexpr char32_t HANGUL_LAST = U'힣';
constexpr char32_t FINALS_PER_SYLLABLE = 28;
constexpr char32_t FINAL_NIEUN = 4;   // ㄴ 받침
constexpr char32_t FINAL_SIOT = 19;   // ㅅ 받침

using Entry = std::pair<std::u32string_view, std::u32string_view>;

// 두 글자 조합을 먼저 찾아야 하므로(예: キャ) 두 글자 항목을 앞에 둔다.
constexpr std::array KANA_TABLE = {
    Entry{U"キャ", U"캬"}, Entry{U"キュ", U"큐"}, Entry{U"キョ", U"쿄"}, Entry{U"シャ", U"샤"}, Entry{U"シュ", U"슈"},
    Entry{U"ショ", U"쇼"}, Entry{U"チャ", U"차"}, Entry{U"チュ", U"추"}, Entry{U"チョ", U"초"}, Entry{U"ニャ", U"냐"},
    Entry{U"ニュ", U"뉴"}, Entry{U"ニョ", U"뇨"}, Entry{U"ヒャ", U"햐"}, Entry{U"ヒュ", U"휴"}, Entry{U"ヒョ", U"효"},
    Entry{U"ミャ", U"먀"}, Entry{U"ミュ", U"뮤"}, Entry{U"ミョ", U"묘"}, Entry{U"リャ", U"랴"}, Entry{U"リュ", U"류"},
    Entry{U"リョ", U"료"}, Entry{U"ギャ", U"갸"}, Entry{U"ギュ", U"규"}, Entry{U"ギョ", U"교"}, Entry{U"ジャ", U"자"},
    Entry{U"ジュ", U"주"}, Entry{U"ジョ", U"조"}, Entry{U"ビャ", U"뱌"}, Entry{U"ビュ", U"뷰"}, Entry{U"ビョ", U"뵤"},
    Entry{U"ピャ", U"퍄"}, Entry{U"ピュ", U"퓨"}, Entry{U"ピョ", U"표"},
    // 외래어 표기에 쓰는 조합
    Entry{U"ファ", U"파"}, Entry{U"フィ", U"피"}, Entry{U"フェ", U"페"}, Entry{U"フォ", U"포"}, Entry{U"ティ", U"티"},
    Entry{U"ディ", U"디"}, Entry{U"トゥ", U"투"}, Entry{U"ドゥ", U"두"}, Entry{U"テュ", U"튜"}, Entry{U"デュ", U"듀"},
    Entry{U"ウィ", U"위"}, Entry{U"ウェ", U"웨"}, Entry{U"ウォ", U"워"}, Entry{U"シェ", U"셰"}, Entry{U"ジェ", U"제"},
    Entry{U"チェ", U"체"}, Entry{U"ヴァ", U"바"}, Entry{U"ヴィ", U"비"}, Entry{U"ヴェ", U"베"}, Entry{U"ヴォ", U"보"},

    Entry{U"ア", U"아"}, Entry{U"イ", U"이"}, Entry{U"ウ", U"우"}, Entry{U"エ", U"에"}, Entry{U"オ", U"오"},
    Entry{U"カ", U"카"}, Entry{U"キ", U"키"}, Entry{U"ク", U"쿠"}, Entry{U"ケ", U"케"}, Entry{U"コ", U"코"},
    Entry{U"サ", U"사"}, Entry{U"シ", U"시"}, Entry{U"ス", U"스"}, Entry{U"セ", U"세"}, Entry{U"ソ", U"소"},
    Entry{U"タ", U"타"}, Entry{U"チ", U"치"}, Entry{U"ツ", U"츠"}, Entry{U"テ", U"테"}, Entry{U"ト", U"토"},
    Entry{U"ナ", U"나"}, Entry{U"ニ", U"니"}, Entry{U"ヌ", U"누"}, Entry{U"ネ", U"네"}, Entry{U"ノ", U"노"},
    Entry{U"ハ", U"하"}, Entry{U"ヒ", U"히"}, Entry{U"フ", U"후"}, Entry{U"ヘ", U"헤"}, Entry{U"ホ", U"호"},
    Entry{U"マ", U"마"}, Entry{U"ミ", U"미"}, Entry{U"ム", U"무"}, Entry{U"メ", U"메"}, Entry{U"モ", U"모"},
    Entry{U"ヤ", U"야"}, Entry{U"ユ", U"유"}, Entry{U"ヨ", U"요"},
    Entry{U"ラ", U"라"}, Entry{U"リ", U"리"}, Entry{U"ル", U"루"}, Entry{U"レ", U"레"}, Entry{U"ロ", U"로"},
    Entry{U"ワ", U"와"}, Entry{U"ヲ", U"오"},
    Entry{U"ガ", U"가"}, Entry{U"ギ", U"기"}, Entry{U"グ", U"구"}, Entry{U"ゲ", U"게"}, Entry{U"ゴ", U"고"},
    Entry{U"ザ", U"자"}, Entry{U"ジ", U"지"}, Entry{U"ズ", U"즈"}, Entry{U"ゼ", U"제"}, Entry{U"ゾ", U"조"},
    Entry{U"ダ", U"다"}, Entry{U"ヂ", U"지"}, Entry{U"ヅ", U"즈"}, Entry{U"デ", U"데"}, Entry{U"ド", U"도"},
    Entry{U"バ", U"바"}, Entry{U"ビ", U"비"}, Entry{U"ブ", U"부"}, Entry{U"ベ", U"베"}, Entry{U"ボ", U"보"},
    Entry{U"パ", U"파"}, Entry{U"ピ", U"피"}, Entry{U"プ", U"푸"}, Entry{U"ペ", U"페"}, Entry{U"ポ", U"포"},
    Entry{U"ヴ", U"부"},
    // 앞 글자와 조합되지 못하고 남은 작은 글자
    Entry{U"ァ", U"아"}, Entry{U"ィ", U"이"}, Entry{U"ゥ", U"우"}, Entry{U"ェ", U"에"}, Entry{U"ォ", U"오"},
    Entry{U"ャ", U"야"}, Entry{U"ュ", U"유"}, Entry{U"ョ", U"요"},
};

bool is_katakana(char32_t c) {
    return (c >= KATAKANA_FIRST && c <= KATAKANA_LAST) || c == LONG_VOWEL_MARK;
}

std::u32string decode_utf8(std::string_view text) {
    std::u32string result;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        size_t length = 1;
        char32_t code = lead;
        if (lead >= 0xF0) {
            length = 4;
            code = lead & 0x07;
        } else if (lead >= 0xE0) {
            length = 3;
            code = lead & 0x0F;
        } else if (lead >= 0xC0) {
            length = 2;
            code = lead & 0x1F;
        }
        // 잘린 글자는 첫 바이트만 그대로 넘긴다.
        if (i + length > text.size()) {
            length = 1;
            code = lead;
        }
        for (size_t k = 1; k < length; ++k) {
            code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        result += code;
        i += length;
    }
    return result;
}

void append_utf8(std::string& out, char32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// 바로 앞 글자가 받침 없는 한글이면 받침을 붙이고 true를 반환한다.
bool add_final_consonant(std::u32string& text, char32_t final) {
    if (text.empty()) {
        return false;
    }
    char32_t& last = text.back();
    if (last < HANGUL_FIRST || last > HANGUL_LAST || (last - HANGUL_FIRST) % FINALS_PER_SYLLABLE != 0) {
        return false;
    }
    last += final;
    return true;
}

}  // namespace

std::string katakana_to_hangul(std::string_view text) {
    const std::u32string input = decode_utf8(text);
    std::u32string output;

    for (size_t i = 0; i < input.size();) {
        const char32_t c = input[i];
        if (!is_katakana(c)) {
            output += c;
            ++i;
            continue;
        }

        // 장음 부호는 한글로 적지 않는다.
        if (c == LONG_VOWEL_MARK) {
            ++i;
            continue;
        }
        if (c == SYLLABIC_N) {
            if (!add_final_consonant(output, FINAL_NIEUN)) {
                output += U'응';
            }
            ++i;
            continue;
        }
        if (c == SMALL_TSU) {
            add_final_consonant(output, FINAL_SIOT);
            ++i;
            continue;
        }

        bool converted = false;
        for (const auto& [kana, hangul] : KANA_TABLE) {
            if (input.compare(i, kana.size(), kana) == 0) {
                output += hangul;
                i += kana.size();
                converted = true;
                break;
            }
        }
        if (!converted) {
            output += c;
            ++i;
        }
    }

    std::string result;
    for (const char32_t code : output) {
        append_utf8(result, code);
    }
    return result;
}

}  // namespace overlay_trans
