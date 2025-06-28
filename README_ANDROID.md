# MotionCam Player Android Port Guide

This document outlines the high level steps for building an Android version of
`motioncam-player`. It assumes you have basic knowledge of Android Studio and
that the Android SDK and NDK are installed.

## 1. Install Required Tools

1. **Android Studio** – Download from the official website and install.
2. **Android NDK** – During Android Studio setup, install the NDK and CMake
   packages via the SDK Manager.
3. **Java Development Kit** – Required by Android Studio (often bundled).

## 2. Create a New Android Studio Project

1. Launch Android Studio and create a new project using the *Native C++* template.
2. Choose a package name and minimum SDK level that suits your needs.
3. When prompted for a C++ standard, select `C++17`.

## 3. Add the Existing Source Code

1. Copy the contents of this repository into the `app/src/main/cpp` directory of
   your new Android project.
2. Remove the existing `native-lib.cpp` sample file if present.
3. Ensure the `include` and `external` directories are also copied.

## 4. Configure CMake for Android

Edit your module-level `build.gradle` to point to the supplied `CMakeLists.txt`:

```gradle
android {
    defaultConfig {
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path "CMakeLists.txt"
        }
    }
    ndkVersion "${ndkVersion}"
}
```

Update the existing `CMakeLists.txt` with the following Android section:

```cmake
if(ANDROID)
    set(APP_PLATFORM android-21)
    add_definitions(-D__ANDROID__)
    # GLFW is not available on Android; use SDL2 instead.
    # Ensure SDL2 is built for Android and update include/lib paths here.
    set(GLFW_LIB_PATH_VC_SPECIFIC "")
    add_library(SDL2 SHARED IMPORTED)
    set_target_properties(SDL2 PROPERTIES
        IMPORTED_LOCATION "${APP_EXTERNAL_DIR}/SDL2/lib/\${ANDROID_ABI}/libSDL2.so"
        INTERFACE_INCLUDE_DIRECTORIES "${APP_EXTERNAL_DIR}/SDL2/include")
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2 log android)
endif()
```

This snippet disables the GLFW dependency and links against the prebuilt SDL2
library shipped with the project. The paths may need adjustment to match your
NDK build configuration.

## 5. Build and Deploy

1. In Android Studio, select a device or emulator and click **Run**.
2. Gradle will invoke CMake to build the native library and package it into the
   APK.
3. The first build can take several minutes as the toolchain compiles all
   dependencies. Subsequent builds will be faster.

## 6. Runtime Notes

- The application window is managed by SDL2 instead of GLFW on Android.
- Touch input and audio playback are automatically handled by SDL2.
- Vulkan validation layers are typically disabled on Android devices.

## 7. Known Limitations

This porting guide provides only the minimal steps to compile the application on
Android. Additional work may be required to fully integrate Android-specific
lifecycle handling, permissions, and resource management.

