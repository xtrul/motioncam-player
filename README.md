# MotionCam Player

This project builds a desktop player for MotionCam `.mcraw` files.

## Building

1. Install the **Vulkan SDK** and ensure `glslangValidator` is in `PATH`.
2. Install [**vcpkg**](https://github.com/microsoft/vcpkg) and bootstrap it.
    Make sure it resides at `C:/dev/vcpkg` (or adjust the toolchain path).
    Install dependencies using:
    ```cmd
    vcpkg install "ffmpeg[avcodec,avformat,avutil,swscale,swresample]" glfw3 sdl2
    ```
3. Generate the Visual Studio project using the vcpkg toolchain file:
    ```cmd
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
          -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
    ```
4. Build:
    ```cmd
    cmake --build build --config Release
    ```

FFmpeg is required for the "Export to ProRes" feature. The CMake configuration
will fail if the libraries cannot be found via vcpkg.

## ProRes Export

- Right-click anywhere in the player window to open the context menu.
- Choose **Export to ProRes** and select a `.mov` file path.
- Encoding uses FFmpeg linked via vcpkg. Make sure the libraries are installed in your vcpkg instance.
- Progress is shown in a modal popup while the encoding thread runs.
