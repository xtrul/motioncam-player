#pragma once
#include <string>

class DecoderWrapper;
class Renderer_VK;
class AudioController;

class IProResExporter {
public:
    virtual ~IProResExporter() = default;
    virtual bool start(const std::string& path, const std::string& outMov,
                       DecoderWrapper* decoder, Renderer_VK* renderer,
                       AudioController* audio) = 0;
    virtual void join() = 0;
    virtual bool isRunning() const = 0;
};
