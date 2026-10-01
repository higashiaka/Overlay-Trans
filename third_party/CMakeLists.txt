foreach(submodule_file miniaudio/miniaudio.c whisper.cpp/CMakeLists.txt llama.cpp/CMakeLists.txt)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${submodule_file}")
        message(FATAL_ERROR "서브모듈이 없습니다. git submodule update --init --recursive 를 실행하세요.")
    endif()
endforeach()

# miniaudio는 단일 소스 라이브러리라 필요한 기능만 골라 직접 빌드한다.
# llama.cpp가 miniaudio라는 타깃을 따로 정의하므로 이름을 구분한다.
add_library(overlay_miniaudio STATIC miniaudio/miniaudio.c)
target_include_directories(overlay_miniaudio SYSTEM PUBLIC miniaudio)
target_compile_definitions(overlay_miniaudio PUBLIC
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS
    MA_ENABLE_WASAPI
    MA_NO_DECODING
    MA_NO_GENERATION
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
)

# llama.cpp와 whisper.cpp는 실행 파일에 정적으로 링크한다. (기본값은 DLL 빌드)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# 두 라이브러리는 각자 ggml을 내장하고 있어 그대로 함께 링크하면 충돌한다.
# llama.cpp를 먼저 추가하면 whisper.cpp가 이미 정의된 ggml 타깃을 그대로 사용한다.
#
# Vulkan 셰이더 빌드는 폴더가 깊어 Windows 경로 길이 제한(260자)에 걸린다.
# 빌드 폴더 이름을 한 글자(l, w)로 두어 경로를 줄인다.
add_subdirectory(llama.cpp ${CMAKE_BINARY_DIR}/l EXCLUDE_FROM_ALL)
add_subdirectory(whisper.cpp ${CMAKE_BINARY_DIR}/w EXCLUDE_FROM_ALL)
