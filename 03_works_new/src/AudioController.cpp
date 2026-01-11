#include "AudioController.h"
// Logger include removed

AudioController::AudioController() {}
AudioController::~AudioController() { shutdown(); }

bool AudioController::init()
{
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        // SDL_GetError() can be used if needed, no logging here
        return false;
    }

    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 2048; // Buffer size
    want.callback = nullptr; // Using SDL_QueueAudio

    m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!m_device) {
        // SDL_GetError() can be used if needed, no logging here
        return false;
    }

    m_latencyNs = static_cast<int64_t>(have.samples) * 1'000'000'000LL / have.freq;
    // Info calculated, no log

    SDL_PauseAudioDevice(m_device, 0); // Start unpaused
    m_isPaused = false;
    m_isForceMuted = false;
    return true;
}

void AudioController::reset(motioncam::AudioChunkLoader* loader,
    int64_t firstVideoFrameTimestampNs)
{
    m_loader = loader;
    m_firstVideoFrameTs = firstVideoFrameTimestampNs;
    m_hasCache = false;
    m_isPaused = false; // Default to not paused on reset
    m_lastQueuedTimestamp = 0; // Reset last queued timestamp

    if (m_device) {
        SDL_ClearQueuedAudio(m_device); // Clear any old audio
        if (!m_isForceMuted) { // If not force muted, ensure it's unpaused
            SDL_PauseAudioDevice(m_device, 0);
        } else { // If force muted, ensure it's paused
            SDL_PauseAudioDevice(m_device, 1);
        }
    }
}

void AudioController::shutdown()
{
    if (m_device) {
        SDL_PauseAudioDevice(m_device, 1); // Pause before closing
        SDL_ClearQueuedAudio(m_device);    // Clear queue
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
}

void AudioController::pause_internal()
{
    if (m_device && !m_isPaused) {
        if (!m_isForceMuted) { // Only pause if not already force-muted (which implies paused)
            SDL_PauseAudioDevice(m_device, 1);
        }
        m_isPaused = true;
    }
}
void AudioController::resume_internal()
{
    if (m_device && m_isPaused) {
        if (!m_isForceMuted) { // Only resume if not force-muted
            SDL_ClearQueuedAudio(m_device); // Clear old audio before resuming to avoid jump
            SDL_PauseAudioDevice(m_device, 0);
        }
        m_isPaused = false;
    }
}

void AudioController::setPaused(bool desiredPauseState) {
    if (desiredPauseState) {
        pause_internal();
    } else {
        resume_internal();
    }
}

void AudioController::setForceMute(bool forceMute) {
    if (m_isForceMuted == forceMute) return;

    m_isForceMuted = forceMute;
    // State changed, no log

    if (m_device) {
        if (m_isForceMuted) {
            SDL_PauseAudioDevice(m_device, 1); // Muting implies pausing
            SDL_ClearQueuedAudio(m_device);    // Clear queue on mute
        } else { // Unmuting
            if (m_isPaused) { // If it was supposed to be paused, keep it paused
                SDL_PauseAudioDevice(m_device, 1);
            } else { // Otherwise, unpause it
                SDL_ClearQueuedAudio(m_device); // Clear before unpausing
                SDL_PauseAudioDevice(m_device, 0);
            }
        }
    }
}

bool AudioController::isEffectivelyMuted() const {
    return m_isPaused || m_isForceMuted;
}

void AudioController::updatePlayback(int64_t elapsedNs)
{
    if (!m_device || isEffectivelyMuted() || !m_loader) return;
    // Target to queue audio up to: current playback time + audio buffer latency
    const int64_t targetQueueUntilNs = elapsedNs + m_latencyNs;

    // Keep loading and queueing audio chunks as long as their start time
    // is before our target queue-until time.
    while (true) {
        if (!m_hasCache) {
            AudioChunk tmpChunk;
            if (!m_loader->next(tmpChunk)) {
                // No more audio chunks from the loader
                return;
            }
            // Adjust chunk timestamp to be relative to the video segment start
            tmpChunk.first -= m_firstVideoFrameTs;
            if (tmpChunk.first < 0) { // Skip chunks before the video starts
                continue;
            }
            m_cache = std::move(tmpChunk);
            m_hasCache = true;
        }

        // If the cached chunk starts after our target time, stop queueing for now
        if (m_cache.first > targetQueueUntilNs) {
            break;
        }

        // Queue the cached chunk and clear the cache
        queueSamples(m_cache.second);
        m_hasCache = false;
    }
}

void AudioController::queueSamples(const std::vector<int16_t>& pcm)
{
    if (pcm.empty() || !m_device) return;

    const Uint8* data = reinterpret_cast<const Uint8*>(pcm.data());
    Uint32       bytes = static_cast<Uint32>(pcm.size() * sizeof(int16_t));

    if (SDL_QueueAudio(m_device, data, bytes) != 0) {
        // SDL_GetError() can be used if needed, no logging here
    } else {
        // Successfully queued, update the timestamp of the last known queued audio.
        // m_cache.first is already relative to video segment start.
        m_lastQueuedTimestamp = m_cache.first;
    }
}
