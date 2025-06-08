#include "App/App.h"
#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"
#include "Playback/PlaybackController.h"
#include "Graphics/Renderer_VK.h"
#include "Utils/DebugLog.h"
#include "Gui/GuiOverlay.h"

#include <imgui.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>
namespace DebugLogHelper {
    extern std::string wstring_to_utf8(const std::wstring& wstr);
}
#endif

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;

void App::framebuffer_size_callback_static(GLFWwindow* window, int width, int height) {
    auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app) app->framebufferSizeCallback(width, height);
}

void App::key_callback_static(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard && key != GLFW_KEY_TAB) {
        return;
    }
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window))) {
            app->handleKey(key, mods);
        }
    }
}

void App::drop_callback_static(GLFWwindow* window, int count, const char** paths) {
    if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window)))
        app->handleDrop(count, paths);
}

void App::mouse_button_callback_static(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) { return; }
    if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window))) {
        app->handleMouseButton(button, action, mods);
    }
}

void App::cursor_pos_callback_static(GLFWwindow* window, double xpos, double ypos) {
    if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window))) {
        app->handleCursorPos(xpos, ypos);
    }
}

void App::framebufferSizeCallback(int width, int height) {
    std::string msg = "[App::framebufferSizeCallback] Framebuffer resized to " + std::to_string(width) + "x" + std::to_string(height);
    LogToFile(msg);
    m_framebufferResized = true;
    if (!m_isFullscreen) {
        m_windowWidth = width;
        m_windowHeight = height;
        m_storedWindowedWidth = width;
        m_storedWindowedHeight = height;
    }
}


