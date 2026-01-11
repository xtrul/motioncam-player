// --- START OF FILE PlaybackController.cpp ---

#include "PlaybackController.h"
// Logger include removed
#include <GLFW/glfw3.h> // For GLFW_KEY_SPACE etc.
#include <string>
#include <stdexcept>
#include <algorithm> // For std::lower_bound, std::min
#include <nlohmann/json.hpp> // For metadata processing

double PlaybackController::s_displayFps = 0.0; // Static member definition

PlaybackController::PlaybackController() {
    m_fpsAvgStart = std::chrono::steady_clock::now();
}

void PlaybackController::handleKey(int key, GLFWwindow* /*window*/) {
    switch (key) {
    case GLFW_KEY_SPACE:
        togglePause();
        break;
    // Other keys specific to playback can be handled here if not in App.cpp
    default:
        break;
    }
}

void PlaybackController::togglePause() {
    m_isPaused = !m_isPaused;
}

bool PlaybackController::isPaused() const {
    return m_isPaused;
}

void PlaybackController::processNewSegment(const nlohmann::json& firstFrameMetadata,
    size_t totalFramesInSegment,
    std::chrono::steady_clock::time_point segmentWallClockStartTime) {
    m_totalFramesInCurrentSegment = totalFramesInSegment;
    m_currentFrameIdx = 0; // Reset to start of new segment
    m_firstFrameMediaTimestampNs_currentSegment.reset(); // Clear old timestamp
    m_segmentWallClockStartTime = segmentWallClockStartTime;

    if (firstFrameMetadata.contains("timestamp")) {
        try {
            if (firstFrameMetadata["timestamp"].is_string()) {
                m_firstFrameMediaTimestampNs_currentSegment = std::stoll(firstFrameMetadata["timestamp"].get<std::string>());
            } else if (firstFrameMetadata["timestamp"].is_number()) {
                m_firstFrameMediaTimestampNs_currentSegment = firstFrameMetadata["timestamp"].get<int64_t>();
            } else {
                // Warning ("Invalid timestamp format in first frame metadata.") removed
            }
        } catch (const std::exception& e) {
            m_firstFrameMediaTimestampNs_currentSegment.reset();
            // Warning ("Error parsing timestamp from first frame metadata: ...") removed
        }
    }
    if (!m_firstFrameMediaTimestampNs_currentSegment && totalFramesInSegment > 0) {
        // Warning ("First frame timestamp not found for new segment, playback timing may be affected.") removed
    }
    // New segment info processed, no log
}

bool PlaybackController::updatePlayhead(
    std::chrono::steady_clock::time_point currentWallClock,
    const std::vector<int64_t>& mediaFrameTimestamps)
{
    if (m_isPaused || mediaFrameTimestamps.empty() || !m_firstFrameMediaTimestampNs_currentSegment) {
        // Update FPS counter even if paused or no data, to show UI responsiveness
        auto frameEndTime = std::chrono::steady_clock::now();
        ++m_framesForAvg;
        double secs = std::chrono::duration<double>(frameEndTime - m_fpsAvgStart).count();
        if (secs >= 1.0) {
            s_displayFps = static_cast<double>(m_framesForAvg) / secs;
            m_fpsAvgStart = frameEndTime;
            m_framesForAvg = 0;
        }
        return false; // No looping if paused or no data
    }

    auto wallClockElapsedSinceSegmentStart = currentWallClock - m_segmentWallClockStartTime;
    int64_t targetMediaTimestampAbsolute = *m_firstFrameMediaTimestampNs_currentSegment +
        std::chrono::duration_cast<std::chrono::nanoseconds>(wallClockElapsedSinceSegmentStart).count();

    // Find the frame whose timestamp is closest to (but not after) targetMediaTimestampAbsolute
    auto it = std::lower_bound(mediaFrameTimestamps.begin(), mediaFrameTimestamps.end(), targetMediaTimestampAbsolute);

    bool looped = false;
    if (it == mediaFrameTimestamps.end()) { // Target time is past the last frame
        m_currentFrameIdx = mediaFrameTimestamps.size() - 1; // Stay on last frame
        looped = true; // Signal end of segment
    } else if (it == mediaFrameTimestamps.begin()) { // Target time is before or at the first frame
        m_currentFrameIdx = 0;
    } else {
        // *it is the first frame >= target. If *it > target, we want the previous frame.
        if (*it > targetMediaTimestampAbsolute) { // && it != mediaFrameTimestamps.begin() is implied by else
            it = std::prev(it);
        }
        m_currentFrameIdx = static_cast<size_t>(std::distance(mediaFrameTimestamps.begin(), it));
    }

    // Ensure currentFrameIdx is valid
    m_currentFrameIdx = std::min(m_currentFrameIdx, mediaFrameTimestamps.size() - 1);


    // Update FPS counter
    auto frameEndTime = std::chrono::steady_clock::now();
    ++m_framesForAvg;
    double secs = std::chrono::duration<double>(frameEndTime - m_fpsAvgStart).count();
    if (secs >= 1.0) {
        s_displayFps = static_cast<double>(m_framesForAvg) / secs;
        m_fpsAvgStart = frameEndTime;
        m_framesForAvg = 0;
    }

    if (looped) {
        // Segment end detected, no log
    }
    return looped;
}

void PlaybackController::stepForward(size_t totalFramesInSegment) {
    if (totalFramesInSegment > 0) {
        m_currentFrameIdx = (m_currentFrameIdx + 1);
        if (m_currentFrameIdx >= totalFramesInSegment) {
            m_currentFrameIdx = totalFramesInSegment - 1; // Clamp to last frame
        }
    }
}

void PlaybackController::stepBackward(size_t totalFramesInSegment) {
    if (totalFramesInSegment > 0) {
        if (m_currentFrameIdx > 0) {
            m_currentFrameIdx--;
        } else {
            m_currentFrameIdx = 0; // Clamp to first frame
        }
    }
}

void PlaybackController::seekFrame(size_t frameIdx, size_t totalFramesInSegment)
{
    if (totalFramesInSegment == 0) return;
    if (frameIdx >= totalFramesInSegment) frameIdx = totalFramesInSegment - 1; // Clamp
    m_currentFrameIdx = frameIdx;
}


void PlaybackController::toggleZoomNativePixels() {
    m_zoomNativePixels = !m_zoomNativePixels;
}

bool PlaybackController::isZoomNativePixels() const {
    return m_zoomNativePixels;
}

size_t PlaybackController::getCurrentFrameIndex() const {
    return m_currentFrameIdx;
}

std::optional<int64_t> PlaybackController::getCurrentFrameMediaTimestamp(const std::vector<int64_t>& mediaFrameTimestamps) const {
    if (m_currentFrameIdx < mediaFrameTimestamps.size()) {
        return mediaFrameTimestamps[m_currentFrameIdx];
    }
    return std::nullopt;
}

std::optional<int64_t> PlaybackController::getFirstFrameMediaTimestampOfSegment() const {
    return m_firstFrameMediaTimestampNs_currentSegment;
}

std::chrono::steady_clock::time_point PlaybackController::getWallClockAnchorForSegment() const {
    return m_segmentWallClockStartTime;
}

void PlaybackController::setWallClockAnchorForSegment(std::chrono::steady_clock::time_point t) {
    m_segmentWallClockStartTime = t;
}

double PlaybackController::getDisplayFps() {
    return s_displayFps;
}
