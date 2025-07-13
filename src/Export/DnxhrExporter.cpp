#include "Export/DnxhrExporter.h"
#include "Utils/DebugLog.h"
#include <string>
#include <libavutil/pixdesc.h>

#if CONFIG_DNXHR_ENCODER

AVCodecContext* create_dnxhr_hqx_encoder(int width, int height, AVRational time_base) {
    const AVCodec* codec = avcodec_find_encoder_by_name("dnxhd");
    if(!codec) {
        LogDnxhr("[DNxHR] encoder dnxhd not found");
        return nullptr;
    }
    assert(width % 4 == 0 && "DNxHR width must be multiple of 4");
    assert(height % 2 == 0 && "DNxHR height must be even");

    AVCodecContext* c = avcodec_alloc_context3(codec);
    c->codec_id = codec->id;
    c->codec_type = AVMEDIA_TYPE_VIDEO;
    c->pix_fmt = AV_PIX_FMT_YUV422P10LE;
    c->profile = FF_PROFILE_DNXHR_HQX;
    c->width = width;
    c->height = height;
    c->time_base = time_base;
    c->framerate = av_inv_q(time_base);

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "profile", "dnxhr_hqx", 0);
    int err = avcodec_open2(c, codec, &opts);
    char errbuf[128];
    if(err < 0) {
        av_strerror(err, errbuf, sizeof(errbuf));
        LogDnxhr(std::string("[DNxHR] avcodec_open2 failed: ") + errbuf);
        av_dict_free(&opts);
        avcodec_free_context(&c);
        return nullptr;
    }
    av_dict_free(&opts);
    uint8_t* prof = nullptr;
    av_opt_get(c->priv_data, "profile", 0, &prof);
    LogDnxhr(std::string("[DNxHR] DNxHR profile: ") +
             (prof ? reinterpret_cast<char*>(prof) : "?") +
             ", pix_fmt: " + av_get_pix_fmt_name(c->pix_fmt) +
             ", bps: " + std::to_string(c->bits_per_raw_sample));
    if(prof) av_freep(&prof);
    LogDnxhr("[DNxHR] Encode loop starting…");
    return c;
}

#else

AVCodecContext* create_dnxhr_hqx_encoder(int, int, AVRational) {
    LogDnxhr("[DNxHR] CONFIG_DNXHR_ENCODER not enabled");
    return nullptr;
}

#endif // CONFIG_DNXHR_ENCODER
