add_executable(OverlayTransWin
    main.cpp
    capture/audio_file_reader.cpp
    capture/loopback_capture.cpp
    tray/tray_icon.cpp
)

target_include_directories(OverlayTransWin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(OverlayTransWin PRIVATE
    OVERLAY_TRANS_VERSION="${PROJECT_VERSION}"
    OVERLAY_TRANS_VERSION_W=L"${PROJECT_VERSION}"
)
target_link_libraries(OverlayTransWin PRIVATE
    overlay_core
    overlay_miniaudio
    # Media Foundation (오디오 파일 디코딩)
    mfplat
    mfreadwrite
    mfuuid
    ole32
    # 로컬 앱 데이터 폴더 조회, 알림 영역 아이콘
    shell32
    user32
)

# sherpa-onnx와 ONNX Runtime DLL을 실행 파일 옆에 복사한다. (화자 구분)
add_custom_command(TARGET OverlayTransWin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OVERLAY_SHERPA_ONNX_DLLS} $<TARGET_FILE_DIR:OverlayTransWin>
)
