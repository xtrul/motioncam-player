#include "Export/ProResExporter.h"
#include "Graphics/GpuYuvConverter.h"
#include "Decoder/DecoderWrapper.h"
#include "Graphics/Renderer_VK.h"
#include "Audio/AudioController.h"
#include "Utils/DebugLog.h"
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
    LogProRes("[ProResExporter] Thread started");
    if(!m_decoder || !m_renderer){ m_running.store(false); return; }
    // Placeholder: real implementation should initialize Vulkan hw frames and FFmpeg pipeline.
    // Here we simply log and exit.
    LogProRes("[ProResExporter] Export functionality not fully implemented in sample code.");
    m_running.store(false);
}
