cmake_minimum_required(VERSION 3.21)
project(OverlayTrans LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party)

if(WIN32)
    add_subdirectory(windows)
endif()
