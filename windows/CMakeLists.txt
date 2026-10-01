add_executable(OverlayTransWin
    main.cpp
    capture/loopback_capture.cpp
)

target_include_directories(OverlayTransWin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(OverlayTransWin PRIVATE miniaudio)

if(MSVC)
    target_compile_options(OverlayTransWin PRIVATE /W4 /utf-8)
endif()
