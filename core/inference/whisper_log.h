#pragma once

namespace overlay_trans {

// whisper.cpp는 호출마다 INFO 로그를 남긴다. 경고와 오류만 stderr로 출력하도록 바꾼다.
void quiet_whisper_log();

}  // namespace overlay_trans
