cmake_minimum_required(VERSION 3.15)
project(imgui LANGUAGES CXX)

# Collect all core ImGui sources
file(GLOB IMGUI_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# Build core ImGui sources only. Backends are compiled by the parent project.
add_library(imgui STATIC
    ${IMGUI_SRC}
)

# Public include dirs so users only need to do `target_link_libraries(imgui)` 
target_include_directories(imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# The parent project links GLFW and any renderer backends as needed

# Optional: set C++ standard (should match your main project)
target_compile_features(imgui PUBLIC cxx_std_17)