void App::handleKey(int key, int mods) {
    if (!m_window || !m_playbackController) return;

    if (key == GLFW_KEY_TAB && mods == 0) {
        m_showUI = !m_showUI;
        LogToFile(std::string("[App::handleKey] UI Toggled: ") + (m_showUI ? "ON" : "OFF"));
        return;
    }
    if (key == GLFW_KEY_M && mods == 0) {
        m_showMetrics = !m_showMetrics;
        LogToFile(std::string("[App::handleKey] Metrics Toggled: ") + (m_showMetrics ? "ON" : "OFF"));
        return;
    }
    if ((key == GLFW_KEY_H && mods == 0) || key == GLFW_KEY_F1) {
        toggleHelpPage();
        if (m_showHelpPage) GuiOverlay::show_playlist_aux = false;
        return;
    }

    if (key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) {
        LogToFile("[App::handleKey] Ctrl+Q pressed. Closing window.");
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }
    if (key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL)) {
        LogToFile("[App::handleKey] Ctrl+O pressed. Triggering open file dialog.");
        triggerOpenFileViaDialog();
        return;
    }

    bool keyHandledByAppLogic = false;
    size_t totalFramesInCurrentFile = 0;

    if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
        totalFramesInCurrentFile = m_decoderWrapper->getDecoder()->getFrames().size();
    }

    bool wasPausedBeforeKeyAction = m_playbackController->isPaused();
    bool seekActionTookPlace = false;

    if (key == GLFW_KEY_LEFT_BRACKET) {
        keyHandledByAppLogic = true;
        LogToFile("[App::handleKey] '[' pressed. Loading previous file.");
        if (!m_fileList.empty()) {
            bool tempFirstLoaded = m_firstFileLoaded;
            m_firstFileLoaded = true;
            loadFileAtIndex((m_currentFileIndex - 1 + static_cast<int>(m_fileList.size())) % static_cast<int>(m_fileList.size()));
            m_firstFileLoaded = tempFirstLoaded;
        }
    }
    else if (key == GLFW_KEY_RIGHT_BRACKET) {
        keyHandledByAppLogic = true;
        LogToFile("[App::handleKey] ']' pressed. Loading next file.");
        if (!m_fileList.empty()) {
            bool tempFirstLoaded = m_firstFileLoaded;
            m_firstFileLoaded = true;
            loadFileAtIndex((m_currentFileIndex + 1) % static_cast<int>(m_fileList.size()));
            m_firstFileLoaded = tempFirstLoaded;
        }
    }
    else if (key == GLFW_KEY_LEFT) {
        keyHandledByAppLogic = true;
        if (totalFramesInCurrentFile > 0) {
            if (!m_playbackController->isPaused()) {
                m_playbackController->togglePause();
                if (m_audio) m_audio->setPaused(true);
            }
            size_t current_idx = m_playbackController->getCurrentFrameIndex();
            size_t new_idx = (current_idx > 0) ? current_idx - 1 : 0;
            performSeek(new_idx);
            seekActionTookPlace = true;
            LogToFile(std::string("[App::handleKey] Left Arrow. Seeked to frame index: ") + std::to_string(new_idx));
        }
    }
    else if (key == GLFW_KEY_RIGHT) {
        keyHandledByAppLogic = true;
        if (totalFramesInCurrentFile > 0) {
            if (!m_playbackController->isPaused()) {
                m_playbackController->togglePause();
                if (m_audio) m_audio->setPaused(true);
            }
            size_t current_idx = m_playbackController->getCurrentFrameIndex();
            size_t new_idx = (current_idx + 1 < totalFramesInCurrentFile) ? current_idx + 1 : (totalFramesInCurrentFile > 0 ? totalFramesInCurrentFile - 1 : 0);
            performSeek(new_idx);
            seekActionTookPlace = true;
            LogToFile(std::string("[App::handleKey] Right Arrow. Seeked to frame index: ") + std::to_string(new_idx));
        }
    }
    else if (key == GLFW_KEY_HOME) {
        keyHandledByAppLogic = true;
        if (totalFramesInCurrentFile > 0) {
            if (!m_playbackController->isPaused()) {
                m_playbackController->togglePause();
                if (m_audio) m_audio->setPaused(true);
            }
            performSeek(0);
            seekActionTookPlace = true;
            LogToFile("[App::handleKey] Home. Seeked to frame index: 0");
        }
    }
    else if (key == GLFW_KEY_END) {
        keyHandledByAppLogic = true;
        if (totalFramesInCurrentFile > 0) {
            if (!m_playbackController->isPaused()) {
                m_playbackController->togglePause();
                if (m_audio) m_audio->setPaused(true);
            }
            size_t last_frame_idx = totalFramesInCurrentFile > 0 ? totalFramesInCurrentFile - 1 : 0;
            performSeek(last_frame_idx);
            seekActionTookPlace = true;
            LogToFile(std::string("[App::handleKey] End. Seeked to frame index: ") + std::to_string(last_frame_idx));
        }
    }
    else if (key == GLFW_KEY_Z) {
        keyHandledByAppLogic = true;
        if (m_playbackController) {
            m_playbackController->toggleZoomNativePixels();
            LogToFile(std::string("[App::handleKey] Z pressed. Zoom native pixels: ") + (m_playbackController->isZoomNativePixels() ? "ON" : "OFF"));
            if (m_rendererVk) {
                m_rendererVk->setZoomNativePixels(m_playbackController->isZoomNativePixels());
                if (m_playbackController->isZoomNativePixels()) {
                    int winW, winH;
                    glfwGetWindowSize(m_window, &winW, &winH);
                    int imgW = m_rendererVk->getImageWidth();
                    int imgH = m_rendererVk->getImageHeight();

                    if (imgW > 0 && imgH > 0) {
                        float pan_x = (static_cast<float>(winW) - static_cast<float>(imgW)) / 2.0f;
                        float pan_y = (static_cast<float>(winH) - static_cast<float>(imgH)) / 2.0f;
                        m_rendererVk->setPanOffsets(pan_x, pan_y);
#ifndef NDEBUG
                        LogToFile(std::string("[App::handleKey] Zoom ON. Centered pan: ") + std::to_string(pan_x) + ", " + std::to_string(pan_y));
#endif
                    }
                    else {
                        m_rendererVk->resetPanOffsets();
#ifndef NDEBUG
                        LogToFile("[App::handleKey] Zoom ON. No valid image dims, pan reset.");
#endif
                    }
                }
                else {
                    m_rendererVk->resetPanOffsets();
                    if (m_isPanning) { m_isPanning = false; }
#ifndef NDEBUG
                    LogToFile("[App::handleKey] Zoom OFF. Pan reset.");
#endif
                }
            }
        }
    }
    else if (key == GLFW_KEY_0 || key == GLFW_KEY_KP_0) {
        keyHandledByAppLogic = true;
        m_cfaOverride = std::nullopt;
        LogToFile("[App::handleKey] 0 pressed. CFA override disabled (using metadata).");
    }
    else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_4) {
        keyHandledByAppLogic = true;
        m_cfaOverride = key - GLFW_KEY_1;
        LogToFile(std::string("[App::handleKey] ") + std::to_string(key - GLFW_KEY_0) + " pressed. CFA override set to: " + std::to_string(m_cfaOverride.value()));
    }
    else if (key == GLFW_KEY_F || key == GLFW_KEY_F11) {
        keyHandledByAppLogic = true;
        LogToFile(std::string("[App::handleKey] F/F11 pressed. Toggling fullscreen. Was: ") + (m_isFullscreen ? "ON" : "OFF"));
        if (m_isFullscreen) {
            glfwSetWindowMonitor(m_window, nullptr, m_storedWindowedPosX, m_storedWindowedPosY, m_storedWindowedWidth, m_storedWindowedHeight, 0);
            m_isFullscreen = false;
            glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
        }
        else {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            if (monitor) {
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                if (mode) {
                    glfwGetWindowPos(m_window, &m_storedWindowedPosX, &m_storedWindowedPosY);
                    glfwGetWindowSize(m_window, &m_storedWindowedWidth, &m_storedWindowedHeight);
                    glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                    m_isFullscreen = true;
                    glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
                }
            }
        }
        m_framebufferResized = true;
    }
    else if (key == GLFW_KEY_P) {
        keyHandledByAppLogic = true;
        if (m_playbackController) {
            auto current = m_playbackController->getPlaybackMode();
            PlaybackController::PlaybackMode next;
            switch (current) {
            case PlaybackController::PlaybackMode::REALTIME: next = PlaybackController::PlaybackMode::FIXED_24FPS; break;
            case PlaybackController::PlaybackMode::FIXED_24FPS: next = PlaybackController::PlaybackMode::FIXED_30FPS; break;
            case PlaybackController::PlaybackMode::FIXED_30FPS: next = PlaybackController::PlaybackMode::FIXED_60FPS; break;
            case PlaybackController::PlaybackMode::FIXED_60FPS: next = PlaybackController::PlaybackMode::BENCHMARK; break;
            case PlaybackController::PlaybackMode::BENCHMARK: default: next = PlaybackController::PlaybackMode::REALTIME; break;
            }
            setPlaybackMode(next);
        }
    }
    else if (key == GLFW_KEY_ESCAPE) {
        keyHandledByAppLogic = true;
        if (m_isFullscreen) {
            LogToFile("[App::handleKey] ESC pressed. Exiting fullscreen.");
            glfwSetWindowMonitor(m_window, nullptr, m_storedWindowedPosX, m_storedWindowedPosY, m_storedWindowedWidth, m_storedWindowedHeight, 0);
            m_isFullscreen = false;
            glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
            m_framebufferResized = true;
        }
        else if (m_showHelpPage) {
            LogToFile("[App::handleKey] ESC pressed. Closing help page.");
            m_showHelpPage = false;
        }
        else if (GuiOverlay::show_playlist_aux) {
            LogToFile("[App::handleKey] ESC pressed. Closing auxiliary playlist.");
            GuiOverlay::show_playlist_aux = false;
        }
        else {
            LogToFile("[App::handleKey] ESC pressed. Closing window.");
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }
    else if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) {
        keyHandledByAppLogic = true;
        LogToFile("[App::handleKey] DELETE/BACKSPACE pressed. Attempting soft delete.");
        softDeleteCurrentFile();
    }

    if (!keyHandledByAppLogic) {
        m_playbackController->handleKey(key, m_window);
    }

    bool isPausedAfterKeyAction = m_playbackController->isPaused();

    if (isPausedAfterKeyAction != wasPausedBeforeKeyAction) {
        if (m_audio) m_audio->setPaused(isPausedAfterKeyAction);
        if (isPausedAfterKeyAction) {
            recordPauseTime();
        }
        else {
            anchorPlaybackTimeForResume();
        }
        m_ioThreadFileCv.notify_all();
    }
    else if (seekActionTookPlace && isPausedAfterKeyAction) {
    }
}

