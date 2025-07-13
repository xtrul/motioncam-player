#include "ffmpeg_headers.hpp"
#include <cassert>

int main() {
#ifdef CONFIG_DNXHR_ENCODER
    const AVCodec *codec = avcodec_find_encoder_by_name("dnxhd");
    if (!codec) return 1;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    ctx->width = 64;
    ctx->height = 32;
    ctx->pix_fmt = AV_PIX_FMT_YUV422P10LE;
    ctx->time_base = {1,25};
    ctx->framerate = {25,1};
    ctx->profile = FF_PROFILE_DNXHR_HQX;
    ctx->bit_rate = 1000000;
    int err = avcodec_open2(ctx, codec, nullptr);
    if (err < 0) return 1;
    assert(ctx->profile == FF_PROFILE_DNXHR_HQX);
    avcodec_free_context(&ctx);
#endif
    return 0;
}
