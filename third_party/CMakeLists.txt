foreach(submodule_file miniaudio/miniaudio.c whisper.cpp/CMakeLists.txt)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${submodule_file}")
        message(FATAL_ERROR "서브모듈이 없습니다. git submodule update --init --recursive 를 실행하세요.")
    endif()
endforeach()

# miniaudio는 단일 소스 라이브러리라 필요한 기능만 골라 직접 빌드한다.
add_library(miniaudio STATIC miniaudio/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC miniaudio)
target_compile_definitions(miniaudio PUBLIC
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS
    MA_ENABLE_WASAPI
    MA_NO_DECODING
    MA_NO_GENERATION
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
)

# whisper.cpp는 실행 파일에 정적으로 링크한다. (기본값은 DLL 빌드)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(whisper.cpp EXCLUDE_FROM_ALL)
