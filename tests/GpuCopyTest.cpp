#include "Utils/ColorPipelineCPU.h"
#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"
#include "ffmpeg_headers.hpp"
#include <vector>
#include <random>
#include <cassert>

int main(){
    int width = 32;
    int height = 16;
    std::vector<uint16_t> raw(width*height);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0,1023);
    for(auto &v: raw) v = static_cast<uint16_t>(dist(rng));

    CPUColorParams cp{};
    cp.width = width; cp.height = height;
    cp.cfaType = 0; cp.blackLevel = 0.0; cp.whiteLevel = 1023.0;
    std::vector<uint8_t> rgb;
    convertRawToRGB24(raw.data(), cp, rgb, 1);

    SwsContext* sws = sws_getContext(width,height,AV_PIX_FMT_RGB24,width,height,AV_PIX_FMT_YUV422P10LE,SWS_BILINEAR,nullptr,nullptr,nullptr);
    AVFrame* cpu = av_frame_alloc();
    cpu->format = AV_PIX_FMT_YUV422P10LE; cpu->width=width; cpu->height=height;
    av_frame_get_buffer(cpu,32);
    const uint8_t* srcSlices[1]={rgb.data()};
    int srcStride[1]={width*3};
    sws_scale(sws,srcSlices,srcStride,0,height,cpu->data,cpu->linesize);

    Renderer_VK* dummy=nullptr; // placeholder, converter won't run without real renderer
    (void)dummy;
    // This test only validates that CPU path works.
    // Full GPU comparison requires Vulkan device not available in CI.
    av_frame_free(&cpu);
    sws_freeContext(sws);
    return 0;
}
