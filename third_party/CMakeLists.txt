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

# 로컬 통신 서버에 쓰는 cpp-httplib과 JSON 라이브러리. llama.cpp에 들어 있는 사본을 그대로 사용한다.
add_library(overlay_httplib STATIC llama.cpp/vendor/cpp-httplib/httplib.cpp)
target_include_directories(overlay_httplib SYSTEM PUBLIC llama.cpp/vendor/cpp-httplib llama.cpp/vendor)
target_compile_features(overlay_httplib PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(overlay_httplib PRIVATE /utf-8)
endif()
if(WIN32)
    target_link_libraries(overlay_httplib PUBLIC ws2_32)
endif()

# 화자 구분에 쓰는 sherpa-onnx (화자 임베딩 추출). 소스 빌드가 무거워서 공식 배포 바이너리를 받아 쓴다.
# C API DLL과 ONNX Runtime DLL을 실행 파일 옆에 함께 두어야 한다. (OVERLAY_SHERPA_ONNX_DLLS)
include(FetchContent)
set(SHERPA_ONNX_VERSION 1.13.8)
FetchContent_Declare(sherpa_onnx
    URL https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_ONNX_VERSION}/sherpa-onnx-v${SHERPA_ONNX_VERSION}-win-x64-shared-MD-Release-no-tts.tar.bz2
    URL_HASH SHA256=876e6b89b8cf84a3a1b375a397507f2cfe9c227c2a945411a11a668475fcb5d3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sherpa_onnx)

add_library(overlay_sherpa_onnx SHARED IMPORTED GLOBAL)
set_target_properties(overlay_sherpa_onnx PROPERTIES
    IMPORTED_LOCATION ${sherpa_onnx_SOURCE_DIR}/lib/sherpa-onnx-c-api.dll
    IMPORTED_IMPLIB ${sherpa_onnx_SOURCE_DIR}/lib/sherpa-onnx-c-api.lib
    INTERFACE_INCLUDE_DIRECTORIES ${sherpa_onnx_SOURCE_DIR}/include
)
set(OVERLAY_SHERPA_ONNX_DLLS
    ${sherpa_onnx_SOURCE_DIR}/lib/sherpa-onnx-c-api.dll
    ${sherpa_onnx_SOURCE_DIR}/lib/onnxruntime.dll
    ${sherpa_onnx_SOURCE_DIR}/lib/onnxruntime_providers_shared.dll
    CACHE INTERNAL "")
