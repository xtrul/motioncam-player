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


```
ffmpeg -y -f rawvideo -pix_fmt rgba -s WxH -r <fps> -i - \
       -f s16le -ar <sampleRate> -ac <channels> -i - \
       -c:v prores_ks output.mov
```

FFmpeg 4.0 or newer is recommended.

