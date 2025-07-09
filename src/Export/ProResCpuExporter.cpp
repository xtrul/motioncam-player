#include "Export/ProResCpuExporter.h"
#include "Decoder/DecoderWrapper.h"
#include "Utils/ColorPipelineCPU.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"
#include <filesystem>

ProResCpuExporter::ProResCpuExporter() = default;
ProResCpuExporter::~ProResCpuExporter() { join(); }

bool ProResCpuExporter::start(const std::string& path, const std::string& outMov,
                              DecoderWrapper* decoder, Renderer_VK*,
                              AudioController*) {
    if (m_running.load()) return false;
    m_path = path;
    m_out = outMov;
    m_decoder = decoder;
    m_running.store(true);
    m_thread = std::thread(&ProResCpuExporter::run, this);
    return true;
}

void ProResCpuExporter::join() {
    if (m_thread.joinable()) m_thread.join();
    m_running.store(false);
}

void ProResCpuExporter::run() {
    av_log_set_level(AV_LOG_ERROR);
    if (!m_decoder) { m_running.store(false); return; }
    auto* dec = m_decoder->getDecoder();
    const auto& frames = dec->getFrames();
    if (frames.empty()) { LogProRes("[ProResCPU] No frames to export"); m_running.store(false); return; }

    RawBytes raw; nlohmann::json meta;
    dec->loadFrame(frames[0], raw, meta);
    int width = meta.value("width",0); int height = meta.value("height",0);
    if(width<=0||height<=0){ LogProRes("[ProResCPU] Invalid frame dimensions"); m_running.store(false); return; }

    const AVCodec* vcodec = avcodec_find_encoder_by_name("prores_ks");
    if(!vcodec){ LogProRes("[ProResCPU] prores_ks encoder not found"); m_running.store(false); return; }
    AVFormatContext* fmt = nullptr;
    if(avformat_alloc_output_context2(&fmt,nullptr,nullptr,m_out.c_str())<0 || !fmt){ LogProRes("[ProResCPU] avformat_alloc_output_context2 failed"); m_running.store(false); return; }
    AVStream* vstream = avformat_new_stream(fmt,nullptr);
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    vctx->codec_id=vcodec->id; vctx->codec_type=AVMEDIA_TYPE_VIDEO; vctx->pix_fmt=AV_PIX_FMT_YUV422P10LE;
    vctx->width=width; vctx->height=height; vctx->time_base={1,24};
    av_opt_set_int(vctx,"thread_type",FF_THREAD_SLICE,0);
    if(fmt->oformat->flags & AVFMT_GLOBALHEADER) vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if(avcodec_open2(vctx,vcodec,nullptr)<0){ LogProRes("[ProResCPU] avcodec_open2 failed"); avformat_free_context(fmt); m_running.store(false); return; }
    avcodec_parameters_from_context(vstream->codecpar,vctx); vstream->time_base=vctx->time_base;
    if(!(fmt->oformat->flags & AVFMT_NOFILE)){ if(avio_open(&fmt->pb,m_out.c_str(),AVIO_FLAG_WRITE)<0){ LogProRes("[ProResCPU] avio_open failed"); avcodec_free_context(&vctx); avformat_free_context(fmt); m_running.store(false); return; } }
    if(avformat_write_header(fmt,nullptr)<0){ LogProRes("[ProResCPU] write_header failed"); if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb); avcodec_free_context(&vctx); avformat_free_context(fmt); m_running.store(false); return; }

    CPUColorParams params{}; params.width=width; params.height=height;
    params.blackLevel=0; params.whiteLevel=65535; params.cfaType=0;

    SwsContext* sws = sws_getContext(width,height,AV_PIX_FMT_RGB24,width,height,AV_PIX_FMT_YUV422P10LE,SWS_BILINEAR,nullptr,nullptr,nullptr);
    AVFrame* frame = av_frame_alloc();
    frame->format=vctx->pix_fmt; frame->width=width; frame->height=height; av_frame_get_buffer(frame,32);
    std::vector<uint8_t> rgbBuf; AVPacket pkt{};
    for(size_t i=0;i<frames.size();++i){
        dec->loadFrame(frames[i],raw,meta);
        convertRawToRGB24(asU16(raw),params,rgbBuf,1);
        const uint8_t* src[1]={rgbBuf.data()}; int stride[1]={width*3};
        sws_scale(sws,src,stride,0,height,frame->data,frame->linesize);
        frame->pts=i;
        if(avcodec_send_frame(vctx,frame)==0){ while(avcodec_receive_packet(vctx,&pkt)==0){ pkt.stream_index=vstream->index; pkt.duration=1; pkt.pts=av_rescale_q(pkt.pts,vctx->time_base,vstream->time_base); pkt.dts=pkt.pts; av_interleaved_write_frame(fmt,&pkt); av_packet_unref(&pkt);} }
    }
    avcodec_send_frame(vctx,nullptr); while(avcodec_receive_packet(vctx,&pkt)==0){ pkt.stream_index=vstream->index; pkt.duration=1; pkt.pts=av_rescale_q(pkt.pts,vctx->time_base,vstream->time_base); pkt.dts=pkt.pts; av_interleaved_write_frame(fmt,&pkt); av_packet_unref(&pkt); }
    av_write_trailer(fmt);
    if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    avcodec_free_context(&vctx); avformat_free_context(fmt); sws_freeContext(sws); av_frame_free(&frame);
    m_running.store(false);
}
