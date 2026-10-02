#include "whisper_log.h"

#include <whisper.h>

#include <cstdio>

namespace overlay_trans {

namespace {

thread_local bool muted_on_this_thread = false;

}  // namespace

void quiet_whisper_log() {
    whisper_log_set(
        [](ggml_log_level level, const char* text, void*) {
            if (muted_on_this_thread) {
                return;
            }
            if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) {
                std::fputs(text, stderr);
            }
        },
        nullptr);
}

void mute_whisper_log_on_this_thread(bool muted) {
    muted_on_this_thread = muted;
}

}  // namespace overlay_trans
