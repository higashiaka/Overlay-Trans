add_executable(OverlayTransWin
    main.cpp
    capture/audio_file_reader.cpp
    capture/loopback_capture.cpp
)

target_include_directories(OverlayTransWin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(OverlayTransWin PRIVATE OVERLAY_TRANS_VERSION="${PROJECT_VERSION}")
target_link_libraries(OverlayTransWin PRIVATE
    overlay_core
    overlay_miniaudio
    # Media Foundation (오디오 파일 디코딩)
    mfplat
    mfreadwrite
    mfuuid
    ole32
    # 로컬 앱 데이터 폴더 조회
    shell32
)
