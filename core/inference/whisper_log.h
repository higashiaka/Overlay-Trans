#pragma once

namespace overlay_trans {

// whisper.cpp는 호출마다 INFO 로그를 남긴다. 경고와 오류만 stderr로 출력하도록 바꾼다.
void quiet_whisper_log();

// 호출한 스레드에서 나오는 whisper.cpp 로그를 모두 끄거나 다시 켠다.
void mute_whisper_log_on_this_thread(bool muted);

}  // namespace overlay_trans
