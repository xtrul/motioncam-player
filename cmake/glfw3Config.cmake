# File: <YourProjectRoot>/cmake/glfw3Config.cmake
if(NOT DEFINED GLFW_PATH)
    message(SEND_ERROR "glfw3Config.cmake: GLFW_PATH was not defined by the calling project.")
    set(glfw3_FOUND FALSE)
    return()
endif()

set(GLFW3_INCLUDE_DIR_CONFIG_GUESS "${GLFW_PATH}/include")
set(GLFW3_LIBRARY_FILE_CONFIG_GUESS "${GLFW_PATH}/lib-vc2022/glfw3.lib")

if(EXISTS "${GLFW3_INCLUDE_DIR_CONFIG_GUESS}" AND EXISTS "${GLFW3_LIBRARY_FILE_CONFIG_GUESS}")
    set(glfw3_FOUND TRUE)
    set(GLFW3_INCLUDE_DIRS "${GLFW3_INCLUDE_DIR_CONFIG_GUESS}")
    set(GLFW3_LIBRARIES "${GLFW3_LIBRARY_FILE_CONFIG_GUESS}")

    if(NOT TARGET glfw::glfw)
        add_library(glfw::glfw UNKNOWN IMPORTED GLOBAL)
        set_target_properties(glfw::glfw PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW3_INCLUDE_DIRS}"
        )
        # Set IMPORTED_LOCATION for all relevant configurations to point to your .lib file.
        # For an UNKNOWN IMPORTED target, this tells CMake where the library file is.
        # MSVC linker can handle a .lib file here whether it's static or an import lib.
        set_target_properties(glfw::glfw PROPERTIES
            IMPORTED_LOCATION "${GLFW3_LIBRARIES}" # General case
            IMPORTED_LOCATION_DEBUG "${GLFW3_LIBRARIES}"
            IMPORTED_LOCATION_RELEASE "${GLFW3_LIBRARIES}"
            IMPORTED_LOCATION_MINSIZEREL "${GLFW3_LIBRARIES}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${GLFW3_LIBRARIES}"
        )
        # Also set IMPIMPORTED_IMPLIB for MSVC for good measure if IMPORTED_LOCATION alone isn't enough
        # for the linker to treat it as an import lib vs static lib correctly in all cases.
        if (MSVC)
             set_target_properties(glfw::glfw PROPERTIES 
                IMPORTED_IMPLIB "${GLFW3_LIBRARIES}"
                IMPORTED_IMPLIB_DEBUG "${GLFW3_LIBRARIES}"
                IMPORTED_IMPLIB_RELEASE "${GLFW3_LIBRARIES}"
                IMPORTED_IMPLIB_MINSIZEREL "${GLFW3_LIBRARIES}"
                IMPORTED_IMPLIB_RELWITHDEBINFO "${GLFW3_LIBRARIES}"
            )
        endif()
    endif()

    if(TARGET glfw::glfw AND NOT TARGET glfw)
        add_library(glfw ALIAS glfw::glfw)
        message(STATUS "glfw3Config.cmake: Aliased glfw::glfw to plain 'glfw' target.")
    endif()

    set(GLFW_INCLUDE_DIRS ${GLFW3_INCLUDE_DIRS})
    set(GLFW_LIBRARIES ${GLFW3_LIBRARIES})
    message(STATUS "glfw3Config.cmake: Successfully configured GLFW from pre-set GLFW_PATH: ${GLFW_PATH}")
else()
    set(glfw3_FOUND FALSE)
    if(NOT EXISTS "${GLFW3_INCLUDE_DIR_CONFIG_GUESS}")
        message(WARNING "  Missing include directory: ${GLFW3_INCLUDE_DIR_CONFIG_GUESS}")
    endif()
    if(NOT EXISTS "${GLFW3_LIBRARY_FILE_CONFIG_GUESS}")
        message(WARNING "  Missing library file: ${GLFW3_LIBRARY_FILE_CONFIG_GUESS}")
    endif()
endif()