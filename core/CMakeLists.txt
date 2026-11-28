add_library(overlay_core STATIC
    audio/ring_buffer.cpp
    audio/vad_segmenter.cpp
    audio/wav_writer.cpp
    inference/deepl_translator.cpp
    inference/drafted_translator.cpp
    inference/hardware_profile.cpp
    inference/recognition_worker.cpp
    inference/voice_matcher.cpp
    inference/speech_recognizer.cpp
    inference/translation_prompt.cpp
    inference/translation_worker.cpp
    inference/translator.cpp
    inference/whisper_log.cpp
    server/pairing_token.cpp
    server/subtitle_server.cpp
    text/katakana.cpp
    text/korean_style.cpp
    text/script.cpp
)

target_include_directories(overlay_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(overlay_core PRIVATE whisper llama overlay_httplib overlay_sherpa_onnx)

option(OVERLAY_BUILD_AUDIO_TESTS "Build browser audio transport tests" OFF)
if(OVERLAY_BUILD_AUDIO_TESTS)
    enable_testing()
    add_executable(overlay_browser_audio_tests tests/browser_audio_tests.cpp server/subtitle_server.cpp)
    target_include_directories(overlay_browser_audio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(overlay_browser_audio_tests PRIVATE overlay_httplib)
    add_test(NAME overlay_browser_audio_tests COMMAND overlay_browser_audio_tests)
endif()
