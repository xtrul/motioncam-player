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
#include "Graphics/Renderer_VK.h"


#include <imgui.h>
#include <imgui_internal.h> // ImRect
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#ifndef IMGUI_HAS_DOCK
#define IMGUI_HAS_DOCK 0
#endif
#ifndef ImGuiWindowFlags_NoDocking
#define ImGuiWindowFlags_NoDocking 0
#endif
#ifndef ImGuiDockNodeFlags_None
#define ImGuiDockNodeFlags_None 0
#endif

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
        if (!appInstance) return;

        if (appInstance->m_previewFullscreen) {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;
            
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
            ImGui::Begin("FullscreenPreview", nullptr, flags);
            
            ImTextureID texID = (ImTextureID)appInstance->getRenderer()->getPreviewDescriptorSet();
            if (texID) {
                ImGui::Image(texID, viewport->WorkSize);
            }

            // Store the video rect for controls
            ImRect videoRect{ ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };

            // Render controls over the video
            renderPlaybackControls(appInstance, videoRect, true);

            ImGui::End();
            ImGui::PopStyleVar();
        } else {

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 vp = viewport->WorkPos;
        ImVec2 vs = viewport->WorkSize;

        // Layout constants
        const float rightPanelWidth = 340.0f; // Fixed width for side panels
        const float filesPanelHeight = 260.0f; // Fixed height for Files panel
        const float controlsPanelMinHeight = 200.0f; // Minimum height for Controls panel
        static float logPanelHeight = 180.0f; // Default height for Log panel
        const float panelSpacing = 3.0f;

        float previewWidth = vs.x - rightPanelWidth - panelSpacing;
        // With scrubber overlay the preview only leaves room for the log pane
        float previewHeight = vs.y - logPanelHeight - panelSpacing;
        float controlsPanelHeight = vs.y - filesPanelHeight - panelSpacing * 2;
        if (controlsPanelHeight < controlsPanelMinHeight) controlsPanelHeight = controlsPanelMinHeight;

        ImGuiWindowFlags fixed_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

        // 1. Preview Panel
        ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
        ImGui::SetNextWindowSize(ImVec2(previewWidth, previewHeight));
        ImGui::Begin("Preview", nullptr, fixed_flags);
        {
            ImVec2 size = ImGui::GetContentRegionAvail();
            ImTextureID texID = (ImTextureID)appInstance->getRenderer()->getPreviewDescriptorSet();
            if (texID)
                ImGui::Image(texID, size);

            ImRect videoRect{ ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };

            renderPlaybackControls(appInstance, videoRect, false);
        }
        ImGui::End();



        // 3. Files Panel (fixed size, top right)
        ImGui::SetNextWindowPos(ImVec2(vp.x + previewWidth + panelSpacing, vp.y));
        ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, filesPanelHeight));
        ImGui::Begin("Files", nullptr, fixed_flags);
        {
            bool exporting = appInstance->m_batchActive.load() || appInstance->m_singleExportActive.load()
#ifdef ENABLE_PRORES_EXPORT
                || appInstance->m_proResStatus.active.load() || appInstance->m_dnxhrStatus.active.load() || appInstance->m_hevcStatus.active.load()
#endif
                ;
            ImGui::BeginDisabled(exporting);
            if (ImGui::Button("Add")) { appInstance->triggerOpenFileViaDialog(); }
            ImGui::SameLine();
            if (ImGui::Button("Remove") && appInstance->m_selectedBatchIndex != -1) {
                appInstance->m_fileList.erase(appInstance->m_fileList.begin() + appInstance->m_selectedBatchIndex);
                appInstance->m_fileExportFormats.erase(appInstance->m_fileExportFormats.begin() + appInstance->m_selectedBatchIndex);
                appInstance->m_selectedBatchIndex = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                appInstance->m_fileList.clear();
                appInstance->m_fileExportFormats.clear();
                appInstance->m_selectedBatchIndex = -1;
            }
            ImGui::Separator();
            ImGui::BeginChild("FileListScrollingRegion");
            for (int i = 0; i < (int)appInstance->m_fileList.size(); ++i) {
                ImGui::PushID(i);
                bool is_selected = (i == appInstance->m_selectedBatchIndex);
                std::string name = fs::path(appInstance->m_fileList[i]).filename().string();
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    if (appInstance->m_selectedBatchIndex != i) {
                        appInstance->m_selectedBatchIndex = i;
                        appInstance->loadFileAtIndex(i);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndDisabled();
        }
        ImGui::End();

        // 4. Controls & Export Panel (fixed width, fills right column below Files)
        ImGui::SetNextWindowPos(ImVec2(vp.x + previewWidth + panelSpacing, vp.y + filesPanelHeight + panelSpacing));
        ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, controlsPanelHeight));
        ImGui::Begin("Controls & Export", nullptr, fixed_flags);
        {
            bool exporting = appInstance->m_batchActive.load() || appInstance->m_singleExportActive.load()
#ifdef ENABLE_PRORES_EXPORT
                || appInstance->m_proResStatus.active.load() || appInstance->m_dnxhrStatus.active.load() || appInstance->m_hevcStatus.active.load()
#endif
                ;

            ImGui::BeginDisabled(exporting);
            if (appInstance->m_selectedBatchIndex != -1) {
                int fmt = (int)appInstance->m_fileExportFormats[appInstance->m_selectedBatchIndex];
                static const char* exportFormatNames[] = {
                    "ProRes (CPU)", "ProRes (GPU)", "DNxHR (CPU)", "DNxHR (GPU)", "HEVC (CPU)", "HEVC (GPU)", "DNGs"
                };
                ImGui::Text("Export Format:");
                if (ImGui::Combo("##ExportFormatCombo", &fmt, exportFormatNames, IM_ARRAYSIZE(exportFormatNames))) {
                    appInstance->m_fileExportFormats[appInstance->m_selectedBatchIndex] = (App::ExportFormat)fmt;
                }
            }
            ImGui::InputText("Output Folder", appInstance->m_outputFolder, sizeof(appInstance->m_outputFolder));
            if (ImGui::IsItemActivated() && ImGui::IsMouseDoubleClicked(0)) {
                std::string folder = appInstance->openFolderDialog();
                if (!folder.empty()) strncpy(appInstance->m_outputFolder, folder.c_str(), sizeof(appInstance->m_outputFolder)-1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string folder = appInstance->openFolderDialog();
                if (!folder.empty()) strncpy(appInstance->m_outputFolder, folder.c_str(), sizeof(appInstance->m_outputFolder)-1);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.33f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.38f, 1.00f));
            if (ImGui::Button("Convert All") && !exporting) {
                appInstance->startBatchConversion();
            }
            ImGui::SameLine();
            if (ImGui::Button("Convert Selected") && appInstance->m_selectedBatchIndex != -1 && !exporting) {
                appInstance->startSingleConversion(appInstance->m_selectedBatchIndex);
            }
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();

            if (exporting) {
                if (ImGui::Button("Stop Conversion", ImVec2(-1,0))) {
                    appInstance->stopAllExports();
                }
                float cur = static_cast<float>(appInstance->getCurrentFileProgress());
                ImGui::ProgressBar(cur, ImVec2(-1,0));
                float batch = static_cast<float>(appInstance->getBatchProgress());
                ImGui::ProgressBar(batch, ImVec2(-1,0));
                ImGui::Text("%s", appInstance->m_currentExportingFileName.c_str());
                double eta = appInstance->calculateTimeRemaining();
                if (eta > 0.0) ImGui::Text("ETA %.1fs", eta);
            }
        }
        ImGui::End();

        // 5. Log Panel (bottom left, only as wide as Preview)
        ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y + previewHeight + panelSpacing));
        ImGui::SetNextWindowSize(ImVec2(previewWidth, logPanelHeight));
        ImGui::Begin("Log", nullptr, fixed_flags);
        {
            ImGui::BeginChild("LogScrollingRegion", ImVec2(0,0), true);
            for (const auto& line : appInstance->m_batchLog) ImGui::TextUnformatted(line.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();

            // --- POLISH BEGIN ---
            float logPad = ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImVec2 oldPos = ImGui::GetCursorPos();
            ImVec2 winSize = ImGui::GetWindowSize();
            ImVec2 clearButtonSize = ImGui::CalcTextSize("Clear Logs");
            clearButtonSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
            clearButtonSize.y += ImGui::GetStyle().FramePadding.y * 2.0f;
            ImGui::SetCursorPos(ImVec2(winSize.x - clearButtonSize.x - logPad, winSize.y - clearButtonSize.y - logPad));
            if (ImGui::Button("Clear Logs")) appInstance->m_batchLog.clear();
            ImGui::SetCursorPos(oldPos);
            // --- POLISH END ---
        }
        ImGui::End();
    }
}
    void renderPlaybackControls(App* appInstance, const ImRect& videoRect, bool isFullscreen) {
        bool paused = appInstance->m_playbackController_ptr ? appInstance->m_playbackController_ptr->isPaused() : true;
        size_t curIdx = 0, total = 0;
        bool hasVideo = false;

        if (appInstance->m_playbackController_ptr && appInstance->m_decoderWrapper_ptr && appInstance->m_decoderWrapper_ptr->getDecoder()) {
            total = appInstance->m_decoderWrapper_ptr->getDecoder()->getFrames().size();
            if (total > 0) {
                curIdx = appInstance->m_playbackController_ptr->getCurrentFrameIndex();
                hasVideo = true;
            }
        }

        if (!hasVideo) return;

        float progress = (total > 1) ? (float)curIdx / (float)(total - 1) : 0.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float controlsHeight = 60.0f;
        const float margin = 15.0f;
        const float barHeight = 3.0f;
        const float handleRadius = 6.0f;
        const float iconSize = 24.0f;

        float controlsY = videoRect.Max.y - controlsHeight;

        // Semi-transparent background for controls
        dl->AddRectFilled(ImVec2(videoRect.Min.x, controlsY), videoRect.Max, IM_COL32(0, 0, 0, 100));

        // Play/Pause Button
        ImGui::SetCursorScreenPos(ImVec2(videoRect.Min.x + margin, controlsY + (controlsHeight - iconSize) / 2.0f));
        ImGui::PushFont(GuiStyles::G_LargeIconFont);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.2f));
        if (ImGui::Button(paused ? ICON_MD_PLAY_ARROW : ICON_MD_PAUSE, ImVec2(iconSize, iconSize))) {
            appInstance->m_playbackController_ptr->togglePause();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopFont();

        // Timestamp
        int64_t firstFrameTs = 0, curTs = 0, totalTs = 0;
        if (appInstance->m_decoderWrapper_ptr && appInstance->m_decoderWrapper_ptr->getDecoder()) {
            const auto& frames = appInstance->m_decoderWrapper_ptr->getDecoder()->getFrames();
            if (!frames.empty()) {
                firstFrameTs = frames.front();
                totalTs = frames.back() - firstFrameTs;
                if (curIdx < frames.size()) curTs = frames[curIdx] - firstFrameTs;
            }
        }
        std::string timeStr = GuiUtils::formatHMS(curTs) + " / " + GuiUtils::formatHMS(totalTs);
        ImVec2 textSize = ImGui::CalcTextSize(timeStr.c_str());
        ImGui::SetCursorScreenPos(ImVec2(videoRect.Min.x + margin + iconSize + 10, controlsY + (controlsHeight - textSize.y) / 2.0f));
        ImGui::TextUnformatted(timeStr.c_str());

        // Right-side Icons
        float rightIconsX = videoRect.Max.x - margin - (isFullscreen ? iconSize : (iconSize * 2 + 10));
        ImGui::SetCursorScreenPos(ImVec2(rightIconsX, controlsY + (controlsHeight - iconSize) / 2.0f));
        ImGui::PushFont(GuiStyles::G_LargeIconFont);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.2f));

        if (!isFullscreen) {
            if (ImGui::Button(ICON_MD_SKIP_NEXT, ImVec2(iconSize, iconSize))) {
                appInstance->loadNextFile();
            }
            ImGui::SameLine();
        }

        if (ImGui::Button(isFullscreen ? ICON_MD_FULLSCREEN_EXIT : ICON_MD_FULLSCREEN, ImVec2(iconSize, iconSize))) {
            appInstance->toggleFullscreenPreview();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopFont();

        // Scrubber Bar
        float barY = controlsY - barHeight - 5;
        float barStartX = videoRect.Min.x + margin;
        float barEndX = videoRect.Max.x - margin;
        ImVec2 barStart(barStartX, barY);
        ImVec2 barEnd(barEndX, barY + barHeight);
        dl->AddRectFilled(barStart, barEnd, IM_COL32(100, 100, 100, 120), barHeight * 0.5f);
        float progressPx = progress * (barEndX - barStartX);
        dl->AddRectFilled(barStart, ImVec2(barStartX + progressPx, barEnd.y), IM_COL32(255, 255, 255, 200), barHeight * 0.5f);
        dl->AddCircleFilled(ImVec2(barStartX + progressPx, barY + barHeight / 2), handleRadius, IM_COL32(255, 255, 255, 255));

        ImGui::SetCursorScreenPos(ImVec2(barStartX, barY - handleRadius));
        ImGui::InvisibleButton("##Scrub", ImVec2(barEndX - barStartX, handleRadius * 2));
        
        bool wasPausedOnActivation = false;
        if (ImGui::IsItemActivated()) {
            wasPausedOnActivation = paused;
            if (!paused && appInstance->m_playbackController_ptr) {
                appInstance->m_playbackController_ptr->togglePause();
            }
        }

        if (ImGui::IsItemActive() && appInstance->m_playbackController_ptr) {
            float t = (ImGui::GetIO().MousePos.x - barStartX) / (barEndX - barStartX);
            t = std::clamp(t, 0.0f, 1.0f);
            int newFrame = static_cast<int>(t * ((total > 0) ? (total - 1) : 0));
            appInstance->performSeek(newFrame);
        }

        if (ImGui::IsItemDeactivated() && !wasPausedOnActivation && appInstance->m_playbackController_ptr) {
            appInstance->m_playbackController_ptr->togglePause();
        }
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

