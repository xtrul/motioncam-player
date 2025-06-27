// FILE: src/App/AppLoop.cpp
#include "App/App.h"
#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"
#include "Playback/PlaybackController.h"
#include "Graphics/Renderer_VK.h"
#include "Utils/DebugLog.h"
#include "Gui/GuiOverlay.h"

#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace fs = std::filesystem;

static constexpr std::size_t MAX_LEAD_FRAMES = 16;
static constexpr std::size_t MAX_LAG_FRAMES = 16;

#define VK_APP_CHECK(x)                                                 \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            std::string error_str = std::string("[VULKAN CHECK FAILED IN APP LOOP] Error: ") + std::to_string(err) + " (" #x ") at " __FILE__ ":" + std::to_string(__LINE__); \
            LogToFile(error_str);                                       \
            std::cerr << error_str << std::endl;                        \
            throw std::runtime_error("Vulkan API call failed in App loop context!"); \
        }                                                               \
    } while (0)


bool App::run() {
    LogToFile("[App::run] App::run() called and initialized.");
    LogToFile("[App::run] Entering main loop...");

    using namespace std::chrono_literals;
    using steady_clock = std::chrono::steady_clock;

    steady_clock::time_point loopStartTime, loopEndTime;
    steady_clock::time_point appLogicStartTime, appLogicEndTime;
    // size_t loopIteration = 0; // Removed for less verbose logs


    while (!glfwWindowShouldClose(m_window)) {
        // loopIteration++; // Removed for less verbose logs
        loopStartTime = steady_clock::now();
        m_sleepTimeMs = 0.0;

        // LogToFile(std::string("[App::run] Loop iteration: ") + std::to_string(loopIteration) + ", ActiveFileLoadID: " + std::to_string(m_activeFileLoadID)); // Too verbose

        appLogicStartTime = steady_clock::now();
        glfwPollEvents();

        bool paused = (m_playbackController ? m_playbackController->isPaused() : true);
        bool segment_looped_or_ended = false;

        if (m_playbackController) {
            const std::vector<motioncam::Timestamp>* currentFrameTimestamps = nullptr;
            if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                currentFrameTimestamps = &m_decoderWrapper->getDecoder()->getFrames();
            }
            segment_looped_or_ended = m_playbackController->updatePlayhead(
                steady_clock::now(), // Pass current time for video playhead calculation
                currentFrameTimestamps ? *currentFrameTimestamps : std::vector<motioncam::Timestamp>()
            );
            if (paused) segment_looped_or_ended = false;
        }
        appLogicEndTime = steady_clock::now();
        auto appLogicDuration = std::chrono::duration<double, std::milli>(appLogicEndTime - appLogicStartTime);
        double pollAndPlaybackTimeMs = appLogicDuration.count();


        drawFrame();


        appLogicStartTime = steady_clock::now();
        if (m_audio && !paused && m_playbackController && m_playbackController->getFirstFrameMediaTimestampOfSegment().has_value()) {
            int64_t elapsed_ns_since_segment_start = 0;
            if (m_playbackController->getPlaybackMode() == PlaybackController::PlaybackMode::REALTIME) {
                auto wall_anchor = m_playbackController->getWallClockAnchorForSegment();
                auto current_time_for_audio = steady_clock::now();
                elapsed_ns_since_segment_start = std::chrono::duration_cast<std::chrono::nanoseconds>(current_time_for_audio - wall_anchor).count();
#ifndef NDEBUG
                LogToFile(std::string("[App::run -> AudioUpdate] WallAnchorEpochNs (Playback): ") + std::to_string(wall_anchor.time_since_epoch().count()) +
                    ", CurrentTimeEpochNs: " + std::to_string(current_time_for_audio.time_since_epoch().count()) +
                    ", Passed ElapsedNsForAudio: " + std::to_string(elapsed_ns_since_segment_start));
#endif
            } else {
                elapsed_ns_since_segment_start = static_cast<int64_t>(m_playbackController->getCurrentFrameIndex()) *
                    m_playbackController->getFrameDurationNs();
#ifndef NDEBUG
                LogToFile(std::string("[App::run -> AudioUpdate] Non-realtime mode, elapsedNs: ") + std::to_string(elapsed_ns_since_segment_start));
#endif
            }
            m_audio->updatePlayback(elapsed_ns_since_segment_start);
        }
        appLogicEndTime = steady_clock::now();
        double audioUpdateTimeMs = std::chrono::duration<double, std::milli>(appLogicEndTime - appLogicStartTime).count();
        m_appLogicTimeMs = pollAndPlaybackTimeMs + audioUpdateTimeMs;


        if (!paused && segment_looped_or_ended) {
            LogToFile("[App::run] Segment looped or ended, advancing file or restarting.");
            if (m_fileList.size() > 1) {
                bool tempFirstFileLoaded = m_firstFileLoaded;
                m_firstFileLoaded = true; // Treat as if a file was loaded to allow potential resize
                loadFileAtIndex((m_currentFileIndex + 1) % static_cast<int>(m_fileList.size()));
                m_firstFileLoaded = tempFirstFileLoaded;
            }
            else { // Single file, loop it
                m_playbackStartTime = steady_clock::now();
                if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                    const auto& frames = m_decoderWrapper->getDecoder()->getFrames();
                    if (!frames.empty()) {
                        nlohmann::json firstFrameMetaForPB;
                        RawBytes dummyPixelData; // Not used for metadata parsing here
                        try {
                            // This call to loadFrame is just to get metadata, not actual pixels
                            m_decoderWrapper->getDecoder()->loadFrame(frames.front(), dummyPixelData, firstFrameMetaForPB);
                        }
                        catch (const std::exception& e) {
                            LogToFile(std::string("[App::run] Error loading first frame metadata for loop reset (main decoder): ") + e.what());
                            firstFrameMetaForPB["timestamp"] = frames.front(); // Fallback
                        }
                        LogToFile("[App::run] SINGLE FILE LOOP: -> PlaybackController::processNewSegment. WallTime Anchor: CURRENT_TIME");
                        m_playbackController->processNewSegment(firstFrameMetaForPB, frames.size(), m_playbackStartTime);
                    }
                    else { // No frames in file
                        m_playbackController->processNewSegment({}, 0, m_playbackStartTime);
                    }
                }
                else { // No decoder
                    m_playbackController->processNewSegment({}, 0, m_playbackStartTime);
                }

                if (m_audio && m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                    auto* audio_loader_ref_ptr = m_decoderWrapper->makeFreshAudioLoader();
                    if (audio_loader_ref_ptr) {
                        const auto& video_frames = m_decoderWrapper->getDecoder()->getFrames();
                        int64_t firstVideoFrameTimestampNs = video_frames.empty() ? 0 : video_frames.front();
                        LogToFile(std::string("[App::run] SINGLE FILE LOOP: -> AudioController::reset with firstVideoFrameTsNs: ") + std::to_string(firstVideoFrameTimestampNs));
                        m_audio->setForceMute(false); // Ensure not muted on loop
                        m_audio->reset(audio_loader_ref_ptr, firstVideoFrameTimestampNs);
                    }
                    else {
                        LogToFile("[App::run] Failed to get fresh audio loader for single file loop reset.");
                    }
                }
            }
        }

        if (paused) {
            steady_clock::time_point sleepStart = steady_clock::now();
            std::this_thread::sleep_for(16ms);
            m_sleepTimeMs = std::chrono::duration<double, std::milli>(steady_clock::now() - sleepStart).count();
        }

        loopEndTime = steady_clock::now();
        m_totalLoopTimeMs = std::chrono::duration<double, std::milli>(loopEndTime - loopStartTime).count();


        {
            steady_clock::time_point now = steady_clock::now();
            bool shouldUpdateTitle = false;
            std::string currentTitleString;

            std::ostringstream ss;
            ss << "MotionCam Player -  ";
            if (m_currentFileIndex >= 0 && static_cast<size_t>(m_currentFileIndex) < m_fileList.size()) {
                ss << fs::path(m_fileList[m_currentFileIndex]).filename().string();
                if (m_playbackController && m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                    size_t cur = m_playbackController->getCurrentFrameIndex() + 1;
                    size_t tot = m_decoderWrapper->getDecoder()->getFrames().size();
                    if (tot > 0) {
                        ss << " (" << cur << "/" << tot << ")";
                    }
                    else {
                        ss << " (0 frames)";
                    }
                }
            }
            else {
                ss << "(no file)";
            }
            currentTitleString = ss.str();

            if (currentTitleString != m_lastWindowTitle) {
                shouldUpdateTitle = true;
            }
            else {
                auto titleUpdateElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTitleUpdateTime).count();
                if (titleUpdateElapsed > 1000) {
                    shouldUpdateTitle = true;
                }
            }

            if (shouldUpdateTitle) {
                glfwSetWindowTitle(m_window, currentTitleString.c_str());
                m_lastWindowTitle = currentTitleString;
                m_lastTitleUpdateTime = now;
            }
        }
    }
    LogToFile("[App::run] Exited main loop.");
