# MotionCam Player

This project builds a desktop player for MotionCam `.mcraw` files.

## Building

1. Install the **Vulkan SDK** and ensure `glslangValidator` is in `PATH`.
2. Install [**vcpkg**](https://github.com/microsoft/vcpkg) and bootstrap it.
    Make sure it resides at `C:/dev/vcpkg` (or adjust the toolchain path).
    Install dependencies using:
    ```cmd
    vcpkg install pkgconf "ffmpeg[avcodec,avformat,avutil,swscale,swresample]" glfw3 sdl2
    ```
    Ensure `pkgconf` is in your `PATH` so CMake can locate `pkg-config`.
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
will fail if the libraries cannot be found via vcpkg. When running the
application on Windows, ensure the FFmpeg runtime DLLs are available. A simple
approach is to copy them from
`C:/dev/vcpkg/installed/x64-windows/bin` (e.g. `avcodec-61.dll`,
`avformat-61.dll`, `avutil-59.dll`, `swscale-8.dll`, `swresample-5.dll`,
`avfilter-10.dll`) to the output directory next to the executable. The
`avfilter-10.pdb` file may also be copied for debugging symbols.

## ProRes Export

- Right-click anywhere in the player window to open the context menu.
- Choose **Export to ProRes** and select a `.mov` file path.
- Encoding uses FFmpeg linked via vcpkg. Make sure the libraries are installed in your vcpkg instance.
- Progress is shown in a modal popup while the encoding thread runs.
- A separate log file `prores_export_log.txt` is written in the logs directory
  alongside the regular application log for troubleshooting export issues.

