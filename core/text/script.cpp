#include "script.h"

#include <cstddef>

namespace overlay_trans {

namespace {

bool is_kana_or_han(char32_t code) {
    return (code >= 0x3001 && code <= 0x303F) ||  // 일본어·중국어 문장 부호
           (code >= 0x3040 && code <= 0x30FF) ||  // 히라가나, 가타카나
           (code >= 0x3400 && code <= 0x4DBF) ||  // 한자 확장 A
           (code >= 0x4E00 && code <= 0x9FFF) ||  // 한자
           (code >= 0xFF66 && code <= 0xFF9F);    // 반각 가타카나
}

}  // namespace

bool contains_kana_or_han(std::string_view text) {
    // 찾는 글자는 모두 UTF-8에서 3바이트로 표현된다.
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if ((lead & 0xF0) != 0xE0) {
            continue;
        }
        const char32_t code = (static_cast<char32_t>(lead & 0x0F) << 12) |
                              (static_cast<char32_t>(static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                              static_cast<char32_t>(static_cast<unsigned char>(text[i + 2]) & 0x3F);
        if (is_kana_or_han(code)) {
            return true;
        }
    }
    return false;
}

}  // namespace overlay_trans
