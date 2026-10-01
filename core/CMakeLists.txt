add_library(overlay_core STATIC
    audio/ring_buffer.cpp
)

target_include_directories(overlay_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
