#ifndef PLAYBACK_CONTROLLER_H
#define PLAYBACK_CONTROLLER_H

#include <chrono>
#include <vector>
#include <optional>
#include <cstddef> // For size_t
#include <mutex>   // For std::mutex and std::scoped_lock

#include <nlohmann/json.hpp> // For nlohmann::json

// Forward declare GLFWwindow to avoid including glfw3.h in this header
struct GLFWwindow;

class PlaybackController {
public:
    PlaybackController();

    enum class PlaybackMode {
        REALTIME = 0,
        FIXED_24FPS,
        FIXED_30FPS,
        FIXED_60FPS,
        BENCHMARK
    };

    void handleKey(int key, GLFWwindow* window);
    void togglePause();
    bool isPaused() const;

    void processNewSegment(
        const nlohmann::json& firstFrameMetadata, // Use nlohmann::json directly
        size_t totalFramesInSegment,
        std::chrono::steady_clock::time_point segmentWallClockStartTime
    );

    // mediaFrameTimestamps are the actual timestamps from the loaded file for the current segment
    bool updatePlayhead(
        std::chrono::steady_clock::time_point currentWallClock,
        const std::vector<int64_t>& mediaFrameTimestamps
    );

    void stepForward(size_t totalFramesInSegment);
    void stepBackward(size_t totalFramesInSegment);
    void seekFrame(size_t frameIdx, size_t totalFramesInSegment); // Old seek

    // New centralized seek logic
    void seekToFrame(size_t newIdx, const std::vector<int64_t>& mediaFrameTimestamps);


    void toggleZoomNativePixels();
    bool isZoomNativePixels() const;

    size_t getCurrentFrameIndex() const;
    std::optional<int64_t> getCurrentFrameMediaTimestamp(const std::vector<int64_t>& mediaFrameTimestamps) const;
    std::optional<int64_t> getFirstFrameMediaTimestampOfSegment() const;
    std::chrono::steady_clock::time_point getWallClockAnchorForSegment() const;
    void setWallClockAnchorForSegment(std::chrono::steady_clock::time_point t);


    static double getDisplayFps();
    inline int64_t getFrameDurationNs() const { return m_frameDurationNs; }
    void setPlaybackMode(PlaybackMode mode);
    PlaybackMode getPlaybackMode() const;


private:
    mutable std::mutex m_mutex; // Mutex for protecting shared state

    bool m_isPaused;
    size_t m_currentFrameIdx = 0;
    size_t m_totalFramesInCurrentSegment = 0; // Store total frames for current segment
    int64_t m_frameDurationNs = 16666667; // Default to ~60 FPS (16.66 ms)

    // For time-based playback synchronization
    std::optional<int64_t> m_firstFrameMediaTimestampNs_currentSegment;
    std::chrono::steady_clock::time_point m_segmentWallClockStartTime;

    bool m_zoomNativePixels = false;

    // For FPS calculation
    std::chrono::steady_clock::time_point m_fpsAvgStart;
    int m_framesForAvg = 0;
    std::chrono::steady_clock::time_point m_lastBenchmarkTime;
    static double s_displayFps;
    PlaybackMode m_playbackMode = PlaybackMode::REALTIME;
};

#endif // PLAYBACK_CONTROLLER_H