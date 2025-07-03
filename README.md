# MotionCam Player

This project builds a desktop player for MotionCam `.mcraw` files.

## Building

1. Install the **Vulkan SDK** and ensure `glslangValidator` is in `PATH`.
2. Install **pkg-config** so CMake can locate external libraries.
   - On Windows a simple method is installing [MSYS2](https://www.msys2.org) and running `pacman -S pkgconf`.
   - Ensure the MSYS2 `usr\bin` directory containing `pkg-config.exe` is added to your `PATH`.
3. Obtain **FFmpeg development libraries** with pkg-config files.
   - Prebuilt Windows archives are available from [gyan.dev/ffmpeg](https://www.gyan.dev/ffmpeg/builds/).
   - Extract the archive and set the environment variable `PKG_CONFIG_PATH` to the `lib/pkgconfig` directory of the extracted FFmpeg build.
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
