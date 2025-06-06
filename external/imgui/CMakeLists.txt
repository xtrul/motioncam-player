cmake_minimum_required(VERSION 3.15)
project(imgui LANGUAGES CXX)

# Collect all core ImGui sources
file(GLOB IMGUI_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# Collect the GLFW+OpenGL3 backend sources
set(IMGUI_BACKENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC
    ${IMGUI_SRC}
    ${IMGUI_BACKENDS}
)

# Public include dirs so users only need to do `target_link_libraries(imgui)` 
target_include_directories(imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# Link to GLFW (and OpenGL if needed)
find_package(glfw3 REQUIRED)        # you already have this
target_link_libraries(imgui
    PUBLIC
        glfw
)

# On some platforms you may need to link OpenGL
find_package(OpenGL)
if(OpenGL_FOUND)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

# Optional: set C++ standard (should match your main project)
target_compile_features(imgui PUBLIC cxx_std_17)
