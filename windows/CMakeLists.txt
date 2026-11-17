add_executable(OverlayTransWin
    main.cpp
    translation_settings.cpp
    inference/cli_translator.cpp
    capture/audio_file_reader.cpp
    capture/loopback_capture.cpp
    net/winhttp_client.cpp
    tray/tray_icon.cpp
)

# 계정 연결 창은 개발 빌드에서도 실행 파일 옆의 스크립트를 사용한다.
add_custom_command(TARGET OverlayTransWin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/installer/cli-setup.ps1 $<TARGET_FILE_DIR:OverlayTransWin>
)

target_include_directories(OverlayTransWin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(OverlayTransWin PRIVATE
    OVERLAY_TRANS_VERSION="${PROJECT_VERSION}"
    OVERLAY_TRANS_VERSION_W=L"${PROJECT_VERSION}"
)
target_link_libraries(OverlayTransWin PRIVATE
    overlay_core
    overlay_httplib
    overlay_miniaudio
    # Media Foundation (오디오 파일 디코딩)
    mfplat
    mfreadwrite
    mfuuid
    ole32
    crypt32
    # 로컬 앱 데이터 폴더 조회, 알림 영역 아이콘
    shell32
    user32
    # 번역 API 호출 (HTTPS)
    winhttp
)

# sherpa-onnx와 ONNX Runtime DLL을 실행 파일 옆에 복사한다. (화자 구분)
add_custom_command(TARGET OverlayTransWin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OVERLAY_SHERPA_ONNX_DLLS} $<TARGET_FILE_DIR:OverlayTransWin>
)

# OpenCL 로더를 실행 파일 옆에 복사한다. (OpenCL 프리셋으로 빌드한 경우)
if(OVERLAY_OPENCL_DLL)
    add_custom_command(TARGET OverlayTransWin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OVERLAY_OPENCL_DLL} $<TARGET_FILE_DIR:OverlayTransWin>
    )
endif()

option(OVERLAY_BUILD_CLI_TESTS "Build CLI translation integration tests" OFF)
if(OVERLAY_BUILD_CLI_TESTS)
    enable_testing()
    add_executable(overlay_cli_test_helper tests/cli_test_helper.cpp)
    target_link_libraries(overlay_cli_test_helper PRIVATE overlay_httplib)
    add_executable(overlay_cli_tests tests/cli_tests.cpp inference/cli_translator.cpp translation_settings.cpp net/winhttp_client.cpp)
    target_include_directories(overlay_cli_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(overlay_cli_tests PRIVATE overlay_core overlay_httplib ole32 crypt32 shell32 winhttp)
    add_dependencies(overlay_cli_tests overlay_cli_test_helper)
    add_test(NAME overlay_cli_tests COMMAND overlay_cli_tests $<TARGET_FILE:overlay_cli_test_helper>)
endif()
