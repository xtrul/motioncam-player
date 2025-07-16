add_executable(motioncam_batcher
    motioncam_batcher.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Include decoder and third-party headers
target_include_directories(motioncam_batcher PRIVATE
    ${PROJECT_SOURCE_DIR}/motioncam-decoder/lib/include
    ${PROJECT_SOURCE_DIR}/motioncam-decoder/thirdparty
    ${SDL2_PATH}/include
    ${IMGUI_SOURCE_DIR}
    ${IMGUI_SOURCE_DIR}/backends
)

find_package(OpenGL REQUIRED)
find_package(PkgConfig)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_link_libraries(motioncam_batcher PRIVATE
    motioncam_decoder
    imgui
    OpenGL::GL
    ${SDL2_LIBRARIES}
)
target_include_directories(motioncam_batcher PRIVATE ${SDL2_INCLUDE_DIRS})
