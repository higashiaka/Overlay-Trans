#pragma once

#include <string>
#include <string_view>

namespace overlay_trans {

// UTF-8 한국어 문장의 끝에 붙은 존댓말 어미를 반말로 바꾼다. 문장 끝이 아닌 곳과 낱말은 건드리지 않는다.
// 예: "밥 먹었어요" -> "밥 먹었어", "웹 판매를 합니다" -> "웹 판매를 해", "그런 거겠죠?" -> "그런 거겠지?"
// 방송인이 반말로 말해도 기계 번역은 존댓말로 옮기는 경우가 많아서 쓰는 보정용이다.
std::string to_casual_korean(std::string_view text);

}  // namespace overlay_trans
