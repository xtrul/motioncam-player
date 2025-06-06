#include "Playback/PlaybackController.h"
#include "Utils/DebugLog.h" // For LogToFile

#include <GLFW/glfw3.h>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream> // For std::ostringstream in logging

double PlaybackController::s_displayFps = 0.0;

PlaybackController::PlaybackController() : m_isPaused(false) {
    m_fpsAvgStart = std::chrono::steady_clock::now();
    LogToFile("[PlaybackController] Constructor: Initialized, paused = false.");
}

void PlaybackController::handleKey(int key, GLFWwindow* /*window*/) {
    switch (key) {
    case GLFW_KEY_SPACE:
        togglePause(); // m_mutex is taken inside togglePause
        LogToFile(std::string("[PlaybackController::handleKey] Space pressed. New Paused state (after toggle): ") + (isPaused() ? "true" : "false")); // isPaused also takes mutex
        break;
    default:
        break;
    }
}

void PlaybackController::togglePause() {
    std::scoped_lock lock(m_mutex);
    m_isPaused = !m_isPaused;
    LogToFile(std::string("[PlaybackController::togglePause] Paused state is now: ") + (m_isPaused ? "true" : "false"));
}

bool PlaybackController::isPaused() const {
    std::scoped_lock lock(m_mutex);
    return m_isPaused;
}

void PlaybackController::processNewSegment(
    const nlohmann::json& firstFrameMetadata,
    size_t totalFramesInSegment,
    std::chrono::steady_clock::time_point segmentWallClockStartTime) {

    std::scoped_lock lock(m_mutex);
    m_totalFramesInCurrentSegment = totalFramesInSegment;
    m_currentFrameIdx = 0;
    m_firstFrameMediaTimestampNs_currentSegment.reset();
    m_segmentWallClockStartTime = segmentWallClockStartTime;

    std::ostringstream log_oss_ps;
    log_oss_ps << "[PB::processNewSegment] NEW SEGMENT. Total frames: " << totalFramesInSegment
        << ", WallClockAnchorEpochNs (at segment start): " << m_segmentWallClockStartTime.time_since_epoch().count();
    LogToFile(log_oss_ps.str());


    if (firstFrameMetadata.contains("timestamp")) {
        try {
            if (firstFrameMetadata["timestamp"].is_string()) {
                m_firstFrameMediaTimestampNs_currentSegment = std::stoll(firstFrameMetadata["timestamp"].get<std::string>());
            }
            else if (firstFrameMetadata["timestamp"].is_number_integer()) {
                m_firstFrameMediaTimestampNs_currentSegment = firstFrameMetadata["timestamp"].get<int64_t>();
            }

            std::ostringstream log_oss_ts;
            log_oss_ts << "[PB::processNewSegment] Using firstFrameMetaTS (raw): "
                << (firstFrameMetadata.contains("timestamp") ? firstFrameMetadata["timestamp"].dump() : "N/A")
                << ", Parsed FirstVideoFrameMediaTS: "
                << (m_firstFrameMediaTimestampNs_currentSegment.has_value() ? std::to_string(m_firstFrameMediaTimestampNs_currentSegment.value()) : "NOT_SET");
            LogToFile(log_oss_ts.str());
        }
        catch (const std::exception& e) {
            m_firstFrameMediaTimestampNs_currentSegment.reset();
            LogToFile(std::string("[PB::processNewSegment] Exception parsing first frame timestamp: ") + e.what());
        }
    }
    else {
        LogToFile("[PB::processNewSegment] First frame metadata does not contain 'timestamp'. First Video Frame Media Timestamp will be unset.");
    }
}

