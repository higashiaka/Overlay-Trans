add_executable(OverlayTransWin
    main.cpp
    capture/audio_file_reader.cpp
    capture/loopback_capture.cpp
)

target_include_directories(OverlayTransWin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(OverlayTransWin PRIVATE overlay_core overlay_miniaudio)
