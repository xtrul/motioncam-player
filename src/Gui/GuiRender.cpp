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

        UIData ui = gatherData(appInstance);

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
            
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 vp = viewport->WorkPos;
        ImVec2 vs = viewport->WorkSize;

        // Layout constants
        static float rightPanelWidth = 340.0f; // User-resizable width for side panels
        const float rightPanelMinWidth = 220.0f;
        const float rightPanelMaxWidth = vs.x * 0.6f;
        const float filesPanelHeight = 260.0f; // Fixed height for Files panel
        const float controlsPanelMinHeight = 200.0f; // Minimum height for Controls panel
        static float logPanelHeight = 120.0f; // User adjustable
        const float logPanelMinHeight = 26.0f;
        const float panelSpacing = 3.0f;

        float previewWidth = vs.x - rightPanelWidth - panelSpacing;
        // Drag handle to resize the right panel
        ImGui::SetCursorScreenPos(ImVec2(vp.x + previewWidth, vp.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0,0,0,0));
        ImGui::InvisibleButton("RightPanelResize", ImVec2(panelSpacing, vs.y));
        if (ImGui::IsItemActive()) {
            rightPanelWidth -= ImGui::GetIO().MouseDelta.x;
            if (rightPanelWidth < rightPanelMinWidth) rightPanelWidth = rightPanelMinWidth;
            if (rightPanelWidth > rightPanelMaxWidth) rightPanelWidth = rightPanelMaxWidth;
        }
        ImGui::PopStyleColor(3);
        previewWidth = vs.x - rightPanelWidth - panelSpacing;
        float previewHeight = vs.y - logPanelHeight - panelSpacing;
        float controlsPanelHeight = vs.y - filesPanelHeight - panelSpacing * 2;
        if (controlsPanelHeight < controlsPanelMinHeight) controlsPanelHeight = controlsPanelMinHeight;

        ImGuiWindowFlags fixed_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

        // 1. Preview Panel
        ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y));
        ImGui::SetNextWindowSize(ImVec2(previewWidth, previewHeight));
        ImGui::Begin("Preview", nullptr, fixed_flags);
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImTextureID texID = (ImTextureID)appInstance->getRenderer()->getPreviewDescriptorSet();
            if (texID)
            {
                float imgW = (float)ui.decodedWidth;
                float imgH = (float)ui.decodedHeight;
                ImVec2 display = avail;
                if (imgW > 0.0f && imgH > 0.0f)
                {
                    float imgAspect = imgW / imgH;
                    if (ui.isZoomedToNative)
                    {
                        display.x = imgW;
                        display.y = imgH;
                    }
                    else
                    {
                        float availAspect = avail.x / avail.y;
                        if (imgAspect > availAspect)
                        {
                            display.x = avail.x;
                            display.y = avail.x / imgAspect;
                        }
                        else
                        {
                            display.y = avail.y;
                            display.x = avail.y * imgAspect;
                        }
                    }

                    ImVec2 start = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(start + ImVec2((avail.x - display.x)*0.5f, (avail.y - display.y)*0.5f));
                    ImGui::Image(texID, display);
                    ImGui::SetCursorScreenPos(start);
                }
                else
                {
                    ImGui::Image(texID, avail);
                }
            }
        }
        ImGui::End();

        // 2. Timeline/Play controls (floating overlay)
        const float timelineHeight = 32.0f;
        const float timelineWidth = previewWidth * 0.55f;
        ImVec2 overlayPos(vp.x + (previewWidth - timelineWidth) * 0.5f,
                          vp.y + previewHeight - timelineHeight - 6.0f);
        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowPos(overlayPos);
        ImGui::SetNextWindowSize(ImVec2(timelineWidth, timelineHeight));
        static float timelineAlpha = 0.6f;
        float targetAlpha = 0.15f;
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::IsAnyItemActive())
            targetAlpha = 0.9f;
        timelineAlpha += (targetAlpha - timelineAlpha) * ImGui::GetIO().DeltaTime * 5.0f;
        ImGui::SetNextWindowBgAlpha(timelineAlpha);
        ImGui::Begin("TimelineControls", nullptr, overlayFlags);
        {
            bool paused = appInstance->m_playbackController_ptr ? appInstance->m_playbackController_ptr->isPaused() : true;
            if (ImGui::Button(paused ? ICON_MD_PLAY_ARROW : ICON_MD_PAUSE)) {
                if (appInstance->m_playbackController_ptr) {
                    bool wasPaused = paused;
                    appInstance->m_playbackController_ptr->togglePause();
                    bool nowPaused = appInstance->m_playbackController_ptr->isPaused();
                    if (nowPaused != wasPaused) {
                        if (appInstance->m_audio) appInstance->m_audio->setPaused(nowPaused);
                        if (nowPaused) appInstance->recordPauseTime();
                        else appInstance->anchorPlaybackTimeForResume();
                    }
                }
            }
            ImGui::SameLine();
            size_t curIdx = 0, total = 0;
            if (appInstance->m_playbackController_ptr && appInstance->m_decoderWrapper_ptr && appInstance->m_decoderWrapper_ptr->getDecoder()) {
                curIdx = appInstance->m_playbackController_ptr->getCurrentFrameIndex();
                total = appInstance->m_decoderWrapper_ptr->getDecoder()->getFrames().size();
            }
            int frame_int = (int)curIdx;
            ImGui::PushItemWidth(-1);
            if (ImGui::SliderInt("##Seek", &frame_int, 0, (total > 0) ? (int)total - 1 : 0, "")) {
                if (ImGui::IsItemActive()) {
                    if (!paused) appInstance->m_playbackController_ptr->togglePause();
                    appInstance->performSeek(frame_int);
                }
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            bool zoomNative = ui.isZoomedToNative;
            if (ImGui::Checkbox("Zoom 100%", &zoomNative)) {
                if (appInstance->m_playbackController_ptr) {
                    appInstance->m_playbackController_ptr->toggleZoomNativePixels();
                    appInstance->getRenderer()->setZoomNativePixels(zoomNative);
                }
            }
            ImGui::SameLine();
            ImGui::Text("%s / %s", ui.videoTimestampStr.c_str(), GuiUtils::format_mm_ss(ui.totalDurationSec).c_str());
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
            ImGui::BeginChild("FileListScrollingRegion", ImVec2(0,0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            for (int i = 0; i < (int)appInstance->m_fileList.size(); ++i) {
                bool is_selected = (i == appInstance->m_selectedBatchIndex);
                std::string name = fs::path(appInstance->m_fileList[i]).filename().string();
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    if (appInstance->m_selectedBatchIndex != i) {
                        appInstance->m_selectedBatchIndex = i;
                        appInstance->loadFileAtIndex(i);
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", appInstance->m_fileList[i].c_str());
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
            ImGui::Separator();
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
            ImGui::InputText("Choose Output Folder", appInstance->m_outputFolder, sizeof(appInstance->m_outputFolder));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", appInstance->m_outputFolder);
            if (ImGui::IsItemActivated() && ImGui::IsMouseDoubleClicked(0)) {
                std::string folder = appInstance->openFolderDialog();
                if (!folder.empty()) strncpy(appInstance->m_outputFolder, folder.c_str(), sizeof(appInstance->m_outputFolder)-1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string folder = appInstance->openFolderDialog();
                if (!folder.empty()) strncpy(appInstance->m_outputFolder, folder.c_str(), sizeof(appInstance->m_outputFolder)-1);
            }
            ImVec4 btn = ImVec4(0.3f, 0.4f, 0.6f, 1.0f);
            ImVec4 hover = ImVec4(0.35f, 0.45f, 0.65f, 1.0f);
            ImVec4 active = ImVec4(0.2f, 0.3f, 0.5f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, btn);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
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

        // 5. Log Panel (bottom left, adjustable height)
        ImGui::SetNextWindowPos(ImVec2(vp.x, vp.y + previewHeight + panelSpacing));
        ImGui::SetNextWindowSizeConstraints(ImVec2(previewWidth, logPanelMinHeight), ImVec2(previewWidth, vs.y));
        ImGui::SetNextWindowSize(ImVec2(previewWidth, logPanelHeight));
        static bool logExpanded = false;
        ImGuiWindowFlags logFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Log", nullptr, logFlags);
        logPanelHeight = ImGui::GetWindowSize().y;
        if (!logExpanded) {
            std::string lastLine = appInstance->m_batchLog.empty() ? "Idle" : appInstance->m_batchLog.back();
            ImVec4 col = ImVec4(0.8f,0.8f,0.8f,1.0f);
            if (lastLine.find("Error") != std::string::npos) col = ImVec4(1.0f,0.3f,0.3f,1.0f);
            else if (lastLine.find("Warning") != std::string::npos) col = ImVec4(1.0f,0.7f,0.2f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Text("Status: %s", lastLine.c_str());
            ImGui::PopStyleColor();
            if (ImGui::InvisibleButton("ExpandLog", ImGui::GetContentRegionAvail())) logExpanded = true;
        } else {
            if (ImGui::Button("Collapse")) logExpanded = false;
            ImGui::SameLine();
            if (ImGui::Button("Clear")) appInstance->m_batchLog.clear();
            ImGui::BeginChild("LogScrollingRegion", ImVec2(0,0), false);
            for (const auto& line : appInstance->m_batchLog) {
                ImVec4 col = ImVec4(0.8f,0.8f,0.8f,1.0f);
                if (line.find("Error") != std::string::npos) col = ImVec4(1.0f,0.3f,0.3f,1.0f);
                else if (line.find("Warning") != std::string::npos) col = ImVec4(1.0f,0.7f,0.2f,1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleVar();
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
