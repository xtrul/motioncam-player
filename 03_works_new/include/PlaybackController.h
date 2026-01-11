// --- START OF FILE PlaybackController.h ---
#ifndef PLAYBACK_CONTROLLER_H
#define PLAYBACK_CONTROLLER_H

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>


struct GLFWwindow;

class PlaybackController {
public:
    PlaybackController();

    void handleKey(int key, GLFWwindow* window);
    bool updatePlayhead(
        std::chrono::steady_clock::time_point currentWallClock,
        const std::vector<int64_t>& mediaFrameTimestamps
    );
    void processNewSegment(const nlohmann::json& firstFrameMetadata,
        size_t totalFramesInSegment,
        std::chrono::steady_clock::time_point segmentWallClockStartTime);
    void togglePause();
    bool isPaused() const;
    void stepForward(size_t totalFramesInSegment);
    void stepBackward(size_t totalFramesInSegment);
    void seekFrame(size_t frameIdx, size_t totalFramesInSegment);
    size_t getCurrentFrameIndex() const;
    void toggleZoomNativePixels();
    bool isZoomNativePixels() const;
    static double getDisplayFps();
    std::optional<int64_t> getCurrentFrameMediaTimestamp(const std::vector<int64_t>& mediaFrameTimestamps) const;
    std::optional<int64_t> getFirstFrameMediaTimestampOfSegment() const;
    std::chrono::steady_clock::time_point getWallClockAnchorForSegment() const;
    void setWallClockAnchorForSegment(std::chrono::steady_clock::time_point t);

private:
    size_t m_currentFrameIdx = 0;
    size_t m_totalFramesInCurrentSegment = 0;
    size_t m_framesForAvg = 0;
    std::chrono::steady_clock::time_point m_fpsAvgStart;
    static double s_displayFps;
    std::optional<int64_t> m_firstFrameMediaTimestampNs_currentSegment;
    std::chrono::steady_clock::time_point m_segmentWallClockStartTime;
    bool m_zoomNativePixels = false;
    bool m_isPaused = false;
};

#endif // PLAYBACK_CONTROLLER_H
// --- END OF FILE PlaybackController.h ---