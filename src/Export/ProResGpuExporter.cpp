#include "Export/ProResGpuExporter.h"
#include "Graphics/GpuYuvConverter.h"
#include "Export/ProResCpuExporter.h"
#include "Decoder/DecoderWrapper.h"
#include "Graphics/Renderer_VK.h"
#include "Audio/AudioController.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"
#include "Utils/OrientationUtils.h"
#include <filesystem>

ProResGpuExporter::ProResGpuExporter() = default;
ProResGpuExporter::~ProResGpuExporter() {
    join();
    if(m_hwFrames) av_buffer_unref(&m_hwFrames);
    if(m_hwDevice) av_buffer_unref(&m_hwDevice);
}

bool ProResGpuExporter::start(const std::string& path, const std::string& outMov,
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
    m_thread = std::thread(&ProResGpuExporter::run, this);
    return true;
}

void ProResGpuExporter::join(){
    if(m_thread.joinable()) m_thread.join();
    m_running.store(false);
}

void ProResGpuExporter::run(){
    av_log_set_level(AV_LOG_ERROR);
    LogProRes("[ProResExporter] Thread started");
    if(!m_decoder || !m_renderer){ m_running.store(false); return; }
    try {

    auto* dec = m_decoder->getDecoder();
    const auto& frames = dec->getFrames();
    if(frames.empty()){ LogProRes("[ProResExporter] No frames to export"); m_running.store(false); return; }

    RawBytes raw; nlohmann::json meta;
    dec->loadFrame(frames[0], raw, meta);
    int width = meta.value("width",0); int height = meta.value("height",0);
    if(width<=0||height<=0){ LogProRes("[ProResExporter] Invalid frame dimensions"); m_running.store(false); return; }
    m_converter->init(m_renderer,width,height,2);

    if(av_hwdevice_ctx_create(&m_hwDevice, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0) < 0){
        LogProRes("[ProResExporter] av_hwdevice_ctx_create failed");
        m_running.store(false); return;
    }
    m_hwFrames = av_hwframe_ctx_alloc(m_hwDevice);
    if(!m_hwFrames){ LogProRes("[ProResExporter] av_hwframe_ctx_alloc failed"); m_running.store(false); return; }
    {
        AVHWFramesContext* f = (AVHWFramesContext*)m_hwFrames->data;
        f->format = AV_PIX_FMT_VULKAN;
        f->sw_format = AV_PIX_FMT_YUV422P10LE;
        f->width = width;
        f->height = height;
        f->initial_pool_size = m_converter->getRingSize();
        if(av_hwframe_ctx_init(m_hwFrames) < 0){
            LogProRes("[ProResExporter] av_hwframe_ctx_init failed");
            m_running.store(false); return;
        }
    }

    const AVCodec* vcodec = avcodec_find_encoder_by_name("prores_ks");
    if(!vcodec){ LogProRes("[ProResExporter] prores_ks encoder not found"); m_running.store(false); return; }
    AVFormatContext* fmt = nullptr;
    if(avformat_alloc_output_context2(&fmt,nullptr,nullptr,m_out.c_str())<0 || !fmt){ LogProRes("[ProResExporter] avformat_alloc_output_context2 failed"); m_running.store(false); return; }
    AVStream* vstream = avformat_new_stream(fmt,nullptr);
    AVCodecContext* vctx = avcodec_alloc_context3(vcodec);
    vctx->codec_id = vcodec->id; vctx->codec_type=AVMEDIA_TYPE_VIDEO; vctx->pix_fmt=AV_PIX_FMT_VULKAN;
    vctx->width=width; vctx->height=height; vctx->time_base={1,24};
    av_opt_set_int(vctx,"thread_type",FF_THREAD_SLICE,0); av_opt_set_int(vctx,"slices",8,0);
    if(fmt->oformat->flags & AVFMT_GLOBALHEADER) vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    vctx->hw_frames_ctx = av_buffer_ref(m_hwFrames);
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

        int idx = conv.convert(meta,0,65535,0);
        VkImage yuv = conv.getImage(idx);

        AVFrame* hwFrame = av_frame_alloc();
        hwFrame->format = AV_PIX_FMT_VULKAN;
        hwFrame->width = width;
        hwFrame->height = height;
        if(av_hwframe_get_buffer(m_hwFrames, hwFrame, 0) < 0){
            LogProRes("[ProResExporter] hwframe_get_buffer failed");
            av_frame_free(&hwFrame);
            break;
        }
        AVVkFrame* vkf = (AVVkFrame*)hwFrame->data[0];
        vkf->image[0] = yuv;
        vkf->image_layout[0] = VK_IMAGE_LAYOUT_GENERAL;

        AVFrame* frame = av_frame_alloc();
        frame->format = AV_PIX_FMT_YUV422P10LE;
        frame->width = width;
        frame->height = height;
        if(av_hwframe_transfer_data(frame, hwFrame, 0) < 0){
            LogProRes("[ProResExporter] hwframe_transfer_data failed");
        }
        av_frame_free(&hwFrame);

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
    if(m_hwFrames) av_buffer_unref(&m_hwFrames);
    if(m_hwDevice) av_buffer_unref(&m_hwDevice);
    m_running.store(false);
    return;
    } catch (...) {
        LogProRes("[Exporter] Vulkan error - falling back to CPU path");
        ProResCpuExporter cpu;
        cpu.start(m_path, m_out, m_decoder, nullptr, m_audio);
        cpu.join();
        m_running.store(false);
    }
}
