#include "script.h"

#include <cstddef>
#include <string_view>

namespace overlay_trans {

namespace {

bool is_latin_letter(char32_t code) {
    return (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
}

// 한국어 번역문에 나와도 되는 글자인지. (영문자는 따로 판단한다.)
bool is_allowed_in_korean(char32_t code) {
    return code < 0x80 ||                          // 숫자, 공백, 영문 문장 부호
           (code >= 0x00A0 && code <= 0x00BF) ||   // 기호 (°, ± 등)
           code == 0x00D7 || code == 0x00F7 ||     // ×, ÷
           (code >= 0x1100 && code <= 0x11FF) ||   // 한글 자모
           (code >= 0x2000 && code <= 0x2BFF) ||   // 문장 부호, 화살표, 수학 기호, 그 밖의 기호
           code == 0x3000 ||                       // 전각 공백
           (code >= 0x3130 && code <= 0x318F) ||   // 한글 호환 자모 (ㅋ, ㅠ 등)
           (code >= 0xAC00 && code <= 0xD7A3) ||   // 한글 음절
           (code >= 0xFE00 && code <= 0xFE0F) ||   // 이모지 모양 선택자
           (code >= 0x1F000 && code <= 0x1FAFF);   // 이모지
}

// text의 i번째 바이트에서 시작하는 글자를 읽고 i를 다음 글자로 옮긴다.
// 온전한 UTF-8 글자가 아니면 false를 반환하고 한 바이트만 건너뛴다.
bool read_code_point(std::string_view text, size_t& i, char32_t& code) {
    const auto lead = static_cast<unsigned char>(text[i]);
    size_t length = 0;
    if (lead < 0x80) {
        code = lead;
        ++i;
        return true;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        code = lead & 0x07;
    }

    if (length == 0 || i + length > text.size()) {
        ++i;
        return false;
    }
    for (size_t k = 1; k < length; ++k) {
        const auto next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) {
            ++i;
            return false;
        }
        code = (code << 6) | (next & 0x3F);
    }
    i += length;
    return true;
}

}  // namespace

bool contains_non_korean_script(std::string_view text, bool allow_latin) {
    for (size_t i = 0; i < text.size();) {
        char32_t code = 0;
        // 글자의 일부만 담긴 토큰은 판단할 수 없으므로 넘어간다.
        if (!read_code_point(text, i, code)) {
            continue;
        }
        if (!is_allowed_in_korean(code) || (!allow_latin && is_latin_letter(code))) {
            return true;
        }
    }
    return false;
}

bool is_japanese_interjection(std::string_view text) {
    // 감탄사와 웃음소리에 쓰이는 글자. 뜻이 있는 말에 자주 쓰이는 い, わ 등은 넣지 않는다.
    constexpr std::u32string_view INTERJECTION_LETTERS =
        U"んンーっッあアぁえエぇうウぅおオぉはハひヒふフへヘほホ笑wWｗ";
    constexpr std::u32string_view IGNORED = U" 　、。,.!?！？…~〜～・";

    bool has_letter = false;
    for (size_t i = 0; i < text.size();) {
        char32_t code = 0;
        if (!read_code_point(text, i, code)) {
            return false;
        }
        if (IGNORED.find(code) != std::u32string_view::npos) {
            continue;
        }
        if (INTERJECTION_LETTERS.find(code) == std::u32string_view::npos) {
            return false;
        }
        has_letter = true;
    }
    return has_letter;
}

bool contains_latin_letter(std::string_view text) {
    for (const char c : text) {
        if (is_latin_letter(static_cast<unsigned char>(c))) {
            return true;
        }
    }
    return false;
}

}  // namespace overlay_trans
