#include "Export/DnxhrExporter.h"
#include <cassert>
#include <string>

std::string g_AppBasePath;

int main() {
    AVRational tb{1,25};
    AVCodecContext* ctx = create_dnxhr_hqx_encoder(256, 128, tb);
    assert(ctx);
    assert(ctx->profile == FF_PROFILE_DNXHR_HQX);
    AVFrame* frame = av_frame_alloc();
    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;
    av_frame_get_buffer(frame, 32);
    int ret = avcodec_send_frame(ctx, frame);
    assert(ret >= 0);
    AVPacket pkt;
    av_init_packet(&pkt);
    ret = avcodec_receive_packet(ctx, &pkt);
    assert(ret == 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    if(ret == 0) av_packet_unref(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return 0;
}
