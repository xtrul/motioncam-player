// FILE: src/Gui/GuiRender.cpp
#define IMGUI_DEFINE_MATH_OPERATORS

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "Gui/GuiOverlay.h"
#include "Gui/GuiStyles.h"
#include "Gui/GuiUtils.h"
#include "Utils/IconsMaterial.h"
#include "Utils/DebugLog.h" // For LogToFile, if logging is desired

#include "App/App.h"
#include "Playback/PlaybackController.h"
#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"


#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#endif

namespace fs = std::filesystem;

namespace GuiOverlay {

    bool show_playlist_aux = false;

    void beginFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    UIData gatherData(App* appInstance) {
        UIData data = {};
        if (!appInstance) return data;

        PlaybackController* playbackController = appInstance->m_playbackController_ptr;
        DecoderWrapper* decoderWrapper = appInstance->m_decoderWrapper_ptr;


        if (playbackController) {
            data.isPaused = playbackController->isPaused();
            data.isZoomedToNative = playbackController->isZoomNativePixels();
            data.currentFrameIndex = playbackController->getCurrentFrameIndex();
            data.playbackMode = static_cast<int>(playbackController->getPlaybackMode());
        }
        else {
            data.isPaused = true;
            data.isZoomedToNative = false;
            data.currentFrameIndex = 0;
        }
        data.actualDisplayFps = PlaybackController::getDisplayFps();


        if (appInstance->m_currentFileIndex >= 0 && static_cast<size_t>(appInstance->m_currentFileIndex) < appInstance->m_fileList.size()) {
            try { data.currentFileName = fs::path(appInstance->m_fileList[appInstance->m_currentFileIndex]).filename().string(); }
            catch (const std::exception&) { data.currentFileName = "Error"; }
        }
        else { data.currentFileName = "N/A"; }

        if (decoderWrapper && decoderWrapper->getDecoder()) {
            const auto& frames = decoderWrapper->getDecoder()->getFrames();
            data.totalFramesInFile = frames.size();
            int64_t firstFrameTsOfSegment = 0;
            if (playbackController) {
                if (auto optTs = playbackController->getFirstFrameMediaTimestampOfSegment()) {
                    firstFrameTsOfSegment = *optTs;
                }
                else if (!frames.empty()) {
                    firstFrameTsOfSegment = frames.front();
                }
            }
            else if (!frames.empty()) {
                firstFrameTsOfSegment = frames.front();
            }


            if (data.totalFramesInFile > 0 && data.currentFrameIndex < data.totalFramesInFile) {
                if (data.currentFrameIndex < frames.size()) {
                    int64_t current_ts_in_file = frames[data.currentFrameIndex];
                    data.currentVideoTimeSec = static_cast<double>(current_ts_in_file - firstFrameTsOfSegment) * 1e-9;
                    if (data.currentVideoTimeSec < 0.0) data.currentVideoTimeSec = 0.0;
                    data.videoTimestampStr = GuiUtils::formatHMS(current_ts_in_file - firstFrameTsOfSegment);
                }
                else {
                    data.currentVideoTimeSec = 0.0; data.videoTimestampStr = GuiUtils::formatHMS(0);
                }
            }
            else {
                data.currentVideoTimeSec = 0.0; data.videoTimestampStr = GuiUtils::formatHMS(0);
            }
            if (data.totalFramesInFile >= 2) {
                if (!frames.empty()) {
                    data.totalDurationSec = static_cast<double>(frames.back() - firstFrameTsOfSegment) * 1e-9;
                    if (data.totalDurationSec < 0.0) data.totalDurationSec = 0.0;
                    if (data.totalDurationSec > 1e-6 && data.totalFramesInFile > 1) {
                        data.capturedFps = (static_cast<double>(data.totalFramesInFile - 1) / data.totalDurationSec);
                    }
                    else { data.capturedFps = 0.0; }
                }
                else {
                    data.totalDurationSec = 0.0; data.capturedFps = 0.0;
                }
            }
            else { data.totalDurationSec = 0.0; data.capturedFps = 0.0; }
        }
        else {
            data.totalFramesInFile = 0;
            data.currentVideoTimeSec = 0.0; data.videoTimestampStr = GuiUtils::formatHMS(0);
            data.totalDurationSec = 0.0; data.capturedFps = 0.0;
        }

        if (appInstance->m_audio) {
            int64_t audioAnchorMediaTs = appInstance->m_audio->getAudioAnchorTimestampNs();
            int64_t lastQueuedAudioOffsetFromAnchor = appInstance->m_audio->getLastQueuedTimestamp();
            int64_t actualLastQueuedAudioMediaTs = audioAnchorMediaTs + lastQueuedAudioOffsetFromAnchor;
            data.audioTimestampStr = GuiUtils::formatHMS(actualLastQueuedAudioMediaTs - (playbackController ? playbackController->getFirstFrameMediaTimestampOfSegment().value_or(0) : 0));
        }
        else {
            data.audioTimestampStr = GuiUtils::formatHMS(0);
        }

        if (playbackController && decoderWrapper && decoderWrapper->getDecoder() && appInstance->m_audio) {
            const auto& frames = decoderWrapper->getDecoder()->getFrames();
            if (data.currentFrameIndex < frames.size()) {
                int64_t currentActualVideoMediaTs = frames[data.currentFrameIndex];
                int64_t audioAnchorMediaTs = appInstance->m_audio->getAudioAnchorTimestampNs();
                int64_t lastQueuedAudioOffsetFromAnchor = appInstance->m_audio->getLastQueuedTimestamp();
                int64_t actualLastQueuedAudioMediaTs = audioAnchorMediaTs + lastQueuedAudioOffsetFromAnchor;

                double deltaAvSyncSec = static_cast<double>(actualLastQueuedAudioMediaTs - currentActualVideoMediaTs) * 1e-9;
                std::ostringstream oss; oss << std::showpos << std::fixed << std::setprecision(3) << deltaAvSyncSec << "s";
                data.avSyncDeltaStr = oss.str();
            }
            else { data.avSyncDeltaStr = "N/A (idx err)"; }
        }
        else { data.avSyncDeltaStr = "N/A"; }

        data.cfaOverride = appInstance->m_cfaOverride;
        data.cfaFromMetadataStr = appInstance->m_cfaStringFromMetadata;
        data.isFullscreen = appInstance->m_isFullscreen;
        data.showMetrics = appInstance->m_showMetrics;
        data.showHelpPage = appInstance->m_showHelpPage;

        data.decodedWidth = appInstance->m_decodedWidth;
        data.decodedHeight = appInstance->m_decodedHeight;

        data.totalLoopTimeMs = appInstance->m_totalLoopTimeMs;
        data.gpuWaitTimeMs = appInstance->m_gpuWaitTimeMs;
        data.decodeTimeMs = appInstance->m_decodeTimeMs;
        data.renderPrepTimeMs = appInstance->m_renderPrepTimeMs;
        data.guiRenderTimeMs = appInstance->m_guiRenderTimeMs;
        data.vkSubmitPresentTimeMs = appInstance->m_vkSubmitPresentTimeMs;
        data.appLogicTimeMs = appInstance->m_appLogicTimeMs;
        data.sleepTimeMs = appInstance->m_sleepTimeMs;

        return data;
    }


