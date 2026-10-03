#pragma once

#include <string_view>

namespace overlay_trans {

// UTF-8 문자열에 한국어 번역문에 쓰이지 않는 글자가 하나라도 있으면 true.
// 한글, 숫자, 문장 부호, 기호(이모지 포함)는 허용하고, 가나·한자·키릴 문자 같은 다른 나라 글자는 허용하지 않는다.
// 영문자(A-Z, a-z)는 allow_latin이 true일 때만 허용한다.
bool contains_non_korean_script(std::string_view text, bool allow_latin);

// UTF-8 문자열에 영문자(A-Z, a-z)가 하나라도 있으면 true.
bool contains_latin_letter(std::string_view text);

// 일본어 감탄사나 웃음소리(ん, えー, はっはっは, ふふ, 笑 등)로만 이루어져 있으면 true. 빈 문자열은 false.
bool is_japanese_interjection(std::string_view text);

}  // namespace overlay_trans
