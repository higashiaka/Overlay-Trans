#pragma once

#include <string_view>

namespace overlay_trans {

// UTF-8 문자열에 일본어·중국어 글자(가나, 한자, 일본어 문장 부호)가 하나라도 있으면 true.
bool contains_kana_or_han(std::string_view text);

}  // namespace overlay_trans
