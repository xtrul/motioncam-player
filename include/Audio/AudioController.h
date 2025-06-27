// FILE: include/Audio/AudioController.h
#ifndef AUDIO_CONTROLLER_H
#define AUDIO_CONTROLLER_H

#include <SDL.h>
#include <vector>
#include <cstdint>
#include <utility>
#include <motioncam/Decoder.hpp>

class AudioController {
public:
    AudioController();
    ~AudioController();

    bool init();
    void shutdown();
    void reset(motioncam::AudioChunkLoader* loader, int64_t firstVideoFrameTimestampNs);
    void setPaused(bool paused);
    void setForceMute(bool forceMute);
    bool isEffectivelyMuted() const;
    void updatePlayback(int64_t elapsedNs);
    int64_t getLastQueuedTimestamp() const { return m_lastQueuedTimestamp; }
    int64_t getAudioAnchorTimestampNs() const { return m_firstVideoFrameTs; }
    int64_t latency() const { return m_latencyNs; }

private:
    SDL_AudioDeviceID            m_device = 0;
    motioncam::AudioChunkLoader* m_loader = nullptr;
    int64_t                      m_firstVideoFrameTs = 0;
    int64_t                      m_latencyNs = 0;
    motioncam::AudioChunk        m_cache;
    bool                         m_hasCache = false;
    bool                         m_isPaused = false;
    bool                         m_isForceMuted = false;
    int64_t                      m_lastQueuedTimestamp = 0;

    void pause_internal();
    void resume_internal();
    void queueSamples(const std::vector<int16_t>& pcm);
};

#endif