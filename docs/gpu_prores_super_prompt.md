Below is a ready-to-paste “super-prompt” you can give to ChatGPT / Copilot / Code-Llama to generate the second, GPU-accelerated ProRes path while keeping the existing CPU exporter unchanged.

Project snapshot
----------------
• **CPU exporter (already works)**
  – Lives in `Export/ProResCpuExporter.cpp` and calls `ColorPipelineCPU.cpp` to
    convert RAW → RGB → YUV422P10 in system RAM, then feeds FFmpeg’s
    `prores_ks` encoder.
  – CL flag: `--export-prores-cpu <in.mcraw> <out.mov>`

• **GPU playback path**
  – `Graphics/Renderer_VK.cpp` + helpers do demosaic, colour-matrix, tone-map
    on GPU and display to the swap-chain in real time.
  – `Graphics/VulkanHelpers.cpp` encapsulates command-buffers & image
    allocation.
  – `Decoder/DecoderWrapper.cpp` delivers RAW16 frames and metadata.

Objective
---------
* **Keep** the current CPU-only converter as the default & reference path.
* **Add** a *second* path, “GPU ProRes”, that:
  ▸ Re-uses Vulkan compute to pack YUV 4:2:2 10-bit on-GPU.
  ▸ Streams frames directly into FFmpeg via the Vulkan hw-frames API.
  ▸ Achieves ≥ 24 fps for 4 K.
* Expose both paths in GUI (radio buttons) and CLI
  (`--export-prores <cpu|gpu>`).
* If the GPU path hits an error (device lost, unsupported format),
  **auto-fallback** to the CPU path and emit a log warning.

High-level design
-----------------
```
                    DecoderWrapper
                     (RAW16+meta)
                          │
          ┌───────────────┴────────────────┐
          │                                │
     CPU path                     GPU path (new)
 ──────────────────         ──────────────────────────────
ColorPipelineCPU.cpp 1. raw_to_yuv422.comp (new)
RGB16F → YUV422P10 RAW16 → YUV422P10 in VkImage

System RAM buffer 2. AVHWFramesContext (Vulkan)

prores_ks 3. prores_ks (same settings)

mux 4. mux
```

Implementation checklist
------------------------
| **Task** | **File / location** |
|----------|---------------------|
| 1. **`GpuYuvConverter` class** – wraps a ring of `VK_FORMAT_G10X6_B10X6_R10X6_2PACK16` images, dispatches new shader, signals a `VkSemaphore` for FFmpeg. | `Graphics/GpuYuvConverter.{h,cpp}` |
| 2. **Shader** `raw_to_yuv422.comp` + SPIR-V build rule. Re-use uniforms & LUTs from existing demosaic shader. | `shaders/` |
| 3. **`ProResGpuExporter` class** – owns: `GpuYuvConverter`, FFmpeg HW device (`av_hwdevice_ctx_create("vulkan")`), frames ctx, audio track, mux. | `Export/ProResGpuExporter.{h,cpp}` |
| 4. **Factory** `ProResExporter::create(mode)` returns CPU or GPU implementation. | `Export/ProResExporterFactory.cpp` |
| 5. **CLI**: extend `main.cpp` to parse `--export-prores [cpu|gpu]`. Default = `cpu`. | `main.cpp` |
| 6. **GUI**: in `Gui/GuiRender.cpp` add radio buttons **CPU** / **GPU** under “Convert to ProRes…”. | `Gui/GuiRender.cpp` |
| 7. **Fallback**: catch any Vulkan / FFmpeg error in `ProResGpuExporter::run()`; log warning; call CPU exporter. | place inside `ProResGpuExporter.cpp` |
| 8. **CMake**: option `ENABLE_GPU_PRORES`; link FFmpeg with `--enable-vulkan`. | `CMakeLists.txt` |

Key FFmpeg call-sequence for GPU path
-------------------------------------
```cpp
// 1. HW device from existing VkDevice
av_hwdevice_ctx_create(&hwDevCtx, AV_HWDEVICE_TYPE_VULKAN,
                       "vk:0", nullptr, 0);

// 2. Wrap our YUV ring into an AVHWFramesContext
AVHWFramesContext* fctx = av_hwframe_ctx_alloc(hwDevCtx);
fctx->format            = AV_PIX_FMT_VULKAN;
fctx->sw_format         = AV_PIX_FMT_YUV422P10;
fctx->width             = 4096;
fctx->height            = 3072;
fctx->initial_pool_size = RING_SIZE;
// fill fctx->pool with VkImage handles + semaphores…
av_hwframe_ctx_init((AVBufferRef*)fctx);

// 3. Encode
avcodec_find_encoder_by_name("prores_ks");
av_opt_set_int(codecCtx, "profile", 3, 0);   // 422 HQ
av_opt_set(codecCtx, "qscale", "9", 0);
av_opt_set_int(codecCtx, "thread_type", FF_THREAD_SLICE, 0);
av_opt_set_int(codecCtx, "slices", 8, 0);
```

Shader outline (raw_to_yuv422.comp)
```glsl
layout(local_size_x = 32, local_size_y = 32) in;

layout(set = 0, binding = 0) readonly buffer Raw16   { uint data[]; } raw;
layout(set = 0, binding = 1) writeonly  image2D yuv;

void main() {
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    vec3  rgb = demosaic_and_ccm(raw, px);   // reuse existing lib
    vec3  yuv = rgb_to_yuv_709(rgb);
    pack422_10bit(yuv, yuvImg, px);          // write to 2PACK16
}
```

Performance targets (per 4 K frame)
StageBudgetNotes
GPU RAW→YUV≤ 8 msMeasured inside shader timestamp query
Vk→FFmpeg transfer2 msav_hwframe_transfer_data no-copy
ProRes encode4 msprores_ks, 16 CPU threads
Disk mux< 1 msNVMe recommended
Total≤ 15 ms ≈ 27 fps

Usage examples
GUI → File → Convert to ProRes… → pick GPU → run.

CLI
CPU path (reference)
```bash
app.exe --export-prores cpu in.mcraw out.mov
```
GPU path (fast)
```bash
app.exe --export-prores gpu in.mcraw out.mov
```
If the GPU pipeline fails, the log will show:
```css
[Exporter] Vulkan error VK_ERROR_DEVICE_LOST – falling back to CPU path
```

Acceptance test
```
tests/export_gpu_fallback.sh
```
Convert with gpu mode → expect ≤ 7 s wall-time, ≥ 24 fps logged.

Force a device-lost (mock) → verify fallback to CPU and a green exit status.

ffprobe confirms codec prores, profile 422HQ, 10-bit, 168 frames.

Deliverables
GpuYuvConverter, ProResGpuExporter, shader, factory, GUI update, CLI flag.

Updated CMakeLists.txt.

The test script above.

Code comments following the verbose LogToFile style in Renderer_VK.cpp.

**What the prompt does**

* **Keeps the CPU exporter untouched** – still the default, still your benchmark.
* **Adds a second, GPU-accelerated exporter** with clear file locations, API calls, error handling, build flags, performance budget, GUI & CLI hooks.
* Explains exactly **how to wire Vulkan images into FFmpeg** and how to **fallback** automatically if anything GPU-side goes wrong.

You can tweak naming, budgets, or GUI wording, but this prompt should give the code-gen model everything it needs to generate a working dual-path ProRes exporter.
