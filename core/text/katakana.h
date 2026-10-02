#pragma once

#include <string>
#include <string_view>

namespace overlay_trans {

// UTF-8 문자열에 들어 있는 가타카나를 소리 나는 대로 한글로 바꾼다. 다른 글자는 그대로 둔다.
// 예: "ゾロ킷" -> "조로킷", "ミントさん" -> "민토さん"
// 번역 모델이 가타카나를 옮기지 않고 남긴 경우에 쓰는 보정용이다.
std::string katakana_to_hangul(std::string_view text);

}  // namespace overlay_trans
