cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0111 NEW)
project(MotionCam_Player VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

message(STATUS "---------------------------------------------------------------------")
message(STATUS "Configuring ${PROJECT_NAME} (Version ${PROJECT_VERSION}) for Vulkan")
message(STATUS "  Source Directory: ${PROJECT_SOURCE_DIR}")
message(STATUS "  Binary Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "---------------------------------------------------------------------")

set(APP_ROOT_DIR        "${PROJECT_SOURCE_DIR}")
set(APP_EXTERNAL_DIR    "${APP_ROOT_DIR}/external")
set(APP_DEPS_DIR       "${APP_ROOT_DIR}/deps")
set(APP_ASSETS_DIR      "${APP_ROOT_DIR}/assets")
set(APP_SHADERS_SRC_DIR "${APP_ROOT_DIR}/shaders")
set(APP_CMAKE_DIR       "${APP_ROOT_DIR}/cmake")
set(APP_DEPS_DIR       "${APP_ROOT_DIR}/deps")

if(APPLE)
    option(MC_BUILD_UNIVERSAL "Build universal macOS binary (arm64 + x86_64)" OFF)
    if(MC_BUILD_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures" FORCE)
    endif()
    set(MC_MAC_DEPS_ROOT "$ENV{FATPREFIX}" CACHE PATH "Prefix containing macOS dependencies (MoltenVK/Vulkan, SDL2, GLFW, etc.)")
    if(MC_MAC_DEPS_ROOT AND EXISTS "${MC_MAC_DEPS_ROOT}")
        list(PREPEND CMAKE_PREFIX_PATH "${MC_MAC_DEPS_ROOT}")
        list(PREPEND CMAKE_FRAMEWORK_PATH "${MC_MAC_DEPS_ROOT}")
        message(STATUS "macOS deps root: ${MC_MAC_DEPS_ROOT}")
    endif()
endif()

message(STATUS "Application Structure:")
message(STATUS "  Source files (.cpp):      ${APP_ROOT_DIR}/src (structured)")  
message(STATUS "  Header files (.h):        ${APP_ROOT_DIR}/include (structured)")
message(STATUS "  External libraries:       ${APP_EXTERNAL_DIR}")
message(STATUS "  External dependencies:    ${APP_DEPS_DIR}")
message(STATUS "  Asset files (fonts etc.): ${APP_ASSETS_DIR}")
message(STATUS "  Shader source files:      ${APP_SHADERS_SRC_DIR}")
message(STATUS "  Custom CMake modules:     ${APP_CMAKE_DIR}")

find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please ensure it is installed and discoverable.")
endif()
find_package(Threads REQUIRED)
message(STATUS "Found Vulkan SDK:")
message(STATUS "  Libraries:    ${Vulkan_LIBRARIES}")
message(STATUS "  Include Dirs: ${Vulkan_INCLUDE_DIRS}")

set(GLM_SOURCE_DIR "${APP_EXTERNAL_DIR}/glm")
if(NOT EXISTS "${GLM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "GLM CMakeLists.txt not found at ${GLM_SOURCE_DIR}.")
endif()
set(GLM_TEST_ENABLE OFF CACHE BOOL "Disable GLM tests" FORCE)
add_subdirectory(${GLM_SOURCE_DIR} ${CMAKE_BINARY_DIR}/glm_build EXCLUDE_FROM_ALL)
message(STATUS "Configured GLM from: ${GLM_SOURCE_DIR}")

set(VMA_INCLUDE_DIR "${APP_EXTERNAL_DIR}/vma")
if(NOT EXISTS "${VMA_INCLUDE_DIR}/vk_mem_alloc.h")
    message(FATAL_ERROR "vk_mem_alloc.h not found at ${VMA_INCLUDE_DIR}.")
endif()
message(STATUS "Found VMA header in: ${VMA_INCLUDE_DIR}")

set(PLATFORM_INCLUDE_DIRS "")
set(PLATFORM_LIBRARIES "")

if(WIN32)
    set(GLFW_PATH "${APP_EXTERNAL_DIR}/glfw" CACHE PATH "Path to GLFW pre-built libraries and headers")
    if(NOT EXISTS "${GLFW_PATH}/include/GLFW/glfw3.h")
        message(FATAL_ERROR "GLFW headers not found at ${GLFW_PATH}/include. Check GLFW_PATH.")
    endif()
    set(glfw3_DIR "${APP_CMAKE_DIR}" CACHE INTERNAL "Path to custom glfw3Config.cmake")
    message(STATUS "Using pre-built GLFW from: ${GLFW_PATH}")

    set(SDL2_PATH "${APP_EXTERNAL_DIR}/SDL2" CACHE PATH "Path to SDL2 development libraries")
    if(NOT EXISTS "${SDL2_PATH}/include/SDL.h")
        message(FATAL_ERROR "SDL2 headers not found at ${SDL2_PATH}/include. Check SDL2_PATH.")
    endif()
    message(STATUS "Using pre-built SDL2 from: ${SDL2_PATH}")

    list(APPEND PLATFORM_INCLUDE_DIRS "${GLFW_PATH}/include" "${SDL2_PATH}/include")
elseif(APPLE)
    find_package(glfw3 CONFIG REQUIRED)
    if(TARGET glfw)
        set(MC_GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw)
        set(MC_GLFW_TARGET glfw3::glfw)
    else()
        message(FATAL_ERROR "glfw target not found after find_package(glfw3).")
    endif()
    get_target_property(MC_GLFW_INCLUDE_DIRS ${MC_GLFW_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(MC_GLFW_INCLUDE_DIRS)
        list(APPEND PLATFORM_INCLUDE_DIRS ${MC_GLFW_INCLUDE_DIRS})
    endif()

    find_package(SDL2 CONFIG REQUIRED)
    if(NOT TARGET SDL2::SDL2)
        message(FATAL_ERROR "SDL2::SDL2 target not found; ensure SDL2 CONFIG package is available.")
    endif()
    if(DEFINED SDL2_INCLUDE_DIRS)
        list(APPEND PLATFORM_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
    else()
        get_target_property(MC_SDL2_INCLUDE_DIRS SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
        if(MC_SDL2_INCLUDE_DIRS)
            list(APPEND PLATFORM_INCLUDE_DIRS ${MC_SDL2_INCLUDE_DIRS})
        endif()
    endif()

    list(APPEND PLATFORM_LIBRARIES ${MC_GLFW_TARGET} SDL2::SDL2
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework Metal" "-framework CoreFoundation" "-framework AudioToolbox")
else()
    message(FATAL_ERROR "Unsupported platform: configure dependencies for this OS.")
endif()

if(WIN32)
    set(GLSLANG_VALIDATOR_EXECUTABLE_NAME "glslangValidator.exe")
else()
    set(GLSLANG_VALIDATOR_EXECUTABLE_NAME "glslangValidator")
endif()

if(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    set(GLSLANG_VALIDATOR_CMD "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
    message(STATUS "Using glslangValidator from Vulkan SDK: ${GLSLANG_VALIDATOR_CMD}")
else()
    find_program(GLSLANG_VALIDATOR_PROGRAM NAMES ${GLSLANG_VALIDATOR_EXECUTABLE_NAME})
    if(GLSLANG_VALIDATOR_PROGRAM)
        set(GLSLANG_VALIDATOR_CMD "${GLSLANG_VALIDATOR_PROGRAM}")
        message(STATUS "Found glslangValidator in PATH: ${GLSLANG_VALIDATOR_CMD}")
    else()
        message(FATAL_ERROR "glslangValidator not found. Ensure it's in PATH or Vulkan SDK is configured correctly.")
    endif()
endif()

set(SHADER_COMPILED_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders_spv")
file(MAKE_DIRECTORY ${SHADER_COMPILED_DIR})

set(SHADER_FILES
    "${APP_SHADERS_SRC_DIR}/fullscreen_quad.vert"
    "${APP_SHADERS_SRC_DIR}/image_process.frag"
)
set(COMPILED_SHADER_OUTPUTS "")
foreach(SHADER_INPUT_FILE ${SHADER_FILES})
    get_filename_component(SHADER_BASENAME ${SHADER_INPUT_FILE} NAME)
    set(SHADER_OUTPUT_FILE "${SHADER_COMPILED_DIR}/${SHADER_BASENAME}.spv")
    if(NOT EXISTS ${SHADER_INPUT_FILE})
        message(FATAL_ERROR "Shader source file not found: ${SHADER_INPUT_FILE}")
    endif()
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_FILE}
        COMMAND ${GLSLANG_VALIDATOR_CMD} -V -o ${SHADER_OUTPUT_FILE} ${SHADER_INPUT_FILE}
        DEPENDS ${SHADER_INPUT_FILE}
        COMMENT "Compiling ${SHADER_BASENAME} to SPIR-V"
        VERBATIM
    )
    list(APPEND COMPILED_SHADER_OUTPUTS ${SHADER_OUTPUT_FILE})
endforeach()
add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADER_OUTPUTS}
    COMMENT "Compiling all GLSL shaders to SPIR-V"
)
message(STATUS "Shader compilation configured. Output to: ${SHADER_COMPILED_DIR}")

set(IMGUI_SOURCE_DIR "${APP_EXTERNAL_DIR}/imgui")
if(NOT EXISTS "${IMGUI_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ImGui CMakeLists.txt not found at ${IMGUI_SOURCE_DIR}.")
endif()
set(IMGUI_BUILD_EXAMPLES OFF CACHE BOOL "Disable ImGui examples" FORCE)
add_subdirectory(${IMGUI_SOURCE_DIR} ${CMAKE_BINARY_DIR}/imgui_build EXCLUDE_FROM_ALL)
message(STATUS "Configured ImGui from: ${IMGUI_SOURCE_DIR}")

set(MOTIONCAM_DECODER_DIR "${APP_DEPS_DIR}/motioncam-decoder")
set(MOTIONCAM_DECODER_LIB_DIR "${MOTIONCAM_DECODER_DIR}/lib")
set(MOTIONCAM_DECODER_THIRDPARTY_DIR "${MOTIONCAM_DECODER_DIR}/thirdparty")

if(NOT EXISTS "${MOTIONCAM_DECODER_LIB_DIR}/Decoder.cpp")
    message(FATAL_ERROR "motioncam-decoder not found at ${MOTIONCAM_DECODER_DIR}. "
        "Initialize the submodule with `git submodule update --init --recursive` "
        "or clone https://github.com/mirsadm/motioncam-decoder into ${MOTIONCAM_DECODER_DIR}.")
endif()

add_library(motioncam_decoder STATIC
    "${MOTIONCAM_DECODER_LIB_DIR}/Decoder.cpp"
    "${MOTIONCAM_DECODER_LIB_DIR}/RawData.cpp"
    "${MOTIONCAM_DECODER_LIB_DIR}/RawData_Legacy.cpp"
)
set_property(TARGET motioncam_decoder PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(motioncam_decoder PUBLIC
    "${MOTIONCAM_DECODER_DIR}/lib/include"
    "${MOTIONCAM_DECODER_THIRDPARTY_DIR}"
)
if(MSVC)
    target_compile_options(motioncam_decoder PRIVATE /utf-8)
else()
    target_compile_options(motioncam_decoder PRIVATE -Wall -Werror -O3)
endif()
message(STATUS "Configured motioncam-decoder from: ${MOTIONCAM_DECODER_DIR}")

set(APP_SOURCES
  src/App/AppInit.cpp
  src/App/AppLoop.cpp
  src/App/AppIO.cpp
  src/App/AppDecode.cpp
  src/App/AppInput.cpp
  src/App/AppCleanup.cpp

  src/Audio/AudioController.cpp
  src/Audio/AudioHelpers.cpp

  src/Decoder/DecoderWrapper.cpp

  src/Graphics/Renderer_VK.cpp
  src/Graphics/VulkanHelpers.cpp
  src/Graphics/ImageResource.cpp
  src/Graphics/Pipeline.cpp
  src/Graphics/Descriptor.cpp

  src/Gui/GuiSetup.cpp
  src/Gui/GuiRender.cpp
  src/Gui/GuiUtils.cpp
  src/Gui/GuiStyles.cpp

  src/Playback/PlaybackController.cpp

  src/Utils/DebugLog.cpp
  src/Utils/OrientationUtils.cpp

  src/main.cpp
)

list(APPEND APP_SOURCES "${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
list(APPEND APP_SOURCES "${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")

message(STATUS "Application sources for executable: ${APP_SOURCES}")

if(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE "")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/Info.plist"
        MACOSX_RPATH TRUE
    )
else()
    add_executable(${PROJECT_NAME} WIN32 "")
endif()
target_sources(${PROJECT_NAME} PRIVATE ${APP_SOURCES})

if(WIN32 AND EXISTS "${APP_ROOT_DIR}/icon.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${APP_ROOT_DIR}/icon.rc")
    message(STATUS "Added Windows icon resource: ${APP_ROOT_DIR}/icon.rc")
endif()

add_dependencies(${PROJECT_NAME} CompileShaders glm motioncam_decoder imgui)

target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

if(MSVC AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
    message(STATUS "MSVC: Setting LINK_FLAGS /ENTRY:mainCRTStartup for ${PROJECT_NAME}")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/include/App"
  "${PROJECT_SOURCE_DIR}/include/Audio"
  "${PROJECT_SOURCE_DIR}/include/Decoder"
  "${PROJECT_SOURCE_DIR}/include/Graphics"
  "${PROJECT_SOURCE_DIR}/include/Gui"
  "${PROJECT_SOURCE_DIR}/include/Playback"
  "${PROJECT_SOURCE_DIR}/include/Utils"

  "${VMA_INCLUDE_DIR}"
  "${Vulkan_INCLUDE_DIRS}"
  ${PLATFORM_INCLUDE_DIRS}

  "${MOTIONCAM_DECODER_DIR}/lib/include"
  "${MOTIONCAM_DECODER_THIRDPARTY_DIR}"

  "${IMGUI_SOURCE_DIR}"
  "${IMGUI_SOURCE_DIR}/backends"
  "${APP_EXTERNAL_DIR}/tinydngwriter"
)

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL2_ARCH_SUFFIX "x64")
    else()
        set(SDL2_ARCH_SUFFIX "x86")
    endif()
    set(SDL2_LIBRARY_DIR "${SDL2_PATH}/lib/${SDL2_ARCH_SUFFIX}")

    set(GLFW_LIB_PATH_VC_SPECIFIC "${GLFW_PATH}/lib-vc2022/glfw3.lib")
    if(NOT EXISTS "${GLFW_LIB_PATH_VC_SPECIFIC}" AND MSVC)
        message(FATAL_ERROR "Pre-built GLFW library not found at ${GLFW_LIB_PATH_VC_SPECIFIC}. Please check GLFW_PATH or adjust for your compiler.")
    endif()

    list(APPEND PLATFORM_LIBRARIES "${GLFW_LIB_PATH_VC_SPECIFIC}" "${SDL2_LIBRARY_DIR}/SDL2.lib")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    motioncam_decoder
    imgui
    glm
    ${PLATFORM_LIBRARIES}
    ${Vulkan_LIBRARIES}
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE Shlwapi.lib Comdlg32.lib dwmapi.lib Shell32.lib)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
endif()


set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    OUTPUT_NAME "MotionCam Player"
)

if(APPLE)
    set(APP_BUNDLE_RESOURCES "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_RESOURCES}/shaders_spv"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_COMPILED_DIR}" "${APP_BUNDLE_RESOURCES}/shaders_spv"
        COMMENT "Copying compiled shaders into bundle Resources"
        VERBATIM
    )
    if(EXISTS "${APP_ASSETS_DIR}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_RESOURCES}/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_ASSETS_DIR}" "${APP_BUNDLE_RESOURCES}/assets"
            COMMENT "Copying assets into bundle Resources"
            VERBATIM
        )
    else()
        message(WARNING "Assets directory not found at ${APP_ASSETS_DIR}. Skipping asset copy.")
    endif()
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_COMPILED_DIR}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders_spv"
        COMMENT "Copying compiled shaders to executable output directory"
        VERBATIM
    )
    message(STATUS "POST_BUILD: Configured to copy ${SHADER_COMPILED_DIR} to $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders_spv")

    if(EXISTS "${APP_ASSETS_DIR}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${APP_ASSETS_DIR}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
            COMMENT "Copying assets to executable output directory"
            VERBATIM
        )
        message(STATUS "POST_BUILD: Configured to copy ${APP_ASSETS_DIR} to $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets")
    else()
        message(WARNING "Assets directory not found at ${APP_ASSETS_DIR}. Skipping asset copy.")
    endif()
endif()

# Copy SDL2.dll to executable output directory on Windows
if(WIN32)
    set(SDL2_DLL_PATH "${SDL2_LIBRARY_DIR}/SDL2.dll")
    if(EXISTS "${SDL2_DLL_PATH}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL_PATH}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/SDL2.dll"
            COMMENT "Copying SDL2.dll to executable output directory"
            VERBATIM
        )
        message(STATUS "POST_BUILD: Configured to copy ${SDL2_DLL_PATH} to $<TARGET_FILE_DIR:${PROJECT_NAME}>/")
    else()
        message(WARNING "SDL2.dll not found at ${SDL2_DLL_PATH}. You may need to copy it manually.")
    endif()

    # Copy glfw3.dll to executable output directory (dynamic GLFW build)
    set(GLFW_DLL_PATH "${GLFW_PATH}/lib-vc2022/glfw3.dll")
    if(EXISTS "${GLFW_DLL_PATH}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GLFW_DLL_PATH}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/glfw3.dll"
            COMMENT "Copying glfw3.dll to executable output directory"
            VERBATIM
        )
        message(STATUS "POST_BUILD: Configured to copy ${GLFW_DLL_PATH} to $<TARGET_FILE_DIR:${PROJECT_NAME}>/")
    else()
        message(WARNING "glfw3.dll not found at ${GLFW_DLL_PATH}. If you use the dynamic GLFW build, copy it manually or switch to the static lib.")
    endif()
endif()

message(STATUS "---------------------------------------------------------------------")
message(STATUS "Final Configuration Summary:")
message(STATUS "  Target Executable: MotionCam Player")
message(STATUS "  Output Directory:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}")
message(STATUS "  Output Executable: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/MotionCam Player${CMAKE_EXECUTABLE_SUFFIX}")
message(STATUS "---------------------------------------------------------------------")
