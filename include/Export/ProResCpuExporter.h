#pragma once
#include <string>
#include <thread>
#include <atomic>
#include "ffmpeg_headers.hpp"
#include "Export/IProResExporter.h"

class DecoderWrapper;
class Renderer_VK; // unused
class AudioController;

class ProResCpuExporter : public IProResExporter {
public:
    ProResCpuExporter();
    ~ProResCpuExporter();

    bool start(const std::string& path, const std::string& outMov,
               DecoderWrapper* decoder, Renderer_VK* renderer,
               AudioController* audio) override;
    void join() override;
    bool isRunning() const override { return m_running.load(); }
private:
    void run();
    std::string m_path;
    std::string m_out;
    DecoderWrapper* m_decoder{nullptr};
    AudioController* m_audio{nullptr};

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};
