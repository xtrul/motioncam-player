# MotionCam Converter

This project builds a desktop converter for MotionCam `.mcraw` files.

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

FFmpeg is required for the "Export to ProRes" feature. When detected, the build
system defines the `ENABLE_PRORES_EXPORT` flag. It will link using the
`FFMPEG::` imported targets provided by vcpkg when they are available, falling
back to the `${FFMPEG_LIBRARIES}` list otherwise. The CMake configuration will
fail if the libraries cannot be found via vcpkg. When running the application on
Windows, ensure the FFmpeg runtime DLLs are available. A simple
approach is to copy them from
`C:/dev/vcpkg/installed/x64-windows/bin` (e.g. `avcodec-61.dll`,
`avformat-61.dll`, `avutil-59.dll`, `swscale-8.dll`, `swresample-5.dll`,
`avfilter-10.dll`) to the output directory next to the executable. The
`avfilter-10.pdb` file may also be copied for debugging symbols.

If the **Export to ProRes** option is grayed out in the application, it means
the binary was built without FFmpeg support. Re-run CMake and check that the
configure step prints "FFmpeg found via vcpkg, enabling ProRes export". When
this message appears, the `ENABLE_PRORES_EXPORT` definition is added and the
runtime DLLs will be copied automatically.

## Usage

The application uses a single dockspace window. The video is drawn inside the
**Preview** panel which can be resized or moved like any other ImGui window.
Clips start in a paused state so you can inspect the first frame before
beginning playback. Use the **Play** button or press the spacebar to toggle
playback.

## ProRes Export

- Right-click anywhere in the player window to open the context menu.
- Choose **Export to ProRes** and select a `.mov` file path.
- Encoding uses FFmpeg linked via vcpkg. Make sure the libraries are installed in your vcpkg instance.
 - Progress is shown in a modal popup while the encoding thread runs.
 - The export uses the `prores_ks` encoder with frame threading and sets
   `slice_count` to the number of CPU cores. The conversion and scaling stages
   run on the same thread count via libswscale. The log records all thread
   counts and prints progress every 50 frames with elapsed time. A timing
   summary of decode, color conversion, scaling and encode steps is written at
   the end so you can locate bottlenecks. Average per-frame times and each
   stage's percentage of the total duration are also printed.
- Frame rate is derived from the clip's timestamps so the output is constant frame rate.
- If the clip contains audio, 16‑bit PCM is muxed into the `.mov` container.
- A `prores_export_log.txt` file is written inside the `Logs` folder for troubleshooting.
- The export pipeline relies on the `blackLevel`, `whiteLevel`, `asShotNeutral`, and
  `ColorMatrix*` metadata found in the `.mcraw` file. Incorrect values can result
  in an image that appears completely black. The log file lists the parsed
  numbers so you can verify them.
- Encoder parameters, metadata values, and the detected CFA pattern are written
  to `prores_export_log.txt` so you can verify exactly how the clip was processed.
- If the resulting `.mov` only contains audio, check the log for
  **"No video frames encoded"**. This means the frame conversion failed and no
  video packets were written.
- You can override the detected CFA pattern at runtime by pressing the number
  keys `1`‑`4` which map to **BGGR**, **RGGB**, **GBRG**, and **GRBG** respectively.

### GPU ProRes Conversion

- Choose **Convert to ProRes (GPU)** from the context menu to run the RAW→YUV conversion on the GPU.
- The log prints `MODE = GPU (Vulkan hw_frames)` when this path is used.
- If initialization of Vulkan hardware frames fails it falls back to the CPU path automatically.
- When GPU processing is active additional `[GPU]` log lines confirm the compute pipeline ran.

## DNxHR Export

- The context menu also provides **Export to DNxHR** and **Convert to DNxHR (GPU)** which operate similarly to the ProRes paths.
- Choose a `.mxf` file path when prompted. The encoder uses the MXF container.
- Supports any even resolution. Width should be a multiple of 4 and height even.
  Non-conforming sizes emit a warning but encoding still proceeds.
- Uses the DNxHR **HQX** profile (10‑bit 4:2:2) with the MXF container.
- Progress and error reporting mirror the ProRes export and are written to the same log file.

## HEVC Export (AMD)

- Choose **Convert to HEVC (GPU, AMD 10-bit HQ)** to encode using the `hevc_amf` hardware encoder.
- Encoding uses the `transcoding` usage preset with the `slow` quality setting and 10‑bit P010 frames.
- The encoder runs in high quality **ABR** mode at ~250&nbsp;Mbps with an IDR frame every frame (GOP&nbsp;= 1).
- BT.709 color metadata is written so DaVinci Resolve interprets the clip correctly.
- If the AMD encoder is unavailable the export aborts and logs to `hevc_export_log.txt`.
- The log file lists these settings so you can verify the parameters used.

See `docs/Converter_Enhancement_Spec.md` for proposed improvements to the MotionCam Converter add-on.
