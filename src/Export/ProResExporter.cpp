#include "Export/ProResExporter.h"
#include "Graphics/GpuYuvConverter.h"
#include "Decoder/DecoderWrapper.h"
#include "Graphics/Renderer_VK.h"
#include "Audio/AudioController.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"
#include "Utils/OrientationUtils.h"
#include <filesystem>

ProResExporter::ProResExporter() = default;
ProResExporter::~ProResExporter() { join(); }

bool ProResExporter::start(const std::string& path, const std::string& outMov,
                           DecoderWrapper* decoder, Renderer_VK* renderer,
                           AudioController* audio){
    if(m_running.load()) return false;
    m_path = path;
    m_out = outMov;
    m_decoder = decoder;
    m_renderer = renderer;
    m_audio = audio;
    m_converter = std::make_unique<GpuYuvConverter>();
    m_running.store(true);
    m_thread = std::thread(&ProResExporter::run, this);
    return true;
}

void ProResExporter::join(){
    if(m_thread.joinable()) m_thread.join();
    m_running.store(false);
}

void ProResExporter::run(){
    av_log_set_level(AV_LOG_ERROR);
    LogProRes("[ProResExporter] Thread started");
    if(!m_decoder || !m_renderer){ m_running.store(false); return; }

    auto* dec = m_decoder->getDecoder();
    const auto& frames = dec->getFrames();
    if(frames.empty()){ LogProRes("[ProResExporter] No frames to export"); m_running.store(false); return; }

    RawBytes raw; nlohmann::json meta;
    dec->loadFrame(frames[0], raw, meta);
    int width = meta.value("width",0); int height = meta.value("height",0);
    if(width<=0||height<=0){ LogProRes("[ProResExporter] Invalid frame dimensions"); m_running.store(false); return; }
    m_converter->init(m_renderer,width,height,2);

    const AVCodec* vcodec = avcodec_find_encoder_by_name("prores_ks");
    if(!vcodec){ LogProRes("[ProResExporter] prores_ks encoder not found"); m_running.store(false); return; }
    AVFormatContext* fmt = nullptr;
    if(avformat_alloc_output_context2(&fmt,nullptr,nullptr,m_out.c_str())<0 || !fmt){ LogProRes("[ProResExporter] avformat_alloc_output_context2 failed"); m_running.store(false); return; }
    AVStream* vstream = avformat_new_stream(fmt,nullptr);
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    vctx->codec_id = vcodec->id; vctx->codec_type=AVMEDIA_TYPE_VIDEO; vctx->pix_fmt=AV_PIX_FMT_YUV422P10LE;
    vctx->width=width; vctx->height=height; vctx->time_base={1,24};
    av_opt_set_int(vctx,"thread_type",FF_THREAD_SLICE,0); av_opt_set_int(vctx,"slices",8,0);
    if(fmt->oformat->flags & AVFMT_GLOBALHEADER) vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if(avcodec_open2(vctx,vcodec,nullptr)<0){ LogProRes("[ProResExporter] avcodec_open2 failed"); avformat_free_context(fmt); m_running.store(false); return; }
    avcodec_parameters_from_context(vstream->codecpar,vctx); vstream->time_base=vctx->time_base;
    if(!(fmt->oformat->flags & AVFMT_NOFILE)){ if(avio_open(&fmt->pb,m_out.c_str(),AVIO_FLAG_WRITE)<0){ LogProRes("[ProResExporter] avio_open failed"); avcodec_free_context(&vctx); avformat_free_context(fmt); m_running.store(false); return; } }
    if(avformat_write_header(fmt,nullptr)<0){ LogProRes("[ProResExporter] write_header failed"); if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb); avcodec_free_context(&vctx); avformat_free_context(fmt); m_running.store(false); return; }

    GpuYuvConverter conv; conv.init(m_renderer,width,height,2);

    for(size_t i=0;i<frames.size();++i){
        dec->loadFrame(frames[i], raw, meta);
        VkBufferCreateInfo bci{}; bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size=raw.size(); bci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci{}; aci.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST; aci.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        VkBuffer buf; VmaAllocation alloc; VmaAllocationInfo info{};
        vmaCreateBuffer(m_renderer->m_allocator_p,&bci,&aci,&buf,&alloc,&info);
        memcpy(info.pMappedData, raw.data(), raw.size());
        vmaFlushAllocation(m_renderer->m_allocator_p,alloc,0,VK_WHOLE_SIZE);
        vmaUnmapMemory(m_renderer->m_allocator_p,alloc);
        VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(m_renderer->m_device_p,m_renderer->m_hostSiteCommandPool_p);
        m_renderer->prepareAndUploadFrameData(cmd,0,buf,width,height,meta,0,65535,0,true,OrientationTag::kNormal,false);
        VulkanHelpers::endSingleTimeCommands(m_renderer->m_device_p,m_renderer->m_hostSiteCommandPool_p,m_renderer->m_graphicsQueue_p,cmd);
        vmaDestroyBuffer(m_renderer->m_allocator_p,buf,alloc);

        VkImage yuv = conv.convert(meta,0,65535,0);
        std::vector<uint16_t> gpuBuf; conv.readback(yuv,gpuBuf);

        AVFrame* frame = av_frame_alloc();
        frame->format=vctx->pix_fmt; frame->width=width; frame->height=height; av_frame_get_buffer(frame,32);

        uint16_t* yPlane=(uint16_t*)frame->data[0]; int yStride=frame->linesize[0]/2;
        uint16_t* uPlane=(uint16_t*)frame->data[1]; int uStride=frame->linesize[1]/2;
        uint16_t* vPlane=(uint16_t*)frame->data[2]; int vStride=frame->linesize[2]/2;
        for(int y=0;y<height;++y){
            for(int x=0;x<width;x+=2){
                size_t idx=((size_t)y*width + x)/2*4; uint16_t Y0=gpuBuf[idx]; uint16_t U=gpuBuf[idx+1]; uint16_t Y1=gpuBuf[idx+2]; uint16_t V=gpuBuf[idx+3];
                yPlane[y*yStride + x]=Y0; yPlane[y*yStride + x+1]=Y1; uPlane[y*uStride + x/2]=U; vPlane[y*vStride + x/2]=V;
            }
        }

        frame->pts=i;
        if(avcodec_send_frame(vctx,frame)==0){
            AVPacket pkt{}; while(avcodec_receive_packet(vctx,&pkt)==0){ pkt.stream_index=vstream->index; pkt.duration=1; pkt.pts=av_rescale_q(pkt.pts,vctx->time_base,vstream->time_base); pkt.dts=pkt.pts; av_interleaved_write_frame(fmt,&pkt); av_packet_unref(&pkt); }
        }
        av_frame_free(&frame);
    }

    avcodec_send_frame(vctx,nullptr); AVPacket pkt{}; while(avcodec_receive_packet(vctx,&pkt)==0){ pkt.stream_index=vstream->index; pkt.duration=1; pkt.pts=av_rescale_q(pkt.pts,vctx->time_base,vstream->time_base); pkt.dts=pkt.pts; av_interleaved_write_frame(fmt,&pkt); av_packet_unref(&pkt); }
    av_write_trailer(fmt);
    if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    avcodec_free_context(&vctx); avformat_free_context(fmt);
    conv.cleanup();
    m_running.store(false);
}