void App::handleDrop(int count, const char** paths) {
    if (count <= 0) return;
    std::string firstValidPathDropped;
    bool newFilesAddedToPlaylist = false;

    for (int i = 0; i < count; ++i) {
        if (paths[i] == nullptr) continue;
        try {
            fs::path p = fs::absolute(paths[i]);
            if (p.extension() == ".mcraw" && fs::is_regular_file(p)) {
                std::string s = p.string();
                if (firstValidPathDropped.empty()) {
                    firstValidPathDropped = s;
                }
                if (std::find(m_fileList.begin(), m_fileList.end(), s) == m_fileList.end()) {
                    m_fileList.push_back(s);
                    newFilesAddedToPlaylist = true;
                }
            }
        }
        catch (const fs::filesystem_error& e) {
            LogToFile(std::string("[App::handleDrop] Filesystem error processing dropped path '") + paths[i] + "': " + e.what());
            std::cerr << "Error processing dropped path '" << paths[i] << "': " << e.what() << std::endl;
        }
    }

    if (newFilesAddedToPlaylist) {
        std::sort(m_fileList.begin(), m_fileList.end());
        LogToFile("[App::handleDrop] New files added to playlist and sorted.");
    }

    if (!firstValidPathDropped.empty()) {
        auto it = std::find(m_fileList.begin(), m_fileList.end(), firstValidPathDropped);
        if (it != m_fileList.end()) {
            bool tempFirstLoaded = m_firstFileLoaded;
            m_firstFileLoaded = false;
            loadFileAtIndex(static_cast<int>(std::distance(m_fileList.begin(), it)));
            m_firstFileLoaded = tempFirstLoaded;
            LogToFile(std::string("[App::handleDrop] Loaded dropped file: ") + firstValidPathDropped);
        }
    }
}


