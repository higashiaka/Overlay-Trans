add_library(overlay_core STATIC
    audio/ring_buffer.cpp
    audio/vad_segmenter.cpp
    audio/wav_writer.cpp
    inference/recognition_worker.cpp
    inference/voice_matcher.cpp
    inference/speech_recognizer.cpp
    inference/translator.cpp
    inference/whisper_log.cpp
    server/pairing_token.cpp
    server/subtitle_server.cpp
    text/katakana.cpp
    text/script.cpp
)

target_include_directories(overlay_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(overlay_core PRIVATE whisper llama overlay_httplib overlay_sherpa_onnx)