#ifndef NDEBUG
    std::cout << "[App::run] Exited main loop." << std::endl;
#endif

    return true;
}


void App::drawFrame() {
    using steady_clock = std::chrono::steady_clock;
    steady_clock::time_point timePoint_A, timePoint_B;

    timePoint_A = steady_clock::now();
    VK_APP_CHECK(vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX));
    timePoint_B = steady_clock::now();
    m_gpuWaitTimeMs = std::chrono::duration<double, std::milli>(timePoint_B - timePoint_A).count();


    auto recycleStagingBufferLambda = [&](const GpuUploadPacket& packet) {
        m_availableStagingBufferIndices.push(packet.stagingBufferIndex);
        };

    if (m_inFlightStagingBufferIndices[m_currentFrame].has_value()) {
        size_t recycled_idx = m_inFlightStagingBufferIndices[m_currentFrame].value();
        m_availableStagingBufferIndices.push(recycled_idx);
        m_inFlightStagingBufferIndices[m_currentFrame] = std::nullopt;
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);


    if (result == VK_ERROR_OUT_OF_DATE_KHR) { LogToFile("[App::drawFrame] vkAcquireNextImageKHR: VK_ERROR_OUT_OF_DATE_KHR, recreating swapchain."); recreateSwapChain(); return; }
    else if (result == VK_SUBOPTIMAL_KHR) { LogToFile("[App::drawFrame] vkAcquireNextImageKHR: VK_SUBOPTIMAL_KHR, will present but recreate swapchain later."); m_framebufferResized = true; }
    else if (result != VK_SUCCESS) { LogToFile("[App::drawFrame] Failed to acquire swap chain image! Result: " + std::to_string(result)); throw std::runtime_error("Failed to acquire swap chain image!"); }

    VK_APP_CHECK(vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]));
    VK_APP_CHECK(vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0));

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK_APP_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    GpuUploadPacket packetToRender;
    bool renderContentFromPacket = false;
    bool needsFreshUploadFromStaging = false;
    size_t currentActiveFileLoadID = m_activeFileLoadID;


    steady_clock::time_point RcvTimeStart = steady_clock::now();

    if (m_playbackController && !m_playbackController->isPaused()) {
        GpuUploadPacket candidatePacket;
        size_t targetDisplayIndex = m_playbackController->getCurrentFrameIndex();
        bool foundSuitableNewPacketInQueue = false;
        const int max_pop_attempts = kNumPersistentStagingBuffers;

        for (int attempt = 0; attempt < max_pop_attempts && m_gpuUploadQueue.try_pop(candidatePacket); ++attempt) {
            if (candidatePacket.fileLoadID != currentActiveFileLoadID) {
                recycleStagingBufferLambda(candidatePacket);
                continue;
            }

            bool isFirstFrameForThisFileLoad = (!m_hasLastSuccessfullyUploadedPacket.load(std::memory_order_acquire) || m_lastSuccessfullyUploadedPacket.fileLoadID != currentActiveFileLoadID);

            if (isFirstFrameForThisFileLoad) {
                if (candidatePacket.frameIndex == targetDisplayIndex ||
                    (candidatePacket.frameIndex < targetDisplayIndex && (targetDisplayIndex - candidatePacket.frameIndex) <= MAX_LAG_FRAMES + 4) ||
                    (candidatePacket.frameIndex > targetDisplayIndex && (candidatePacket.frameIndex - targetDisplayIndex) <= MAX_LEAD_FRAMES / 2 + 2))
                {
                    packetToRender = candidatePacket;
                    foundSuitableNewPacketInQueue = true;
                    break;
                }
                else {
                    recycleStagingBufferLambda(candidatePacket);
                    continue;
                }
            }
            else {
                if (candidatePacket.frameIndex + MAX_LAG_FRAMES < targetDisplayIndex) {
                    recycleStagingBufferLambda(candidatePacket);
                    continue;
                }
                if (candidatePacket.frameIndex > targetDisplayIndex + MAX_LEAD_FRAMES) {
                    m_gpuUploadQueue.push_front(std::move(candidatePacket));
                    continue; // This was 'break' before, should be 'continue' to allow other packets to be checked if this one is too far ahead.
                }
                packetToRender = candidatePacket;
                foundSuitableNewPacketInQueue = true;
                break;
            }
        }


        if (foundSuitableNewPacketInQueue) {
            renderContentFromPacket = true;
            needsFreshUploadFromStaging = true;
            m_lastSuccessfullyUploadedPacket = packetToRender;
            m_hasLastSuccessfullyUploadedPacket.store(true, std::memory_order_release);
            m_inFlightStagingBufferIndices[m_currentFrame] = packetToRender.stagingBufferIndex;
        }
        else if (m_hasLastSuccessfullyUploadedPacket.load(std::memory_order_acquire) && m_lastSuccessfullyUploadedPacket.fileLoadID == currentActiveFileLoadID) {
            packetToRender = m_lastSuccessfullyUploadedPacket;
            renderContentFromPacket = true;
            needsFreshUploadFromStaging = false;
        }
    }
    else if (m_playbackController && m_playbackController->isPaused()) {
        GpuUploadPacket candidatePausedPacket;
        bool foundSpecificPausedFrame = false;
        if (m_gpuUploadQueue.try_pop(candidatePausedPacket)) {
            if (candidatePausedPacket.fileLoadID == currentActiveFileLoadID &&
                candidatePausedPacket.frameIndex == m_playbackController->getCurrentFrameIndex()) {
                packetToRender = candidatePausedPacket;
                renderContentFromPacket = true;
                needsFreshUploadFromStaging = true;
                m_lastSuccessfullyUploadedPacket = packetToRender;
                m_hasLastSuccessfullyUploadedPacket.store(true, std::memory_order_release);
                m_inFlightStagingBufferIndices[m_currentFrame] = packetToRender.stagingBufferIndex;
                foundSpecificPausedFrame = true;
            }
            else {
                if (candidatePausedPacket.fileLoadID == currentActiveFileLoadID) {
                    m_gpuUploadQueue.push_front(std::move(candidatePausedPacket));
                }
                else {
                    recycleStagingBufferLambda(candidatePausedPacket);
                }
            }
        }

        if (!foundSpecificPausedFrame && m_hasLastSuccessfullyUploadedPacket.load(std::memory_order_acquire) && m_lastSuccessfullyUploadedPacket.fileLoadID == currentActiveFileLoadID) {
            packetToRender = m_lastSuccessfullyUploadedPacket;
            renderContentFromPacket = true;
            needsFreshUploadFromStaging = false;
        }
    }

    steady_clock::time_point RcvTimeEnd = steady_clock::now();
    m_decodeTimeMs = std::chrono::duration<double, std::milli>(RcvTimeEnd - RcvTimeStart).count();


    if (renderContentFromPacket) {
        m_decodedWidth = packetToRender.width;
        m_decodedHeight = packetToRender.height;
    }
    else {
        m_decodedWidth = 0;
        m_decodedHeight = 0;
    }

    VkRenderPassBeginInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_swapChainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = { 0,0 };
    rpInfo.renderArea.extent = m_swapChainExtent;
    VkClearValue clearColorValue{};

    timePoint_A = steady_clock::now();
    if (renderContentFromPacket) {
        VkBuffer stagingBufferToUseForUpload = VK_NULL_HANDLE;
        if (needsFreshUploadFromStaging) {
            if (packetToRender.stagingBufferIndex < m_persistentStagingBuffers.size()) {
                stagingBufferToUseForUpload = m_persistentStagingBuffers[packetToRender.stagingBufferIndex].buffer;
            }
            else {
                LogToFile("[App::drawFrame] ERROR: Invalid stagingBufferIndex " + std::to_string(packetToRender.stagingBufferIndex) + ". Will clear screen.");
                renderContentFromPacket = false;
                if (m_inFlightStagingBufferIndices[m_currentFrame].has_value() &&
                    m_inFlightStagingBufferIndices[m_currentFrame].value() == packetToRender.stagingBufferIndex) {
                    m_availableStagingBufferIndices.push(packetToRender.stagingBufferIndex);
                    m_inFlightStagingBufferIndices[m_currentFrame] = std::nullopt;
                }
            }
        }

        if (renderContentFromPacket) {
            m_rendererVk->prepareAndUploadFrameData(
                cmd, m_currentFrame,
                stagingBufferToUseForUpload,
                packetToRender.width, packetToRender.height, packetToRender.metadata,
                m_staticBlack, m_staticWhite, m_cfaOverride.value_or(m_cfaTypeFromMetadata),
                needsFreshUploadFromStaging
            );
            clearColorValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
        }
        else {
            clearColorValue.color = { {0.1f, 0.1f, 0.1f, 1.0f} };
        }
    }
    else {
        clearColorValue.color = { {0.1f, 0.1f, 0.1f, 1.0f} };
        if (m_inFlightStagingBufferIndices[m_currentFrame].has_value()) {
            m_availableStagingBufferIndices.push(m_inFlightStagingBufferIndices[m_currentFrame].value());
            m_inFlightStagingBufferIndices[m_currentFrame] = std::nullopt;
        }
    }


    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearColorValue;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (renderContentFromPacket) {
        m_rendererVk->recordDrawCommands(cmd, m_currentFrame, m_windowWidth, m_windowHeight);
    }

    if (m_showUI) {
        GuiOverlay::beginFrame();
        GuiOverlay::render(this);
        GuiOverlay::endFrame(cmd);
    }
    vkCmdEndRenderPass(cmd);

    timePoint_B = steady_clock::now();
    m_renderPrepTimeMs = std::chrono::duration<double, std::milli>(timePoint_B - timePoint_A).count();


    VK_APP_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame];

    timePoint_A = steady_clock::now();
    VK_APP_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]));

    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    timePoint_B = steady_clock::now();
    m_vkSubmitPresentTimeMs = std::chrono::duration<double, std::milli>(timePoint_B - timePoint_A).count();


    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        LogToFile(std::string("[App::drawFrame] Swapchain out of date/suboptimal/resized. Recreating. Result: ") + std::to_string(result));
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        LogToFile("[App::drawFrame] Failed to present swap chain image! Result: " + std::to_string(result));
        throw std::runtime_error("Failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}