    void render(App* appInstance) {
#ifdef MOTIONCAM_CONVERTER
        if (!appInstance) return;
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowBgAlpha(0.f);

        // Main background window
        ImGui::Begin("ConverterMain", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

        const float fileListWidth = 300.0f;
        const float exportPanelHeight = 150.0f;
        const float bottomLogHeight = 150.0f;

        // --- Left Panel: File List ---
        ImGui::BeginChild("FileListPanel", ImVec2(fileListWidth, 0), true);
        {
            if (ImGui::Button("Add", ImVec2(ImGui::GetContentRegionAvail().x * 0.33f, 0))) {
                appInstance->triggerOpenFileViaDialog();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
                 if (appInstance->m_selectedBatchIndex >= 0 && appInstance->m_selectedBatchIndex < (int)appInstance->m_fileList.size()) {
                    appInstance->m_fileList.erase(appInstance->m_fileList.begin() + appInstance->m_selectedBatchIndex);
                    if (appInstance->m_selectedBatchIndex < (int)appInstance->m_fileExportFormats.size())
                        appInstance->m_fileExportFormats.erase(appInstance->m_fileExportFormats.begin() + appInstance->m_selectedBatchIndex);
                    appInstance->m_selectedBatchIndex = -1;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(-1, 0))) {
                appInstance->m_fileList.clear();
                appInstance->m_fileExportFormats.clear();
                appInstance->m_selectedBatchIndex = -1;
            }
            ImGui::Separator();

            ImGui::BeginChild("ScrollingFileList");
            for (int i = 0; i < (int)appInstance->m_fileList.size(); ++i) {
                bool sel = (i == appInstance->m_selectedBatchIndex);
                std::string name = std::filesystem::path(appInstance->m_fileList[i]).filename().string();
                if (ImGui::Selectable(name.c_str(), sel)) {
                    if (appInstance->m_selectedBatchIndex != i) {
                        appInstance->m_selectedBatchIndex = i;
                        bool oldFirst = appInstance->m_firstFileLoaded;
                        appInstance->m_firstFileLoaded = true;
                        appInstance->loadFileAtIndex(i);
                        appInstance->m_firstFileLoaded = oldFirst;
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Right Panel: Preview and Controls ---
        ImGui::BeginChild("MainContentPanel");
        {
            // --- Top-Right: Preview and Controls ---
            ImGui::BeginChild("PreviewAndControls", ImVec2(0, -bottomLogHeight), true);
            {
                // Collapsing header for the preview
                if (ImGui::CollapsingHeader("Preview", &appInstance->m_previewOpen, ImGuiTreeNodeFlags_DefaultOpen)) {
                    // This child defines the black area for Vulkan rendering
                    ImGui::BeginChild("PreviewArea", ImVec2(0, -80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImVec2 innerPos = ImGui::GetCursorScreenPos();
                    ImVec2 innerSize = ImGui::GetContentRegionAvail();
                    appInstance->m_previewRect.x = (int)innerPos.x;
                    appInstance->m_previewRect.y = (int)innerPos.y;
                    appInstance->m_previewRect.w = (int)innerSize.x;
                    appInstance->m_previewRect.h = (int)innerSize.y;
                    ImGui::EndChild();
                } else {
                    // If preview is closed, don't render video
                    appInstance->m_previewRect.w = 0;
                    appInstance->m_previewRect.h = 0;
                }

                // Playback Controls
                bool paused = true;
                if (appInstance->m_playbackController_ptr)
                    paused = appInstance->m_playbackController_ptr->isPaused();
                if (ImGui::Button(paused ? ICON_MD_PLAY_ARROW " Play" : ICON_MD_PAUSE " Pause")) {
                    if (appInstance->m_playbackController_ptr)
                        appInstance->m_playbackController_ptr->togglePause();
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    if (appInstance->m_playbackController_ptr) {
                        appInstance->performSeek(0);
                        if (!appInstance->m_playbackController_ptr->isPaused())
                            appInstance->m_playbackController_ptr->togglePause();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Prev Clip")) { /* ... existing code ... */ }
                ImGui::SameLine();
                if (ImGui::Button("Next Clip")) { /* ... existing code ... */ }

                // Seek Bar
                size_t curIdx = 0, total = 1;
                if (appInstance->m_playbackController_ptr && appInstance->m_decoderWrapper_ptr && appInstance->m_decoderWrapper_ptr->getDecoder()) {
                    curIdx = appInstance->m_playbackController_ptr->getCurrentFrameIndex();
                    total = appInstance->m_decoderWrapper_ptr->getDecoder()->getFrames().size();
                    if (total == 0) total = 1;
                }
                int current_frame_int = static_cast<int>(curIdx);
                ImGui::PushItemWidth(-1);
                if (ImGui::SliderInt("##Seek", &current_frame_int, 0, static_cast<int>(total) - 1)) {
                    appInstance->performSeek(static_cast<size_t>(current_frame_int));
                }
                ImGui::PopItemWidth();
            }
            ImGui::EndChild();

            // --- Bottom-Right: Export and Log ---
            ImGui::BeginChild("ExportAndLog", ImVec2(0, 0), true);
            {
                // Export Settings
                if (appInstance->m_selectedBatchIndex >= 0 && appInstance->m_selectedBatchIndex < (int)appInstance->m_fileExportFormats.size()) {
                    int fmt = static_cast<int>(appInstance->m_fileExportFormats[appInstance->m_selectedBatchIndex]);
                    ImGui::Text("Export Format for selected clip:");
                    ImGui::RadioButton("ProRes (CPU)", &fmt, (int)App::ExportFormat::PRORES_CPU); ImGui::SameLine();
                    ImGui::RadioButton("ProRes (GPU)", &fmt, (int)App::ExportFormat::PRORES_GPU); ImGui::SameLine();
                    ImGui::RadioButton("DNxHR (CPU)", &fmt, (int)App::ExportFormat::DNXHR_CPU); ImGui::SameLine();
                    ImGui::RadioButton("DNxHR (GPU)", &fmt, (int)App::ExportFormat::DNXHR_GPU);
                    ImGui::RadioButton("HEVC (GPU)", &fmt, (int)App::ExportFormat::HEVC_GPU); ImGui::SameLine();
                    ImGui::RadioButton("DNGs", &fmt, (int)App::ExportFormat::DNG);
                    appInstance->m_fileExportFormats[appInstance->m_selectedBatchIndex] = static_cast<App::ExportFormat>(fmt);
                } else {
                    ImGui::TextDisabled("Select a file to see export options.");
                }

                ImGui::InputText("Output Folder", appInstance->m_outputFolder, IM_ARRAYSIZE(appInstance->m_outputFolder));
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    std::string folder = appInstance->openFolderDialog();
                    if (!folder.empty()) strncpy(appInstance->m_outputFolder, folder.c_str(), sizeof(appInstance->m_outputFolder)-1);
                }
                if (ImGui::Button("Convert All", ImVec2(-1, 30)) && !appInstance->m_batchActive.load()) {
                    appInstance->startBatchConversion();
                }

                ImGui::Separator();
                ImGui::Text("Log:");
                ImGui::BeginChild("Log", ImVec2(0,0), true, ImGuiWindowFlags_HorizontalScrollbar);
                for (const std::string& line : appInstance->m_batchLog) ImGui::TextUnformatted(line.c_str());
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::End();

#else
        // ... This is the player UI, which is not being used with MOTIONCAM_CONVERTER defined.
        // No changes needed here.
#endif
    }

    void endFrame(VkCommandBuffer commandBuffer) {
        ImVec4 originalWindowBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImVec4 originalText = ImGui::GetStyle().Colors[ImGuiCol_Text];

        // This gamma correction for ImGui was likely specific to a particular display setup
        // and might not be universally needed or correct. It's generally better to handle
        // gamma correction in the final display stage if possible, or ensure Vulkan swapchain
        // is sRGB if ImGui outputs sRGB colors.
        // For now, keeping it as it was in the original user-provided code base.
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(
            powf(originalWindowBg.x, 2.2f),
            powf(originalWindowBg.y, 2.2f),
            powf(originalWindowBg.z, 2.2f),
            originalWindowBg.w
        );
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(
            powf(originalText.x, 2.2f),
            powf(originalText.y, 2.2f),
            powf(originalText.z, 2.2f),
            originalText.w
        );

        ImGui::Render();

        // Restore original colors
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = originalWindowBg;
        ImGui::GetStyle().Colors[ImGuiCol_Text] = originalText;

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

} // namespace GuiOverlay