bool PlaybackController::updatePlayhead(
    std::chrono::steady_clock::time_point currentWallClock,
    const std::vector<int64_t>& mediaFrameTimestamps) {

    auto frameEndTimeForFps = std::chrono::steady_clock::now();
    m_framesForAvg++;
    double elapsedSecondsForFps = std::chrono::duration<double>(frameEndTimeForFps - m_fpsAvgStart).count();
    if (elapsedSecondsForFps >= 1.0) {
        s_displayFps = static_cast<double>(m_framesForAvg) / elapsedSecondsForFps;
        m_fpsAvgStart = frameEndTimeForFps;
        m_framesForAvg = 0;
    }

    std::scoped_lock lock(m_mutex);

    if (m_isPaused || mediaFrameTimestamps.empty() || !m_firstFrameMediaTimestampNs_currentSegment.has_value()) {
        return false;
    }

    auto wallClockElapsedSinceSegmentStart = currentWallClock - m_segmentWallClockStartTime;
    int64_t wallClockElapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(wallClockElapsedSinceSegmentStart).count();

    int64_t targetMediaTimestampAbsolute = m_firstFrameMediaTimestampNs_currentSegment.value() + wallClockElapsedNs;

    // This log can be very verbose, enable if needed for fine-grained debugging
    /*
    std::ostringstream log_oss_up;
    log_oss_up << "[PB::updatePlayhead] CurrWallClockEpochNs: " << currentWallClock.time_since_epoch().count()
               << ", SegWallClockStartEpochNs: " << m_segmentWallClockStartTime.time_since_epoch().count()
               << ", WallClockElapsedNs: " << wallClockElapsedNs
               << ", FirstFrameMediaTs: " << m_firstFrameMediaTimestampNs_currentSegment.value()
               << ", TargetMediaTsAbsolute: " << targetMediaTimestampAbsolute;
    LogToFile(log_oss_up.str());
    */

    auto it = std::lower_bound(mediaFrameTimestamps.begin(), mediaFrameTimestamps.end(), targetMediaTimestampAbsolute);

    bool segmentEnded = false;
    size_t newFrameIdx = m_currentFrameIdx;

    if (it == mediaFrameTimestamps.end()) {
        if (!mediaFrameTimestamps.empty()) {
            newFrameIdx = mediaFrameTimestamps.size() - 1;
        }
        else {
            newFrameIdx = 0;
        }
        segmentEnded = true;
    }
    else if (it == mediaFrameTimestamps.begin()) {
        newFrameIdx = 0;
    }
    else {
        if (*it > targetMediaTimestampAbsolute) {
            it = std::prev(it);
        }
        newFrameIdx = static_cast<size_t>(std::distance(mediaFrameTimestamps.begin(), it));
    }

    if (!mediaFrameTimestamps.empty()) {
        newFrameIdx = std::min(newFrameIdx, mediaFrameTimestamps.size() - 1);
    }
    else {
        newFrameIdx = 0;
    }

    if (newFrameIdx != m_currentFrameIdx) {
        std::ostringstream log_oss_idx;
        log_oss_idx << "[PB::updatePlayhead] OldIdx: " << m_currentFrameIdx
            << ", NewIdx: " << newFrameIdx
            << (mediaFrameTimestamps.empty() || newFrameIdx >= mediaFrameTimestamps.size() ? std::string(", NewIdx MediaTS: OOB or Empty") : std::string(", NewIdx MediaTS: ") + std::to_string(mediaFrameTimestamps[newFrameIdx]))
            << ", SegmentEnded: " << (segmentEnded ? "T" : "F");
        LogToFile(log_oss_idx.str());
    }
    m_currentFrameIdx = newFrameIdx;

    return segmentEnded;
}

void PlaybackController::stepForward(size_t totalFramesInSegment) {
    std::scoped_lock lock(m_mutex);
    size_t current_total_frames = (totalFramesInSegment > 0) ? totalFramesInSegment : m_totalFramesInCurrentSegment;

    if (current_total_frames > 0) {
        m_currentFrameIdx = (m_currentFrameIdx + 1);
        if (m_currentFrameIdx >= current_total_frames) {
            m_currentFrameIdx = current_total_frames - 1;
        }
    }
    LogToFile(std::string("[PlaybackController::stepForward] Stepped forward to frame: ") + std::to_string(m_currentFrameIdx));
}

void PlaybackController::stepBackward(size_t totalFramesInSegment) {
    std::scoped_lock lock(m_mutex);
    size_t current_total_frames = (totalFramesInSegment > 0) ? totalFramesInSegment : m_totalFramesInCurrentSegment;
    (void)current_total_frames;

    if (m_currentFrameIdx > 0) {
        m_currentFrameIdx--;
    }
    else {
        m_currentFrameIdx = 0;
    }
    LogToFile(std::string("[PlaybackController::stepBackward] Stepped backward to frame: ") + std::to_string(m_currentFrameIdx));
}

// Old seek, potentially for non-resync scenarios or internal use if ever needed.
void PlaybackController::seekFrame(size_t frameIdx, size_t totalFramesInSegment) {
    std::scoped_lock lock(m_mutex);
    m_totalFramesInCurrentSegment = totalFramesInSegment;
    if (totalFramesInSegment == 0) {
        m_currentFrameIdx = 0;
        LogToFile("[PlaybackController::seekFrame] Seek (old method) in empty segment, index set to 0.");
        return;
    }
    if (frameIdx >= totalFramesInSegment) {
        m_currentFrameIdx = totalFramesInSegment - 1;
    }
    else {
        m_currentFrameIdx = frameIdx;
    }
    LogToFile(std::string("[PlaybackController::seekFrame] (Old method) Seeked to frame: ") + std::to_string(m_currentFrameIdx));
}