void App::handleMouseButton(int button, int action, int mods) {
    if (!m_rendererVk || !m_playbackController || !m_playbackController->isZoomNativePixels()) {
        if (m_isPanning) { m_isPanning = false; }
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            m_isPanning = true;
            glfwGetCursorPos(m_window, &m_lastMouseX, &m_lastMouseY);
        }
        else if (action == GLFW_RELEASE) {
            if (m_isPanning) { m_isPanning = false; }
        }
    }
}

void App::handleCursorPos(double xpos, double ypos) {
    if (m_isPanning && m_rendererVk && m_playbackController && m_playbackController->isZoomNativePixels()) {
        double dx = xpos - m_lastMouseX;
        double dy = ypos - m_lastMouseY;
        m_rendererVk->setPanOffsets(m_rendererVk->getPanX() + static_cast<float>(dx),
            m_rendererVk->getPanY() + static_cast<float>(dy));
        m_lastMouseX = xpos;
        m_lastMouseY = ypos;
    }
    else if (m_isPanning) {
        m_isPanning = false;
    }
}

std::string App::openMcrawDialog() {
#ifdef _WIN32
    OPENFILENAMEW ofn{};
    wchar_t szFile[MAX_PATH] = { 0 };
    ofn.lStructSize = sizeof(ofn);

    HWND ownerHwnd = NULL;
    if (m_window) {
        ownerHwnd = glfwGetWin32Window(m_window);
    }

    ofn.hwndOwner = ownerHwnd;
    ofn.lpstrFilter = L"MotionCam RAW files\0*.mcraw\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"mcraw";

    if (GetOpenFileNameW(&ofn)) {
        std::string utf8Path = DebugLogHelper::wstring_to_utf8(szFile);
        if (utf8Path.empty() && szFile[0] != L'\0') {
            LogToFile("[App::openMcrawDialog] UTF-8 conversion failed for selected path.");
            return {};
        }
        return utf8Path;
    }
#else
    LogToFile("[App::openMcrawDialog] File dialog not implemented for this platform.");
    std::cerr << "File dialog not implemented for this platform." << std::endl;
#endif
    return {};
}

void App::triggerOpenFileViaDialog() {
    std::string newPath = openMcrawDialog();
    if (!newPath.empty()) {
        auto it_existing = std::find(m_fileList.begin(), m_fileList.end(), newPath);
        if (it_existing == m_fileList.end()) {
            m_fileList.push_back(newPath);
            std::sort(m_fileList.begin(), m_fileList.end());
            it_existing = std::find(m_fileList.begin(), m_fileList.end(), newPath);
        }
        if (it_existing != m_fileList.end()) {
            bool tempFirstLoaded = m_firstFileLoaded;
            m_firstFileLoaded = false;
            loadFileAtIndex(static_cast<int>(std::distance(m_fileList.begin(), it_existing)));
            m_firstFileLoaded = tempFirstLoaded;
        }
    }
}