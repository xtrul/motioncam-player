#include "Export/RawDnxhrWriter.h"
#include "Utils/DebugLog.h"
extern "C" {
#include <libavformat/avformat.h>
}

bool writeRawDnxhr(const std::vector<uint8_t>& frame,
                   int width, int height,
                   const std::string& path,
                   bool asMxf)
{
    AVFormatContext* fmt = nullptr;
    const char* fmtName = asMxf ? "mxf" : "dnxhd";
    if(avformat_alloc_output_context2(&fmt, nullptr, fmtName, path.c_str()) < 0 || !fmt){
        LogDnxhr("[RawDnxhrWriter] avformat_alloc_output_context2 failed");
        return false;
    }
    AVStream* st = avformat_new_stream(fmt, nullptr);
    if(!st){
        LogDnxhr("[RawDnxhrWriter] avformat_new_stream failed");
        avformat_free_context(fmt);
        return false;
    }
    st->codecpar->codec_id = AV_CODEC_ID_DNXHD;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width = width;
    st->codecpar->height = height;
    st->time_base = {1,25};
    if(!(fmt->oformat->flags & AVFMT_NOFILE)){
        if(avio_open(&fmt->pb, path.c_str(), AVIO_FLAG_WRITE) < 0){
            LogDnxhr("[RawDnxhrWriter] avio_open failed");
            avformat_free_context(fmt);
            return false;
        }
    }
    if(avformat_write_header(fmt, nullptr) < 0){
        LogDnxhr("[RawDnxhrWriter] avformat_write_header failed");
        if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        return false;
    }
    AVPacket pkt{};
    av_init_packet(&pkt);
    pkt.data = const_cast<uint8_t*>(frame.data());
    pkt.size = static_cast<int>(frame.size());
    pkt.stream_index = st->index;
    pkt.pts = 0;
    pkt.dts = 0;
    pkt.duration = 1;
    int ret = av_write_frame(fmt, &pkt);
    av_write_trailer(fmt);
    if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    avformat_free_context(fmt);
    if(ret < 0){
        LogDnxhr("[RawDnxhrWriter] av_write_frame failed");
        return false;
    }
    LogDnxhr(std::string("[RawDnxhrWriter] wrote ") + path);
    return true;
}

bool verifyDnxhrFile(const std::string& path)
{
    AVFormatContext* fmt = nullptr;
    bool ok = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) >= 0;
    if(ok) avformat_close_input(&fmt);
    else LogDnxhr("[RawDnxhrWriter] avformat_open_input failed");
    return ok;
}