// New centralized seek logic that also updates wall clock anchor
void PlaybackController::seekToFrame(
    size_t newIdx,
    const std::vector<int64_t>& mediaFrameTimestamps)
{
    std::scoped_lock lock(m_mutex);
    std::ostringstream log_oss_seek;
    log_oss_seek << "[PB::seekToFrame] Input newIdx: " << newIdx;

    if (mediaFrameTimestamps.empty()) {
        m_currentFrameIdx = 0;
        log_oss_seek << ", No media timestamps, index set to 0.";
        LogToFile(log_oss_seek.str());
        return;
    }

    m_totalFramesInCurrentSegment = mediaFrameTimestamps.size();

    size_t clampedNewIdx = newIdx;
    if (clampedNewIdx >= m_totalFramesInCurrentSegment) {
        clampedNewIdx = m_totalFramesInCurrentSegment > 0 ? m_totalFramesInCurrentSegment - 1 : 0;
    }
    m_currentFrameIdx = clampedNewIdx;
    log_oss_seek << ", Clamped m_currentFrameIdx: " << m_currentFrameIdx;

    log_oss_seek << ", Segment's FirstVideoFrameTS: " << (m_firstFrameMediaTimestampNs_currentSegment.has_value() ? std::to_string(m_firstFrameMediaTimestampNs_currentSegment.value()) : "NOT_SET");

    if (!m_firstFrameMediaTimestampNs_currentSegment.has_value()) {
        // This case should ideally be handled by ensuring processNewSegment always sets it if frames exist.
        // If it happens, it's a sign of inconsistent state.
        LogToFile(log_oss_seek.str() + " | CRITICAL WARNING: FirstFrameMediaTS for segment is NOT SET during seekToFrame. Attempting fallback.");
        if (!mediaFrameTimestamps.empty()) {
            // Attempt to recover, but this indicates a logic flow issue elsewhere
            m_firstFrameMediaTimestampNs_currentSegment = mediaFrameTimestamps.front();
            LogToFile(std::string("[PB::seekToFrame] RECOVERED FirstFrameMediaTS for segment: ") + std::to_string(m_firstFrameMediaTimestampNs_currentSegment.value()));
        }
        else {
            LogToFile("[PB::seekToFrame] CANNOT RECOVER FirstFrameMediaTS, no timestamps. Aborting wall clock anchor update.");
            return; // Cannot proceed without a base media timestamp
        }
    }

    int64_t firstTsOfSegment = m_firstFrameMediaTimestampNs_currentSegment.value();
    int64_t targetFrameMediaTs = mediaFrameTimestamps[m_currentFrameIdx];
    int64_t deltaVideoNsFromSegmentStart = targetFrameMediaTs - firstTsOfSegment;

    if (deltaVideoNsFromSegmentStart < 0) {
        log_oss_seek << " | WARN: Negative deltaVideoNsFromSegmentStart (" << deltaVideoNsFromSegmentStart
            << "). TargetFrameMediaTs: " << targetFrameMediaTs
            << ", FirstTsOfSegment: " << firstTsOfSegment << ". Clamping delta to 0.";
        deltaVideoNsFromSegmentStart = 0;
    }

    auto now_for_anchor = std::chrono::steady_clock::now();
    m_segmentWallClockStartTime = now_for_anchor - std::chrono::nanoseconds(deltaVideoNsFromSegmentStart);

    log_oss_seek << ", TargetFrameMediaTs (abs): " << targetFrameMediaTs
        << ", DeltaVideoNsFromSegStart: " << deltaVideoNsFromSegmentStart
        << ", NowEpochNs for anchor: " << now_for_anchor.time_since_epoch().count()
        << ", New WallClockAnchorEpochNs: " << m_segmentWallClockStartTime.time_since_epoch().count();
    LogToFile(log_oss_seek.str());
}


void PlaybackController::toggleZoomNativePixels() {
    std::scoped_lock lock(m_mutex);
    m_zoomNativePixels = !m_zoomNativePixels;
    LogToFile(std::string("[PlaybackController::toggleZoomNativePixels] Zoom native pixels: ") + (m_zoomNativePixels ? "ON" : "OFF"));
}

bool PlaybackController::isZoomNativePixels() const {
    std::scoped_lock lock(m_mutex);
    return m_zoomNativePixels;
}

size_t PlaybackController::getCurrentFrameIndex() const {
    std::scoped_lock lock(m_mutex);
    return m_currentFrameIdx;
}

std::optional<int64_t> PlaybackController::getCurrentFrameMediaTimestamp(const std::vector<int64_t>& mediaFrameTimestamps) const {
    std::scoped_lock lock(m_mutex);
    if (!mediaFrameTimestamps.empty() && m_currentFrameIdx < mediaFrameTimestamps.size()) {
        return mediaFrameTimestamps[m_currentFrameIdx];
    }
    return std::nullopt;
}

std::optional<int64_t> PlaybackController::getFirstFrameMediaTimestampOfSegment() const {
    std::scoped_lock lock(m_mutex);
    return m_firstFrameMediaTimestampNs_currentSegment;
}

std::chrono::steady_clock::time_point PlaybackController::getWallClockAnchorForSegment() const {
    std::scoped_lock lock(m_mutex);
    return m_segmentWallClockStartTime;
}

void PlaybackController::setWallClockAnchorForSegment(std::chrono::steady_clock::time_point t) {
    std::scoped_lock lock(m_mutex);
    LogToFile(std::string("[PB::setWallClockAnchor] Wall clock anchor updated by App. Old: ") +
        std::to_string(m_segmentWallClockStartTime.time_since_epoch().count()) +
        ", New: " + std::to_string(t.time_since_epoch().count()));
    m_segmentWallClockStartTime = t;
}

double PlaybackController::getDisplayFps() {
    return s_displayFps;
}