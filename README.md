# MotionCam Player

This project builds a desktop player for MotionCam `.mcraw` files.

## Building

1. Install the **Vulkan SDK** and ensure `glslangValidator` is in `PATH`.
2. Install **pkg-config** so CMake can locate external libraries.
   - On Windows a simple method is installing [MSYS2](https://www.msys2.org) and running `pacman -S pkgconf`.
   - On Linux install your distribution's `pkg-config` package.
   - Ensure the directory containing `pkg-config` is added to your `PATH`.
3. Obtain **FFmpeg development libraries** with pkg-config files.
   - On Windows grab a prebuilt archive from [gyan.dev/ffmpeg](https://www.gyan.dev/ffmpeg/builds/), then set `PKG_CONFIG_PATH` to its `lib/pkgconfig` folder.
   - On Linux install `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev`.
4. Generate the Visual Studio project:
   ```cmd
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   ```
5. Build:
   ```cmd
   cmake --build build --config Release
   ```

FFmpeg is required for the "Export to ProRes" feature. CMake will abort if the
libraries cannot be found via pkg-config.
