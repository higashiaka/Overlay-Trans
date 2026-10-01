cmake_minimum_required(VERSION 3.21)
project(OverlayTrans LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party)

# 외부 라이브러리에는 적용되지 않도록 third_party 뒤에서 설정한다.
if(MSVC)
    add_compile_options(/W4 /utf-8)
endif()

add_subdirectory(core)

if(WIN32)
    add_subdirectory(windows)
endif()
