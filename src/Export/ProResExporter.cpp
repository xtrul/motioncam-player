#include "Export/ProResExporter.h"
#include "Graphics/GpuYuvConverter.h"
#include "Decoder/DecoderWrapper.h"
#include "Graphics/Renderer_VK.h"
#include "Audio/AudioController.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"
#include "Utils/OrientationUtils.h"
#include <motioncam/Decoder.hpp>
#include <filesystem>

ProResExporter::ProResExporter() = default;
ProResExporter::~ProResExporter() {
    join();
    if(m_hwFrames) av_buffer_unref(&m_hwFrames);
    if(m_hwDevice) av_buffer_unref(&m_hwDevice);
}

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
    LogProRes("[ProResExport] MODE = GPU (Vulkan hw_frames)");
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
        // ffmpeg >=6.1 uses 'img' and 'layout' fields rather than 'image'
        // and 'image_layout'. Assign the Vulkan image directly so no copy is
        // performed when transferring frames.
        vkf->img[0] = yuv;
        vkf->layout[0] = VK_IMAGE_LAYOUT_GENERAL;

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

    // Encode audio if available
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    AVStream* astream = nullptr; AVCodecContext* actx = nullptr;
    if(acodec && m_audio){
        astream = avformat_new_stream(fmt,nullptr);
        actx = avcodec_alloc_context3(acodec);
        actx->codec_id = acodec->id; actx->sample_rate = 48000;
        av_channel_layout_default(&actx->ch_layout, 2);
        actx->sample_fmt = AV_SAMPLE_FMT_S16; actx->time_base = {1,actx->sample_rate};
        if(fmt->oformat->flags & AVFMT_GLOBALHEADER) actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if(avcodec_open2(actx, acodec, nullptr) >= 0){
            avcodec_parameters_from_context(astream->codecpar, actx);
            astream->time_base = actx->time_base;
            LogProRes("[ProResExporter] Audio encoder initialized");
            motioncam::AudioChunkLoader* loader = m_decoder->makeFreshAudioLoader();
            motioncam::AudioChunk chunk; int64_t audioPts=0;
            while(loader && loader->next(chunk)){
                if(chunk.second.empty()) break;
                AVFrame* af=av_frame_alloc();
                af->format=actx->sample_fmt; av_channel_layout_copy(&af->ch_layout,&actx->ch_layout); af->sample_rate=actx->sample_rate; int nb=chunk.second.size()/actx->ch_layout.nb_channels; af->nb_samples=nb; av_frame_get_buffer(af,0); memcpy(af->data[0],chunk.second.data(),chunk.second.size()*sizeof(int16_t)); af->pts=audioPts; audioPts+=nb;
                if(avcodec_send_frame(actx,af)>=0){ AVPacket apkt{}; while(avcodec_receive_packet(actx,&apkt)==0){ apkt.stream_index=astream->index; apkt.pts=av_rescale_q(apkt.pts,actx->time_base,astream->time_base); apkt.dts=apkt.pts; apkt.duration=apkt.size?nb:0; av_interleaved_write_frame(fmt,&apkt); av_packet_unref(&apkt);} }
                av_frame_free(&af);
            }
            avcodec_send_frame(actx,nullptr); AVPacket apkt{}; while(avcodec_receive_packet(actx,&apkt)==0){ apkt.stream_index=astream->index; apkt.pts=av_rescale_q(apkt.pts,actx->time_base,astream->time_base); apkt.dts=apkt.pts; av_interleaved_write_frame(fmt,&apkt); av_packet_unref(&apkt); }
            LogProRes("[ProResExporter] Audio encode finished");
        } else {
            avcodec_free_context(&actx); actx=nullptr; LogProRes("[ProResExporter] Failed to init audio encoder");
        }
    }

    avcodec_send_frame(vctx,nullptr); AVPacket pkt{}; while(avcodec_receive_packet(vctx,&pkt)==0){ pkt.stream_index=vstream->index; pkt.duration=1; pkt.pts=av_rescale_q(pkt.pts,vctx->time_base,vstream->time_base); pkt.dts=pkt.pts; av_interleaved_write_frame(fmt,&pkt); av_packet_unref(&pkt); }
    av_write_trailer(fmt);
    if(!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    if(actx) avcodec_free_context(&actx);
    avcodec_free_context(&vctx); avformat_free_context(fmt);
    conv.cleanup();
    if(m_hwFrames) av_buffer_unref(&m_hwFrames);
    if(m_hwDevice) av_buffer_unref(&m_hwDevice);
    m_running.store(false);
}
