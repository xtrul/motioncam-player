#include "Audio/AudioController.h"
#include "Utils/DebugLog.h"
#include <algorithm> // For std::min, std::max
#include <vector>
#include <utility>
#include <sstream> // For std::ostringstream in logging

// To be absolutely safe with MSVC and windows.h macros
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

AudioController::AudioController() : m_device(0), m_loader(nullptr), m_firstVideoFrameTs(0), m_latencyNs(0), m_hasCache(false), m_isPaused(false), m_isForceMuted(false), m_lastQueuedTimestamp(0) {}

AudioController::~AudioController() {
    shutdown();
}

bool AudioController::init() {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        LogToFile(std::string("[AudioController::init] SDL_Init(SDL_INIT_AUDIO) failed: ") + SDL_GetError());
        return false;
    }

    SDL_AudioSpec wantSpec{}, haveSpec{};
    wantSpec.freq = 48000;
    wantSpec.format = AUDIO_S16LSB;
    wantSpec.channels = 2;
    wantSpec.samples = 2048; // Default, can be adjusted
    wantSpec.callback = nullptr;

    m_device = SDL_OpenAudioDevice(nullptr, 0, &wantSpec, &haveSpec, 0);
    if (!m_device) {
        LogToFile(std::string("[AudioController::init] SDL_OpenAudioDevice failed: ") + SDL_GetError());
        return false;
    }

    m_latencyNs = static_cast<int64_t>(haveSpec.samples) * 1'000'000'000LL / haveSpec.freq;
    LogToFile(std::string("[AudioController::init] Audio device opened. Freq: ") + std::to_string(haveSpec.freq) +
        ", Samples: " + std::to_string(haveSpec.samples) + ", Latency (1 buffer): " + std::to_string(m_latencyNs / 1000000) + "ms");


    SDL_PauseAudioDevice(m_device, 0); // Start unpaused internally
    m_isPaused = false;      // Reflect internal state
    m_isForceMuted = false;
    return true;
}

void AudioController::shutdown() {
    if (m_device) {
        SDL_PauseAudioDevice(m_device, 1);
        SDL_ClearQueuedAudio(m_device);
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
        LogToFile("[AudioController::shutdown] Audio device closed.");
    }
    // SDL_QuitSubSystem(SDL_INIT_AUDIO); // Consider if SDL_Quit should be called here or at app exit
}

void AudioController::reset(motioncam::AudioChunkLoader* loader, int64_t firstVideoFrameTimestampNs) {
    std::ostringstream log_oss;
    log_oss << "[Audio::reset] Called. Loader: " << (loader ? "VALID" : "NULLPTR")
        << ", Input firstVideoFrameTsNs: " << firstVideoFrameTimestampNs;

    m_loader = loader;
    m_firstVideoFrameTs = firstVideoFrameTimestampNs; // This is the T0_media_video anchor for the current segment
    m_hasCache = false;
    m_lastQueuedTimestamp = 0; // Reset relative queued timestamp

    log_oss << ", Internal m_firstVideoFrameTs (AudioAnchor) set to: " << m_firstVideoFrameTs;
    LogToFile(log_oss.str());

    if (m_device) {
        SDL_ClearQueuedAudio(m_device);
        if (!m_isForceMuted && !m_isPaused) {
            SDL_PauseAudioDevice(m_device, 0);
        }
        else {
            SDL_PauseAudioDevice(m_device, 1);
        }
    }
}


void AudioController::pause_internal() {
    if (m_device && !m_isPaused) {
        if (!m_isForceMuted) { // Only pause if not already force-muted (which implies paused)
            SDL_PauseAudioDevice(m_device, 1);
        }
        m_isPaused = true;
        LogToFile("[AudioController::pause_internal] Audio actually paused (SDL_PauseAudioDevice(1)).");
    }
}

void AudioController::resume_internal() {
    if (m_device && m_isPaused) {
        m_isPaused = false;
        if (!m_isForceMuted) { // Only unpause if not force-muted
            SDL_ClearQueuedAudio(m_device); // Clear stale audio from pause
            SDL_PauseAudioDevice(m_device, 0);
            LogToFile("[AudioController::resume_internal] Audio actually resumed (SDL_PauseAudioDevice(0) after clear).");
        }
        else {
            LogToFile("[AudioController::resume_internal] Audio logically resumed, but remains muted by forceMute.");
        }
    }
}

void AudioController::setPaused(bool desiredPauseState) {
    if (desiredPauseState == m_isPaused) return; // No change
    LogToFile(std::string("[AudioController::setPaused] Received desire to change pause state to: ") + (desiredPauseState ? "PAUSE" : "RESUME") + ". Current m_isPaused: " + (m_isPaused ? "true" : "false"));
    if (desiredPauseState) {
        pause_internal();
    }
    else {
        resume_internal();
    }
}

void AudioController::setForceMute(bool forceMute) {
    if (m_isForceMuted == forceMute) {
        return;
    }
    m_isForceMuted = forceMute;
    LogToFile(std::string("[AudioController::setForceMute] Force mute set to: ") + (m_isForceMuted ? "ON" : "OFF"));

    if (m_device) {
        if (m_isForceMuted) {
            SDL_PauseAudioDevice(m_device, 1); // Muting implies pausing the device
            SDL_ClearQueuedAudio(m_device);
        }
        else { // Unmuting
            if (m_isPaused) { // If it was logically paused, keep it physically paused
                SDL_PauseAudioDevice(m_device, 1);
            }
            else { // If it was playing, resume physical playback
                SDL_ClearQueuedAudio(m_device);
                SDL_PauseAudioDevice(m_device, 0);
            }
        }
    }
}

