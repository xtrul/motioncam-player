#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <vulkan/vulkan.h>
#include "ffmpeg_headers.hpp"
#include "Export/IProResExporter.h"

class DecoderWrapper;
class Renderer_VK;
class AudioController;
class GpuYuvConverter;

class ProResGpuExporter : public IProResExporter {
public:
    ProResGpuExporter();
    ~ProResGpuExporter();

    bool start(const std::string& path, const std::string& outMov,
               DecoderWrapper* decoder, Renderer_VK* renderer,
               AudioController* audio);
    void join();
    bool isRunning() const { return m_running.load(); }
private:
    void run();
    std::string m_path;
    std::string m_out;
    DecoderWrapper* m_decoder{nullptr};
    Renderer_VK* m_renderer{nullptr};
    AudioController* m_audio{nullptr};
    std::unique_ptr<GpuYuvConverter> m_converter;

    AVBufferRef* m_hwDevice{nullptr};
    AVBufferRef* m_hwFrames{nullptr};

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};
