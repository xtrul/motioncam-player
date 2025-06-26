cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0111 NEW) # Important for MACOSX_RPATH behavior
project(MotionCam_Player VERSION 0.5 LANGUAGES C CXX)

# --- Universal macOS binary (Apple Silicon + Intel) ---
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)

# --- Use custom-built universal dependencies from FATPREFIX ---
if(DEFINED ENV{FATPREFIX})
    set(APP_FAT_DEPS_DIR "$ENV{FATPREFIX}")
elseif(EXISTS "${CMAKE_HOME_DIRECTORY}/fat-deps")
    set(APP_FAT_DEPS_DIR "${CMAKE_HOME_DIRECTORY}/fat-deps")
else()
    set(APP_FAT_DEPS_DIR "$ENV{HOME}/fat-deps")
    if(NOT EXISTS "${APP_FAT_DEPS_DIR}")
        message(FATAL_ERROR "FATPREFIX environment variable not set, and default fat-deps directory not found at ${CMAKE_HOME_DIRECTORY}/fat-deps or $ENV{HOME}/fat-deps. Please build universal dependencies first and set FATPREFIX.")
    endif()
endif()

list(PREPEND CMAKE_PREFIX_PATH "${APP_FAT_DEPS_DIR}")
set(ENV{PKG_CONFIG_PATH} "${APP_FAT_DEPS_DIR}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
message(STATUS "Using custom universal dependencies from: ${APP_FAT_DEPS_DIR}")
message(STATUS "CMAKE_PREFIX_PATH set to: ${CMAKE_PREFIX_PATH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MACOSX_RPATH TRUE)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "")

# Helper function
function(copy_and_fixup_dylib)
    set(options "")
    # BUNDLE_LINK_NAME is the name the executable is linked against AND the name it will have in the bundle.
    # SOURCE_ACTUAL_PATH is the actual file path in fat-deps (e.g., /path/to/libvulkan.dylib)
    set(oneValueArgs TARGET_NAME SOURCE_ACTUAL_PATH BUNDLE_LINK_NAME)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET_NAME OR NOT ARG_SOURCE_ACTUAL_PATH OR NOT ARG_BUNDLE_LINK_NAME)
        message(FATAL_ERROR "copy_and_fixup_dylib: TARGET_NAME, SOURCE_ACTUAL_PATH, and BUNDLE_LINK_NAME are required.")
    endif()

    if(NOT EXISTS "${ARG_SOURCE_ACTUAL_PATH}")
        message(WARNING "copy_and_fixup_dylib: Library source not found at ${ARG_SOURCE_ACTUAL_PATH}. This will be an issue for bundling.")
        return()
    endif()

    get_target_property(_bundle_output_name ${ARG_TARGET_NAME} OUTPUT_NAME)
    if(NOT _bundle_output_name)
        set(_bundle_output_name ${ARG_TARGET_NAME})
    endif()
    set(BUNDLE_LIB_DIR "${CMAKE_BINARY_DIR}/${_bundle_output_name}.app/Contents/Frameworks")
    set(FINAL_BUNDLE_PATH "${BUNDLE_LIB_DIR}/${ARG_BUNDLE_LINK_NAME}") # File in bundle will have this name
    set(TARGET_EXECUTABLE_PATH "$<TARGET_FILE:${ARG_TARGET_NAME}>")

    add_custom_command(TARGET ${ARG_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_LIB_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ARG_SOURCE_ACTUAL_PATH}"
                    "${FINAL_BUNDLE_PATH}"
        COMMAND echo "Fixing install name for ${ARG_BUNDLE_LINK_NAME} in bundle: ${FINAL_BUNDLE_PATH}"
        COMMAND install_name_tool -id "@rpath/${ARG_BUNDLE_LINK_NAME}" "${FINAL_BUNDLE_PATH}" VERBATIM
        COMMAND echo "Changing linkage in ${ARG_TARGET_NAME} for ${ARG_BUNDLE_LINK_NAME} (original source: ${ARG_SOURCE_ACTUAL_PATH})"
        COMMAND install_name_tool -change "${ARG_BUNDLE_LINK_NAME}" "@rpath/${ARG_BUNDLE_LINK_NAME}" "${TARGET_EXECUTABLE_PATH}" VERBATIM
        COMMAND install_name_tool -change "${ARG_SOURCE_ACTUAL_PATH}" "@rpath/${ARG_BUNDLE_LINK_NAME}" "${TARGET_EXECUTABLE_PATH}" VERBATIM
        COMMENT "Embedding ${ARG_SOURCE_ACTUAL_PATH} as ${ARG_BUNDLE_LINK_NAME} and fixing up for ${ARG_TARGET_NAME}.app"
    )
endfunction()

message(STATUS "---------------------------------------------------------------------")
message(STATUS "Configuring ${PROJECT_NAME} (Version ${PROJECT_VERSION}) for macOS (Universal)")
message(STATUS "Target Architectures: ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "Project Source Directory: ${PROJECT_SOURCE_DIR}")
message(STATUS "Project Binary Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "---------------------------------------------------------------------")

set(APP_SRC_DIR     "${PROJECT_SOURCE_DIR}/src")
set(APP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set(EXTERNAL_DIR    "${PROJECT_SOURCE_DIR}/external")

message(STATUS "Application .cpp files in: ${APP_SRC_DIR}")
message(STATUS "Application .h files in: ${APP_INCLUDE_DIR}")
message(STATUS "External libs in:     ${EXTERNAL_DIR}")

# --- Vulkan SDK ---
find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: Includes=${Vulkan_INCLUDE_DIRS}, Libraries=${Vulkan_LIBRARIES}")
if(TARGET Vulkan::Vulkan)
    get_target_property(VULKAN_LOADER_LIB_PATH_FROM_CMAKE Vulkan::Vulkan IMPORTED_LOCATION_RELEASE)
    if(NOT VULKAN_LOADER_LIB_PATH_FROM_CMAKE)
        get_target_property(VULKAN_LOADER_LIB_PATH_FROM_CMAKE Vulkan::Vulkan IMPORTED_LOCATION)
    endif()
    message(STATUS "Vulkan Loader Library (from Vulkan::Vulkan target): ${VULKAN_LOADER_LIB_PATH_FROM_CMAKE}")
    set(VULKAN_LOADER_LIBRARY_TO_BUNDLE ${VULKAN_LOADER_LIB_PATH_FROM_CMAKE})
else()
    message(WARNING "Vulkan::Vulkan target not found. Vulkan_LIBRARIES: ${Vulkan_LIBRARIES}")
    find_library(VULKAN_LOADER_LIBRARY_TO_BUNDLE NAMES libvulkan.dylib vulkan.dylib libvulkan.1.dylib vulkan.1
        HINTS "${APP_FAT_DEPS_DIR}/lib"
        NO_DEFAULT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    message(STATUS "Vulkan Loader Library (manual find for bundling): ${VULKAN_LOADER_LIBRARY_TO_BUNDLE}")
endif()

find_library(MOLTENVK_LIBRARY_TO_BUNDLE NAMES MoltenVK libMoltenVK.dylib
    HINTS "${APP_FAT_DEPS_DIR}/lib"
    NO_DEFAULT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
message(STATUS "MoltenVK Library (for bundling): ${MOLTENVK_LIBRARY_TO_BUNDLE}")

# --- GLM (external subdirectory) ---
set(GLM_SOURCE_DIR "${EXTERNAL_DIR}/glm")
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
add_subdirectory(${GLM_SOURCE_DIR} ${CMAKE_BINARY_DIR}/glm_build EXCLUDE_FROM_ALL)
message(STATUS "Configured GLM from: ${GLM_SOURCE_DIR}")

# --- GLFW (should use $FATPREFIX) ---
set(GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH "")
set(GLFW3_INCLUDE_DIR_TEMP "")

find_package(glfw3 QUIET CONFIG)
if(glfw3_FOUND AND TARGET glfw3::glfw)
    message(STATUS "Found GLFW (via find_package CONFIG mode): Version ${glfw3_VERSION_STRING}")
    set(GLFW3_INCLUDE_DIR_TEMP ${glfw3_INCLUDE_DIRS})
    if(GLFW3_INCLUDE_DIR_TEMP AND NOT IS_ABSOLUTE "${GLFW3_INCLUDE_DIR_TEMP}")
        list(GET GLFW3_INCLUDE_DIR_TEMP 0 GLFW3_INCLUDE_DIR_TEMP)
    endif()
    get_target_property(TMP_GLFW_LIB glfw3::glfw IMPORTED_LOCATION_RELEASE)
    if(NOT TMP_GLFW_LIB)
        get_target_property(TMP_GLFW_LIB glfw3::glfw IMPORTED_LOCATION)
    endif()
    if(TMP_GLFW_LIB AND EXISTS "${TMP_GLFW_LIB}")
        set(GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH "${TMP_GLFW_LIB}")
        message(STATUS "GLFW Library (from target glfw3::glfw for bundling source): ${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    else()
        message(WARNING "Target glfw3::glfw found, but its IMPORTED_LOCATION is not set or invalid. Will attempt manual find for library path.")
    endif()
else()
    message(STATUS "find_package(glfw3 CONFIG) failed or target glfw3::glfw not created. Falling back to manual find_path/find_library for GLFW.")
endif()

if(NOT GLFW3_INCLUDE_DIR_TEMP OR NOT EXISTS "${GLFW3_INCLUDE_DIR_TEMP}/GLFW/glfw3.h")
    find_path(MANUAL_GLFW_INCLUDE_DIR_RESULT NAMES GLFW/glfw3.h HINTS "${APP_FAT_DEPS_DIR}/include" NO_DEFAULT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(MANUAL_GLFW_INCLUDE_DIR_RESULT AND EXISTS "${MANUAL_GLFW_INCLUDE_DIR_RESULT}/GLFW/glfw3.h")
        set(GLFW3_INCLUDE_DIR_TEMP "${MANUAL_GLFW_INCLUDE_DIR_RESULT}")
    else()
        message(WARNING "Manual find_path for GLFW include dir FAILED.")
    endif()
endif()

if(NOT GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH OR NOT EXISTS "${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    find_library(MANUAL_GLFW_LIB NAMES libglfw.dylib glfw.dylib libglfw.3.dylib
                 HINTS "${APP_FAT_DEPS_DIR}/lib" NO_DEFAULT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(MANUAL_GLFW_LIB AND EXISTS "${MANUAL_GLFW_LIB}")
        set(GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH "${MANUAL_GLFW_LIB}")
        message(STATUS "Found GLFW library (manual find for bundling source): ${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    endif()
endif()
if(NOT GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH)
    message(FATAL_ERROR "GLFW library source not found in ${APP_FAT_DEPS_DIR}/lib.")
endif()

set(GLFW3_INCLUDE_DIR ${GLFW3_INCLUDE_DIR_TEMP})

if(GLFW3_INCLUDE_DIR AND EXISTS "${GLFW3_INCLUDE_DIR}/GLFW/glfw3.h" AND GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH AND EXISTS "${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    message(STATUS "Using GLFW: include=${GLFW3_INCLUDE_DIR}, source_lib=${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    add_library(glfw_interface INTERFACE IMPORTED GLOBAL)
    set_target_properties(glfw_interface PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${GLFW3_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES      "${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
else()
    message(FATAL_ERROR "GLFW include or library source NOT FOUND or invalid.")
endif()

# --- SDL2 (should use $FATPREFIX) ---
find_package(SDL2 CONFIG REQUIRED)
get_target_property(SDL2_LIBRARY_TO_BUNDLE SDL2::SDL2 IMPORTED_LOCATION_RELEASE)
if(NOT SDL2_LIBRARY_TO_BUNDLE)
    get_target_property(SDL2_LIBRARY_TO_BUNDLE SDL2::SDL2 IMPORTED_LOCATION)
endif()
if(NOT SDL2_LIBRARY_TO_BUNDLE OR NOT EXISTS "${SDL2_LIBRARY_TO_BUNDLE}")
    message(FATAL_ERROR "SDL2 target SDL2::SDL2 found, but its IMPORTED_LOCATION is not set or invalid.")
endif()
message(STATUS "SDL2 Library (for bundling source): ${SDL2_LIBRARY_TO_BUNDLE}")

# --- Zstd (should use $FATPREFIX) ---
find_path(ZSTD_INCLUDE_DIR NAMES zstd.h HINTS "${APP_FAT_DEPS_DIR}/include" REQUIRED NO_DEFAULT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH)
find_library(ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH NAMES libzstd.dylib zstd.dylib libzstd.1.dylib
    HINTS "${APP_FAT_DEPS_DIR}/lib"
    NO_DEFAULT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
message(STATUS "Zstd Library (for bundling source): ${ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH}")

if(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH AND EXISTS "${ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    add_library(Zstd_interface INTERFACE IMPORTED GLOBAL)
    set_target_properties(Zstd_interface PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH}")
else()
    message(FATAL_ERROR "Zstd include or library source not found.")
endif()

# --- Vulkan Memory Allocator (VMA) ---
set(VMA_INCLUDE_DIR "${EXTERNAL_DIR}/vma")
message(STATUS "Found VMA headers in: ${VMA_INCLUDE_DIR}")

# --- Shader compilation to SPIR-V ---
find_program(GLSLANG_VALIDATOR_CMD NAMES glslangValidator HINTS "${APP_FAT_DEPS_DIR}/bin" ENV PATH REQUIRED)
set(SHADER_SOURCE_DIR   "${PROJECT_SOURCE_DIR}/shaders")
set(SHADER_COMPILED_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders_spv")
file(MAKE_DIRECTORY "${SHADER_COMPILED_DIR}")
set(SHADER_FILES "${SHADER_SOURCE_DIR}/fullscreen_quad.vert" "${SHADER_SOURCE_DIR}/image_process.frag")
set(COMPILED_SHADER_OUTPUTS "")
foreach(SHADER_INPUT_FILE IN LISTS SHADER_FILES)
    get_filename_component(SHADER_BASENAME "${SHADER_INPUT_FILE}" NAME)
    set(SHADER_OUTPUT_FILE "${SHADER_COMPILED_DIR}/${SHADER_BASENAME}.spv")
    add_custom_command(OUTPUT "${SHADER_OUTPUT_FILE}"
        COMMAND "${GLSLANG_VALIDATOR_CMD}" -V -o "${SHADER_OUTPUT_FILE}" "${SHADER_INPUT_FILE}"
        DEPENDS "${SHADER_INPUT_FILE}"
        COMMENT  "Compiling ${SHADER_BASENAME} → SPIR-V" VERBATIM)
    list(APPEND COMPILED_SHADER_OUTPUTS "${SHADER_OUTPUT_FILE}")
endforeach()
add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADER_OUTPUTS})

# --- ImGui, motioncam-decoder subdirs ---
add_subdirectory(${EXTERNAL_DIR}/imgui       ${CMAKE_BINARY_DIR}/imgui_build EXCLUDE_FROM_ALL)
add_subdirectory(motioncam-decoder           EXCLUDE_FROM_ALL)

# --- App sources & target ---
set(APP_SOURCES
  src/main.cpp src/App.cpp src/Renderer_VK.cpp src/GuiOverlay.cpp
  src/PlaybackController.cpp src/AudioController.cpp src/DecoderWrapper.cpp
  src/Logger.cpp src/DebugLog.cpp src/SingleInstance.cpp src/ThumbnailGenerator.cpp
  ${EXTERNAL_DIR}/imgui/backends/imgui_impl_glfw.cpp
  ${EXTERNAL_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)
if(APPLE)
  list(APPEND APP_SOURCES src/MacOpenFile.mm)
endif()
add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${APP_SOURCES})
add_dependencies(${PROJECT_NAME} CompileShaders glm motioncam_decoder imgui)
target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/macos/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.motioncamapp.mcrawplayer"
    OUTPUT_NAME "MotionCam Player"
    MACOSX_RPATH TRUE
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks"
)

target_include_directories(${PROJECT_NAME} PRIVATE
  include ${VMA_INCLUDE_DIR} ${Vulkan_INCLUDE_DIRS} ${GLFW3_INCLUDE_DIR}
  ${SDL2_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIR} motioncam-decoder/lib/include
  motioncam-decoder/thirdparty ${EXTERNAL_DIR}/imgui ${EXTERNAL_DIR}/imgui/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  motioncam_decoder imgui glm::glm Vulkan::Vulkan glfw_interface SDL2::SDL2 Zstd_interface
  "-framework Metal" "-framework Cocoa" "-framework IOKit"
  "-framework CoreVideo" "-framework CoreFoundation" "-framework AudioToolbox"
)

# --- Embed dylibs into the bundle ---
# VULKAN
if(VULKAN_LOADER_LIBRARY_TO_BUNDLE AND EXISTS "${VULKAN_LOADER_LIBRARY_TO_BUNDLE}")
    copy_and_fixup_dylib(TARGET_NAME      ${PROJECT_NAME}
                         SOURCE_ACTUAL_PATH "${VULKAN_LOADER_LIBRARY_TO_BUNDLE}"
                         BUNDLE_LINK_NAME "libvulkan.1.dylib")
else()
    message(WARNING "Vulkan Loader (VULKAN_LOADER_LIBRARY_TO_BUNDLE) not found or variable empty.")
endif()

# GLFW
if(GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH AND EXISTS "${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    copy_and_fixup_dylib(TARGET_NAME      ${PROJECT_NAME}
                         SOURCE_ACTUAL_PATH "${GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH}"
                         BUNDLE_LINK_NAME "libglfw.3.dylib")
else()
    message(WARNING "GLFW library (GLFW3_LIBRARY_TO_BUNDLE_TEMP_PATH) not found or variable empty.")
endif()

# SDL2
if(SDL2_LIBRARY_TO_BUNDLE AND EXISTS "${SDL2_LIBRARY_TO_BUNDLE}")
    get_filename_component(SDL2_ACTUAL_FILENAME "${SDL2_LIBRARY_TO_BUNDLE}" NAME)
    copy_and_fixup_dylib(TARGET_NAME      ${PROJECT_NAME}
                         SOURCE_ACTUAL_PATH "${SDL2_LIBRARY_TO_BUNDLE}"
                         BUNDLE_LINK_NAME "${SDL2_ACTUAL_FILENAME}")
else()
    message(WARNING "SDL2 library (SDL2_LIBRARY_TO_BUNDLE) not found or variable empty.")
endif()

# ZSTD
if(ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH AND EXISTS "${ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH}")
    copy_and_fixup_dylib(TARGET_NAME      ${PROJECT_NAME}
                         SOURCE_ACTUAL_PATH "${ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH}"
                         BUNDLE_LINK_NAME "libzstd.1.dylib")
else()
    message(WARNING "Zstd library (ZSTD_LIBRARY_TO_BUNDLE_TEMP_PATH) not found or variable empty.")
endif()

# MOLTENVK
if(MOLTENVK_LIBRARY_TO_BUNDLE AND EXISTS "${MOLTENVK_LIBRARY_TO_BUNDLE}")
    get_filename_component(MVK_ACTUAL_FILENAME "${MOLTENVK_LIBRARY_TO_BUNDLE}" NAME)
    copy_and_fixup_dylib(TARGET_NAME      ${PROJECT_NAME}
                         SOURCE_ACTUAL_PATH "${MOLTENVK_LIBRARY_TO_BUNDLE}"
                         BUNDLE_LINK_NAME "${MVK_ACTUAL_FILENAME}")
else()
    message(WARNING "MoltenVK library (MOLTENVK_LIBRARY_TO_BUNDLE) not found or variable empty.")
endif()

get_target_property(APP_BUNDLE_OUTPUT_NAME ${PROJECT_NAME} OUTPUT_NAME)
if(NOT APP_BUNDLE_OUTPUT_NAME)
    set(APP_BUNDLE_OUTPUT_NAME ${PROJECT_NAME})
endif()
set(MACOS_BUNDLE_FRAMEWORKS_DIR "${CMAKE_BINARY_DIR}/${APP_BUNDLE_OUTPUT_NAME}.app/Contents/Frameworks")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MACOS_BUNDLE_FRAMEWORKS_DIR}"
    COMMENT "Ensuring Frameworks directory exists")

# --- Embed .icns ---
set_source_files_properties("${PROJECT_SOURCE_DIR}/mcraw_player.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
target_sources(${PROJECT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/mcraw_player.icns")
set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE_ICON_FILE "mcraw_player.icns")

# --- Resource copy ---
set(APP_BUNDLE_ROOT_DIR "${CMAKE_BINARY_DIR}/${APP_BUNDLE_OUTPUT_NAME}.app")
set(APP_BUNDLE_RESOURCES_DIR "${APP_BUNDLE_ROOT_DIR}/Contents/Resources")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_RESOURCES_DIR}/shaders_spv"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_COMPILED_DIR}" "${APP_BUNDLE_RESOURCES_DIR}/shaders_spv"
    COMMENT "Copying SPIR-V shaders")
if(EXISTS "${PROJECT_SOURCE_DIR}/assets")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_RESOURCES_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "${APP_BUNDLE_RESOURCES_DIR}/assets"
        COMMENT "Copying assets")
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/fonts")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_RESOURCES_DIR}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/fonts" "${APP_BUNDLE_RESOURCES_DIR}/fonts"
        COMMENT "Copying fonts")
endif()

# --- Copy MoltenVK_icd.json AND Patch It ---
set(MOLTENVK_ICD_JSON_ORIGINAL_NAME "MoltenVK_icd.json")
set(MOLTENVK_ICD_JSON_BUNDLED_NAME "MoltenVK_icd.json")

find_file(MOLTENVK_ICD_JSON_SOURCE_PATH NAMES ${MOLTENVK_ICD_JSON_ORIGINAL_NAME}
    HINTS
        "${APP_FAT_DEPS_DIR}/share/vulkan/icd.d"
        "${APP_FAT_DEPS_DIR}/etc/vulkan/icd.d"
        "$ENV{VULKAN_SDK}/share/vulkan/icd.d"
        "$ENV{VULKAN_SDK}/etc/vulkan/icd.d"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)

if(NOT MOLTENVK_ICD_JSON_SOURCE_PATH)
    message(WARNING "Original MoltenVK_icd.json NOT FOUND. vkCreateInstance will likely fail.")
else()
    message(STATUS "Found original MoltenVK_icd.json for bundling: ${MOLTENVK_ICD_JSON_SOURCE_PATH}")

    set(APP_BUNDLE_VK_ICD_DIR_REL "vulkan/icd.d")
    set(APP_BUNDLE_VK_ICD_DIR_ABS "${APP_BUNDLE_RESOURCES_DIR}/${APP_BUNDLE_VK_ICD_DIR_REL}")
    set(BUNDLED_MOLTENVK_ICD_JSON_PATH_ABS "${APP_BUNDLE_VK_ICD_DIR_ABS}/${MOLTENVK_ICD_JSON_BUNDLED_NAME}")

    set(ORIGINAL_JSON_LIB_PATH_STRING "\"../../../lib/libMoltenVK.dylib\"")
    set(CORRECT_JSON_LIB_PATH_STRING  "\"../../../Frameworks/libMoltenVK.dylib\"")

    set(CMAKE_HELPER_SCRIPT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cmake_scripts_mcrawplayer")
    file(MAKE_DIRECTORY "${CMAKE_HELPER_SCRIPT_DIR}")
    set(PATCH_SCRIPT_CONTENT
"# This script is executed by: cmake -P PatchScript.cmake
if(NOT DEFINED JSON_FILE_TO_PATCH)
    message(FATAL_ERROR \"JSON_FILE_TO_PATCH variable not defined for patch script.\")
endif()
if(NOT DEFINED OLD_STRING_TO_REPLACE)
    message(FATAL_ERROR \"OLD_STRING_TO_REPLACE variable not defined for patch script.\")
endif()
if(NOT DEFINED NEW_STRING_TO_REPLACE)
    message(FATAL_ERROR \"NEW_STRING_TO_REPLACE variable not defined for patch script.\")
endif()

if(NOT EXISTS \${JSON_FILE_TO_PATCH})
    message(WARNING \"Patch Script: JSON file not found at '\${JSON_FILE_TO_PATCH}'. Skipping patch.\")
    return()
endif()

file(READ \"\${JSON_FILE_TO_PATCH}\" CURRENT_CONTENT)
string(REPLACE \"\${OLD_STRING_TO_REPLACE}\" \"\${NEW_STRING_TO_REPLACE}\" MODIFIED_CONTENT \"\${CURRENT_CONTENT}\")

if(NOT \"\${CURRENT_CONTENT}\" STREQUAL \"\${MODIFIED_CONTENT}\")
    file(WRITE \"\${JSON_FILE_TO_PATCH}\" \"\${MODIFIED_CONTENT}\")
    message(STATUS \"Patch Script: Patched '\${JSON_FILE_TO_PATCH}'. Replaced '\${OLD_STRING_TO_REPLACE}' with '\${NEW_STRING_TO_REPLACE}'.\")
else()
    message(STATUS \"Patch Script: No changes made to '\${JSON_FILE_TO_PATCH}'. Pattern '\${OLD_STRING_TO_REPLACE}' not found or content already correct.\")
endif()
"
    )
    set(PATCH_SCRIPT_FILE_PATH "${CMAKE_HELPER_SCRIPT_DIR}/PatchMoltenVKJson.cmake")
    file(WRITE "${PATCH_SCRIPT_FILE_PATH}" "${PATCH_SCRIPT_CONTENT}")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_VK_ICD_DIR_ABS}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MOLTENVK_ICD_JSON_SOURCE_PATH}"
                "${BUNDLED_MOLTENVK_ICD_JSON_PATH_ABS}"
        COMMAND ${CMAKE_COMMAND}
                -DJSON_FILE_TO_PATCH=${BUNDLED_MOLTENVK_ICD_JSON_PATH_ABS}
                -DOLD_STRING_TO_REPLACE=${ORIGINAL_JSON_LIB_PATH_STRING}
                -DNEW_STRING_TO_REPLACE=${CORRECT_JSON_LIB_PATH_STRING}
                -P "${PATCH_SCRIPT_FILE_PATH}"
        COMMENT "Copying and Patching MoltenVK_icd.json in bundle"
        VERBATIM
    )
endif()

message(STATUS "---------------------------------------------------------------------")
message(STATUS "App Bundle will contain universal dylibs, shaders, assets, fonts and icon.")
message(STATUS "Find commands should prioritize libraries from: ${APP_FAT_DEPS_DIR}")
message(STATUS "RPATH for executable ${PROJECT_NAME} should include @executable_path/../Frameworks")
message(STATUS "MoltenVK_icd.json will be copied and patched for bundle.")
message(STATUS "Bundled dylibs will be named to match linker expectations (e.g., libvulkan.1.dylib).")
message(STATUS "---------------------------------------------------------------------")