bool AudioController::isEffectivelyMuted() const {
    return m_isPaused || m_isForceMuted;
}

void AudioController::updatePlayback(int64_t elapsedNsSinceSegmentStart) {
    if (!m_device || isEffectivelyMuted() || !m_loader) {
        return;
    }

    const int64_t targetQueueUntilMediaTimeNs = elapsedNsSinceSegmentStart + m_latencyNs;

    // More detailed logging for audio queueing decisions
    std::ostringstream log_oss_up;
    log_oss_up << "[Audio::updatePlayback] ElapNsSegStart: " << elapsedNsSinceSegmentStart
        << ", LatencyNs: " << m_latencyNs
        << ", TargetRelMediaTimeNs (Elap+Lat): " << targetQueueUntilMediaTimeNs
        << ", m_firstVideoFrameTs (AudioAnchor): " << m_firstVideoFrameTs
        << ", m_lastQueuedTimestamp (Rel): " << m_lastQueuedTimestamp;
    // LogToFile(log_oss_up.str()); // This can be very verbose, enable if needed

    const int64_t MAX_QUEUE_AHEAD_OF_LAST_QUEUED_NS = 200LL * 1000000LL; // 200ms
    const int64_t MAX_QUEUE_AHEAD_OF_ELAPSED_NS = 500LL * 1000000LL;     // 500ms

    int64_t catch_up_target_ns = m_lastQueuedTimestamp + MAX_QUEUE_AHEAD_OF_LAST_QUEUED_NS;
    int64_t initial_burst_target_ns = elapsedNsSinceSegmentStart + MAX_QUEUE_AHEAD_OF_ELAPSED_NS;

    int64_t effectiveTargetQueueUntilNs = std::min({ targetQueueUntilMediaTimeNs, catch_up_target_ns, initial_burst_target_ns });
    effectiveTargetQueueUntilNs = std::max(effectiveTargetQueueUntilNs, targetQueueUntilMediaTimeNs); // Ensure we at least target the base latency

    int chunksQueuedThisCall = 0;
    const int MAX_CHUNKS_PER_UPDATE_CALL = 10; // Limit how many chunks we try to process in one go

    while (chunksQueuedThisCall < MAX_CHUNKS_PER_UPDATE_CALL) {
        if (!m_hasCache) {
            motioncam::AudioChunk tempChunk;
            if (!m_loader->next(tempChunk)) {
                // LogToFile("[Audio::updatePlayback] No more audio chunks from loader.");
                break;
            }
            int64_t originalAbsoluteTs = tempChunk.first;
            // Make timestamp relative to the first video frame of the *current segment*
            tempChunk.first -= m_firstVideoFrameTs;

            // If original timestamp was -1 (often indicating end of audio stream or error),
            // then after subtraction, it will be a large negative number.
            // We should only process chunks that are meant to be played *after* the first video frame.
            if (tempChunk.first < 0 && originalAbsoluteTs != -1LL) { // -1LL is a special marker from decoder
                // LogToFile(std::string("[Audio::updatePlayback] Skipping early audio chunk. OrigAbsTS: ") + std::to_string(originalAbsoluteTs) + ", RelTS: " + std::to_string(tempChunk.first));
                continue;
            }
            m_cache = std::move(tempChunk);
            m_hasCache = true;
            // LogToFile(std::string("[Audio::updatePlayback] Loaded chunk. OrigAbsTS: ") + std::to_string(originalAbsoluteTs) + ", RelTS (m_cache.first): " + std::to_string(m_cache.first));
        }

        // Check if the cached chunk's (relative) timestamp is beyond our target
        // A chunk with timestamp -1 (after subtraction) indicates end-of-stream or error, should not be queued based on time.
        bool chunkHasValidTimestamp = (m_cache.first != -1LL - m_firstVideoFrameTs);

        if (chunkHasValidTimestamp && (m_cache.first > effectiveTargetQueueUntilNs)) {
            // LogToFile(std::string("[Audio::updatePlayback] Holding audio. CacheRelTS: ") + std::to_string(m_cache.first) + " > EffectiveTargetRelMediaTimeNs: " + std::to_string(effectiveTargetQueueUntilNs));
            break;
        }

        // LogToFile(std::string("[Audio::updatePlayback] Queuing audio chunk. CacheRelTS: ") + std::to_string(m_cache.first) + (chunkHasValidTimestamp ? "" : " (OrigTS was -1, TS estimated)"));
        queueSamples(m_cache.second); // This will update m_lastQueuedTimestamp if successful and timestamp is valid
        m_hasCache = false;
        chunksQueuedThisCall++;
    }
}

void AudioController::queueSamples(const std::vector<int16_t>& pcm) {
    if (pcm.empty() || !m_device) {
        return;
    }

    const Uint8* dataBytes = reinterpret_cast<const Uint8*>(pcm.data());
    Uint32 numBytes = static_cast<Uint32>(pcm.size() * sizeof(int16_t));

    if (SDL_QueueAudio(m_device, dataBytes, numBytes) != 0) {
        LogToFile(std::string("[AudioController::queueSamples] SDL_QueueAudio failed: ") + SDL_GetError());
    }
    else {
        // Only update m_lastQueuedTimestamp if the chunk had a valid, non-error timestamp
        if (m_cache.first != -1LL - m_firstVideoFrameTs) {
            m_lastQueuedTimestamp = m_cache.first;
        }
    }
}