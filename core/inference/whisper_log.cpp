#include "whisper_log.h"

#include <whisper.h>

#include <cstdio>

namespace overlay_trans {

void quiet_whisper_log() {
    whisper_log_set(
        [](ggml_log_level level, const char* text, void*) {
            if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) {
                std::fputs(text, stderr);
            }
        },
        nullptr);
}

}  // namespace overlay_trans
