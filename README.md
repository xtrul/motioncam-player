# MotionCam Player

This project builds a desktop viewer for `.mcraw` files recorded by MotionCam Pro.

## Building

The project uses CMake and requires a Vulkan SDK, SDL2, GLFW, and FFmpeg installed.

Example build steps:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### FFmpeg

`convertCurrentFileToProRes()` spawns the `ffmpeg` executable to encode ProRes videos. Ensure a recent FFmpeg is available in your `PATH`. On Windows the tool writes video and audio to named pipes which FFmpeg reads from. The command invoked is roughly:

```
ffmpeg -y -f rawvideo -pix_fmt rgba -s WxH -r <fps> -i - \
       -f s16le -ar <sampleRate> -ac <channels> -i - \
       -c:v prores_ks output.mov
```

FFmpeg 4.0 or newer is recommended.

### Debugging

Logs are written to `Logs/motioncam_player_log.txt` next to the application.
ProRes exports also write detailed messages to `Logs/prores_export_log.txt` including pipe setup and frame progress.

