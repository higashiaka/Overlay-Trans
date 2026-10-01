add_library(overlay_core STATIC
    audio/ring_buffer.cpp
    audio/vad_segmenter.cpp
    audio/wav_writer.cpp
    inference/speech_recognizer.cpp
    inference/translator.cpp
    inference/whisper_log.cpp
)

target_include_directories(overlay_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(overlay_core PRIVATE whisper llama)
