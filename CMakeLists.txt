cmake_minimum_required(VERSION 3.21)
project(OverlayTrans LANGUAGES C CXX)

add_subdirectory(third_party)

# 아래 설정은 외부 라이브러리에 적용되지 않도록 third_party 뒤에 둔다.
# (llama.cpp는 C++20으로 빌드하면 컴파일되지 않는다.)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
endif()

add_subdirectory(core)

if(WIN32)
    add_subdirectory(windows)
endif()
