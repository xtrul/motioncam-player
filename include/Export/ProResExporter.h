#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <vulkan/vulkan.h>
#include "ffmpeg_headers.hpp"

class DecoderWrapper;
class Renderer_VK;
class AudioController;
class GpuYuvConverter;

class ProResExporter {
public:
    ProResExporter();
    ~ProResExporter();

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

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};
