// --- START OF FILE src/GuiOverlay.cpp ---

#define IMGUI_DEFINE_MATH_OPERATORS

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "GuiOverlay.h"
#include "IconsMaterial.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "App.h"
#include "PlaybackController.h"
#include "AudioController.h"
#include "DecoderWrapper.h"
#include "Renderer_VK.h"

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

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#endif

namespace fs = std::filesystem;

static constexpr float  PILL_RADIUS = 18.0f;
static constexpr float  PANEL_HORIZONTAL_PADDING = 24.0f;
static constexpr float  PANEL_VERTICAL_PADDING = 14.0f;
static const ImVec2 G_LARGE_BUTTON_PADDING = ImVec2(5.0f, 5.0f);
static const ImVec2 G_SMALL_BUTTON_PADDING = ImVec2(0.5f, 0.5f);
static const ImVec2 G_AUX_OVERLAY_BUTTON_PADDING = ImVec2(2.0f, 2.0f);

static ImFont* G_TextFont = nullptr;
static ImFont* G_LargeIconFont = nullptr;
static ImFont* G_SmallIconFont = nullptr;
static ImFont* G_AuxOverlayIconFont = nullptr;

static float G_BaseTextFontSize = 18.0f;
static float G_LargeIconFontSize = G_BaseTextFontSize * 1.8f;
static float G_SmallIconFontSize = G_BaseTextFontSize * 0.90f;
static float G_AuxOverlayIconFontSize = G_BaseTextFontSize * 0.80f;

#ifndef ICON_MD_KEYBOARD_ARROW_LEFT
#define ICON_MD_KEYBOARD_ARROW_LEFT u8"\uE314"
#endif
#ifndef ICON_MD_KEYBOARD_ARROW_RIGHT
#define ICON_MD_KEYBOARD_ARROW_RIGHT u8"\uE315"
#endif
#ifndef ICON_MD_INSIGHTS
#define ICON_MD_INSIGHTS u8"\uF09C"
#endif
#ifndef ICON_MD_HELP_OUTLINE
#define ICON_MD_HELP_OUTLINE u8"\uE8FD"
#endif
#ifndef ICON_MD_SAVE
#define ICON_MD_SAVE u8"\ue161"
#endif
#ifndef ICON_MD_COLLECTIONS
#define ICON_MD_COLLECTIONS u8"\ue3b7"
#endif

namespace {
    std::string formatHMS(int64_t ns) { if (ns < 0) ns = 0; double s_total = static_cast<double>(ns) * 1e-9; bool is_negative = s_total < 0; if (is_negative) s_total = -s_total; int h = static_cast<int>(s_total / 3600.0); s_total -= static_cast<double>(h) * 3600.0; int m = static_cast<int>(s_total / 60.0); s_total -= static_cast<double>(m) * 60.0; double sec_frac = s_total; std::ostringstream oss; if (is_negative) oss << "-"; oss << std::setfill('0') << std::setw(2) << h << ':' << std::setw(2) << m << ':' << std::fixed << std::setprecision(3) << std::setw(6) << sec_frac; return oss.str(); }
    std::string format_mm_ss(double total_seconds) { if (total_seconds < 0) total_seconds = 0; int minutes = static_cast<int>(total_seconds) / 60; int seconds = static_cast<int>(total_seconds) % 60; char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds); return std::string(buf); }
}


bool GuiOverlay::show_playlist_aux = false;
static bool g_show_context_menu = false;
static ImVec2 g_context_menu_pos = ImVec2(0.0f, 0.0f);

void GuiOverlay::requestContextMenu(float x, float y) {
    g_show_context_menu = true;
    g_context_menu_pos = ImVec2(x, y);
}

void GuiOverlay::setup(GLFWwindow* window, App* appInstance) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    std::string roboto_regular_font_path_str;
    std::string icon_font_load_path_str;
    
    fs::path exePath;
    #ifdef _WIN32
        char modulePath[MAX_PATH];
        GetModuleFileNameA(NULL, modulePath, MAX_PATH);
        exePath = fs::path(modulePath).parent_path();
    #elif defined(__APPLE__)
        exePath = fs::current_path();
    #else
        exePath = fs::current_path();
    #endif

    fs::path font_dir_candidate1 = exePath / "fonts";
    fs::path font_dir_candidate2 = exePath / "assets";
    fs::path font_dir_candidate3 = exePath / "Resources" / "fonts";
    fs::path font_dir_candidate4 = exePath / "Resources" / "assets";

    fs::path final_font_dir;
    if (fs::exists(font_dir_candidate3 / "Roboto-Regular.ttf")) final_font_dir = font_dir_candidate3;
    else if (fs::exists(font_dir_candidate1 / "Roboto-Regular.ttf")) final_font_dir = font_dir_candidate1;
    else if (fs::exists(font_dir_candidate4 / "Roboto-Regular.ttf")) final_font_dir = font_dir_candidate4;
    else if (fs::exists(font_dir_candidate2 / "Roboto-Regular.ttf")) final_font_dir = font_dir_candidate2;
    else final_font_dir = "assets";


    roboto_regular_font_path_str = (final_font_dir / "Roboto-Regular.ttf").string();
    icon_font_load_path_str = (final_font_dir / "MaterialIcons-Regular.ttf").string();

    if (!fs::exists(roboto_regular_font_path_str)) {
        std::cerr << "ERROR: Roboto-Regular.ttf not found at " << roboto_regular_font_path_str << std::endl;
        roboto_regular_font_path_str = "assets/Roboto-Regular.ttf";
    }
     if (!fs::exists(icon_font_load_path_str)) {
        std::cerr << "ERROR: MaterialIcons-Regular.ttf not found at " << icon_font_load_path_str << std::endl;
        icon_font_load_path_str = "assets/MaterialIcons-Regular.ttf";
    }


    ImFontConfig text_font_config; text_font_config.SizePixels = G_BaseTextFontSize; G_TextFont = io.Fonts->AddFontFromFileTTF(roboto_regular_font_path_str.c_str(), G_BaseTextFontSize, &text_font_config); IM_ASSERT(G_TextFont != nullptr && "Failed to load text font");
    static const ImWchar icons_ranges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };
    float dpi_scale = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
    ImFontConfig large_icons_config; large_icons_config.PixelSnapH = true; large_icons_config.GlyphOffset.y = -1.0f * dpi_scale; G_LargeIconFont = io.Fonts->AddFontFromFileTTF(icon_font_load_path_str.c_str(), G_LargeIconFontSize, &large_icons_config, icons_ranges); IM_ASSERT(G_LargeIconFont != nullptr && "Failed to load large icon font");
    ImFontConfig small_icons_config; small_icons_config.PixelSnapH = true; small_icons_config.GlyphOffset.y = 0.0f * dpi_scale; G_SmallIconFont = io.Fonts->AddFontFromFileTTF(icon_font_load_path_str.c_str(), G_SmallIconFontSize, &small_icons_config, icons_ranges); IM_ASSERT(G_SmallIconFont != nullptr && "Failed to load small icon font");
    ImFontConfig aux_overlay_icons_config; aux_overlay_icons_config.PixelSnapH = true; aux_overlay_icons_config.GlyphOffset.y = 0.0f * dpi_scale; G_AuxOverlayIconFont = io.Fonts->AddFontFromFileTTF(icon_font_load_path_str.c_str(), G_AuxOverlayIconFontSize, &aux_overlay_icons_config, icons_ranges); IM_ASSERT(G_AuxOverlayIconFont != nullptr && "Failed to load aux icon font");

    ImGuiStyle& style = ImGui::GetStyle(); style.WindowRounding = 10.0f; style.ChildRounding = 8.0f; style.PopupRounding = 8.0f; style.FrameRounding = 16.0f; style.GrabRounding = 16.0f; style.ScrollbarRounding = 8.0f; style.WindowBorderSize = 0.0f; style.ChildBorderSize = 0.0f; style.PopupBorderSize = 1.0f; style.FrameBorderSize = 0.0f; style.WindowPadding = ImVec2(12.0f, 12.0f); style.FramePadding = ImVec2(8.0f, 6.0f); style.ItemSpacing = ImVec2(8.0f, 8.0f); style.ItemInnerSpacing = ImVec2(6.0f, 6.0f); style.ScrollbarSize = 16.0f; style.GrabMinSize = 12.0f; style.ButtonTextAlign = ImVec2(0.5f, 0.5f); ImVec4* colors = style.Colors; colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f); colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f); colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f); colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.95f); colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f); colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); colors[ImGuiCol_FrameBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f); colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f); colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f); colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f); colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.30f, 0.53f, 1.00f); colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f); colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.06f, 0.07f, 1.00f); colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.53f); colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f); colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f); colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.35f, 0.38f, 1.00f); colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f); colors[ImGuiCol_SliderGrabActive] = ImVec4(0.25f, 0.55f, 0.95f, 1.00f); colors[ImGuiCol_Button] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.05f); colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f); colors[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.85f, 0.45f); colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.50f, 0.90f, 0.80f); colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.45f, 0.85f, 1.00f); colors[ImGuiCol_Separator] = colors[ImGuiCol_Border]; colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f); colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f); colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f); colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f); colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f); colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f); colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f); colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f); colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f); colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f); colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f); colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = appInstance->m_vkInstance;
    init_info.PhysicalDevice = appInstance->m_physicalDevice;
    init_info.Device = appInstance->m_device;
    App::QueueFamilyIndices indices = appInstance->findQueueFamilies(appInstance->m_physicalDevice);
    init_info.QueueFamily = indices.graphicsFamily.value();
    init_info.Queue = appInstance->m_graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = appInstance->m_imguiDescriptorPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = static_cast<uint32_t>(appInstance->m_swapChainImages.size());
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = [](VkResult err) {
        if (err == 0) return;
        fprintf(stderr, "[ImGui Vulkan] Error: VkResult = %d\n", err);
        if (err < 0) abort();
        };
    init_info.RenderPass = appInstance->m_renderPass;
    ImGui_ImplVulkan_Init(&init_info);

    // CORRECTED Font upload sequence:
    // Call CreateFontsTexture(). This records commands into an internal command buffer (usually).
    // The application is then responsible for submitting these commands.
    // The simplest way is to get a one-time command buffer from the app,
    // tell ImGui to record into it (if the backend supports it, some older ones don't and do it internally),
    // then submit and wait.
    // The standard ImGui_ImplVulkan_CreateFontsTexture() (no args) implies it does its own thing.
    // After that, we must ensure the queue it used is idle *before* destroying upload objects.
    
    // Simpler approach: Call it, then ensure queue is idle, then destroy upload objects.
    // This assumes ImGui_ImplVulkan_CreateFontsTexture submits to init_info.Queue.
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        std::cerr << "ERROR: ImGui_ImplVulkan_CreateFontsTexture failed!" << std::endl;
        // Handle error
    }
    // Wait for the queue to ensure font upload commands have finished
    ImGui_ImplVulkan_DestroyFontsTexture(); // Use the queue passed to ImGui

}

void GuiOverlay::cleanup() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void GuiOverlay::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

GuiOverlay::UIData GuiOverlay::gatherData(App* appInstance) {
    UIData data = {};
    if (!appInstance) return data;

    if (appInstance->m_playbackController) {
        data.isPaused = appInstance->m_playbackController->isPaused();
        data.isZoomedToNative = appInstance->m_playbackController->isZoomNativePixels();
        data.currentFrameIndex = appInstance->m_playbackController->getCurrentFrameIndex();
    } else {
        data.isPaused = true; data.isZoomedToNative = false; data.currentFrameIndex = 0;
    }
    data.actualDisplayFps = PlaybackController::getDisplayFps();


    if (appInstance->m_currentFileIndex >= 0 && static_cast<size_t>(appInstance->m_currentFileIndex) < appInstance->m_fileList.size()) {
        try { data.currentFileName = fs::path(appInstance->m_fileList[appInstance->m_currentFileIndex]).filename().string(); }
        catch (const std::exception&) { data.currentFileName = "Error"; }
    } else { data.currentFileName = "N/A"; }

    if (appInstance->m_decoderWrapper && appInstance->m_decoderWrapper->getDecoder()) {
        const auto& frames = appInstance->m_decoderWrapper->getDecoder()->getFrames();
        data.totalFramesInFile = frames.size();
        int64_t firstFrameTs = 0;
        if (appInstance->m_playbackController) {
            if (auto optTs = appInstance->m_playbackController->getFirstFrameMediaTimestampOfSegment()) {
                firstFrameTs = *optTs;
            } else if (!frames.empty()) { firstFrameTs = frames.front(); }
        } else if (!frames.empty()) { firstFrameTs = frames.front(); }


        if (data.totalFramesInFile > 0 && data.currentFrameIndex < data.totalFramesInFile) {
            int64_t current_ts_in_file = frames[data.currentFrameIndex];
            data.currentVideoTimeSec = static_cast<double>(current_ts_in_file - firstFrameTs) * 1e-9;
            if (data.currentVideoTimeSec < 0.0) data.currentVideoTimeSec = 0.0;
            data.videoTimestampStr = formatHMS(current_ts_in_file - firstFrameTs);
        } else {
            data.currentVideoTimeSec = 0.0; data.videoTimestampStr = formatHMS(0);
        }
        if (data.totalFramesInFile >= 2) {
            data.totalDurationSec = static_cast<double>(frames.back() - firstFrameTs) * 1e-9;
            if (data.totalDurationSec < 0.0) data.totalDurationSec = 0.0;
            if (data.totalDurationSec > 1e-6 && data.totalFramesInFile > 1) {
                data.capturedFps = (static_cast<double>(data.totalFramesInFile - 1) / data.totalDurationSec);
            } else { data.capturedFps = 0.0; }
        } else { data.totalDurationSec = 0.0; data.capturedFps = 0.0;}
    } else {
        data.totalFramesInFile = 0;
        data.currentVideoTimeSec = 0.0; data.videoTimestampStr = formatHMS(0);
        data.totalDurationSec = 0.0; data.capturedFps = 0.0;
    }

    if (appInstance->m_audio) { data.audioTimestampStr = formatHMS(appInstance->m_audio->getLastQueuedTimestamp()); }
    else { data.audioTimestampStr = formatHMS(0); }

    if (appInstance->m_playbackController && appInstance->m_decoderWrapper && appInstance->m_decoderWrapper->getDecoder() && appInstance->m_audio) {
        const auto& frames = appInstance->m_decoderWrapper->getDecoder()->getFrames();
        if (data.currentFrameIndex < frames.size()) {
            int64_t firstFrameTs = 0;
            if (auto optTs = appInstance->m_playbackController->getFirstFrameMediaTimestampOfSegment()) {
                firstFrameTs = *optTs;
            } else if (!frames.empty()) { firstFrameTs = frames.front(); }

            int64_t currentVideoTsInFile = frames[data.currentFrameIndex];
            int64_t currentVideoTsRelativeToSegmentStart = currentVideoTsInFile - firstFrameTs;
            if (currentVideoTsRelativeToSegmentStart < 0) currentVideoTsRelativeToSegmentStart = 0;

            int64_t audioLastQueuedTs = appInstance->m_audio->getLastQueuedTimestamp();
            double deltaAvSyncSec = static_cast<double>(audioLastQueuedTs - currentVideoTsRelativeToSegmentStart) * 1e-9;
            std::ostringstream oss; oss << std::showpos << std::fixed << std::setprecision(3) << deltaAvSyncSec << "s";
            data.avSyncDeltaStr = oss.str();
        } else { data.avSyncDeltaStr = "N/A"; }
    } else { data.avSyncDeltaStr = "N/A"; }

    data.cfaOverride = appInstance->m_cfaOverride;
    data.cfaFromMetadataStr = appInstance->m_cfaStringFromMetadata;
    data.isFullscreen = appInstance->m_isFullscreen;
    data.showMetrics = appInstance->m_showMetrics;
    data.showHelpPage = appInstance->m_showHelpPage;
    return data;
}

void GuiOverlay::render(App* appInstance) {
    if (!appInstance) return;
    UIData ui = GuiOverlay::gatherData(appInstance);
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();

    if (g_show_context_menu) {
        ImGui::OpenPopup("RIGHT_CLICK_MENU");
        ImGui::SetNextWindowPos(g_context_menu_pos);
        g_show_context_menu = false;
    }
    if (ImGui::BeginPopup("RIGHT_CLICK_MENU")) {
        if (ImGui::MenuItem("Save frame as DNG")) { appInstance->saveCurrentFrameAsDng(); }
        if (ImGui::MenuItem("Send to MotionCam Fuse")) { appInstance->sendCurrentFileToMotionCamFuse(); }
        ImGui::EndPopup();
    }

    float current_playlist_window_width = 0.0f;
    bool playlist_window_is_visible = false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (!appInstance->m_firstFileLoaded) {
        ImVec2 center = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                               viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("STARTUP_SCREEN", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(60/255.0f,60/255.0f,60/255.0f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(80/255.0f,80/255.0f,80/255.0f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(80/255.0f,80/255.0f,80/255.0f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,1.0f,1.0f,1.0f));
        if (ImGui::Button("Open or drag a MCRAW file")) { appInstance->triggerOpenFileViaDialog(); }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        ImGui::End();
    }

    if (GuiOverlay::show_playlist_aux) {
        const float initial_playlist_width = 320.0f * io.FontGlobalScale; float default_playlist_height = viewport->WorkSize.y * 0.80f;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - initial_playlist_width - style.WindowPadding.x, viewport->WorkPos.y + style.WindowPadding.y), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(initial_playlist_width, default_playlist_height), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(initial_playlist_width * 0.5f, 100.0f * io.FontGlobalScale), ImVec2(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y - 2 * style.WindowPadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4,4)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6,4)); ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.09f, 0.95f));
        if (ImGui::Begin("Playlist", &GuiOverlay::show_playlist_aux, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings )) {
            current_playlist_window_width = ImGui::GetWindowSize().x; playlist_window_is_visible = true;
            if (appInstance->m_fileList.empty()) ImGui::TextDisabled(" (empty)");
            else { for (int i = 0; i < static_cast<int>(appInstance->m_fileList.size()); ++i) { const std::string& filePath = appInstance->m_fileList[i]; std::string filename_to_display = fs::path(filePath).stem().string(); bool is_selected = (appInstance->m_currentFileIndex == i); char entry_buf[512]; snprintf(entry_buf, sizeof(entry_buf), "%2d. %s ", i+1, filename_to_display.c_str()); if (is_selected) ImGui::PushStyleColor(ImGuiCol_Header, style.Colors[ImGuiCol_HeaderActive]); if (ImGui::Selectable(entry_buf, is_selected, ImGuiSelectableFlags_SpanAllColumns)) { if (!is_selected) { appInstance->m_firstFileLoaded = false; appInstance->loadFileAtIndex(i); appInstance->m_firstFileLoaded = true; } } if (is_selected) ImGui::PopStyleColor(); } }
        }
        ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    }

    if (ui.showHelpPage) {
        ImGui::SetNextWindowSize(ImVec2(450 * io.FontGlobalScale, 420 * io.FontGlobalScale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x/2 - 225 * io.FontGlobalScale, viewport->WorkPos.y + viewport->WorkSize.y/2 - 210 * io.FontGlobalScale), ImGuiCond_Appearing);

        bool help_open_flag = ui.showHelpPage;
        if (ImGui::Begin("Help - Keyboard Shortcuts", &help_open_flag, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("Playback Controls:");
            ImGui::BulletText("[Space]        : Play / Pause");
            ImGui::BulletText("[Left Arrow]   : Previous Frame (Step Back)");
            ImGui::BulletText("[Right Arrow]  : Next Frame (Step Forward)");
            ImGui::BulletText("[Home]         : Go to First Frame");
            ImGui::BulletText("[End]          : Go to Last Frame");
            ImGui::Separator();
            ImGui::Text("File Navigation:");
            ImGui::BulletText("[[ (L-Bracket)]: Previous File in Playlist");
            ImGui::BulletText("] (R-Bracket)]: Next File in Playlist");
            ImGui::BulletText("[⌘O]           : Open File Dialog");
            ImGui::BulletText("[Delete] or [Backspace] : Delete Current File");
            ImGui::Separator();
            ImGui::Text("Display & UI:");
            ImGui::BulletText("[F] or [F11]   : Toggle Fullscreen");
            ImGui::BulletText("[Z]            : Toggle Zoom (Native Pixels / Fit to Window)");
            ImGui::BulletText("[M]            : Toggle Metrics Overlay");
            ImGui::BulletText("[H] or [F1]    : Toggle This Help Page");
            ImGui::BulletText("[Tab]          : Toggle Main UI Controls");
            ImGui::BulletText("[Esc]          : Exit Fullscreen / Close Popups / Quit");
            ImGui::Separator();
            ImGui::Text("Application:");
            ImGui::BulletText("[⌘Q]           : Quit Application");
            ImGui::BulletText("[0-4]          : Set CFA Override (0 to reset)");
        }
        ImGui::End();
        if (!help_open_flag && ui.showHelpPage) { appInstance->toggleHelpPage(); }
    }


    ImVec2 sizeLargeButton, sizeSmallButton, sizeAuxOverlayButton, sizePlayPauseButton;
    float largeButtonFrameHeight, smallButtonFrameHeight, auxOverlayButtonFrameHeight, playPauseButtonFrameHeight;

    if (G_LargeIconFont) ImGui::PushFont(G_LargeIconFont);
    float large_icon_line_height_sp = ImGui::GetTextLineHeightWithSpacing();
    float large_button_dim = (large_icon_line_height_sp + G_LARGE_BUTTON_PADDING.y * 2.0f) * 1.1f;
    sizeLargeButton = ImVec2(large_button_dim, large_button_dim);
    largeButtonFrameHeight = sizeLargeButton.y;
    sizePlayPauseButton = ImVec2(large_button_dim * 1.30f, large_button_dim * 1.30f);
    playPauseButtonFrameHeight = sizePlayPauseButton.y;
    if (G_LargeIconFont) ImGui::PopFont();

    if (G_SmallIconFont) ImGui::PushFont(G_SmallIconFont);
    float small_icon_line_height_sp = ImGui::GetTextLineHeightWithSpacing();
    sizeSmallButton = ImVec2(small_icon_line_height_sp + G_SMALL_BUTTON_PADDING.x*2.0f + 4.0f, small_icon_line_height_sp + G_SMALL_BUTTON_PADDING.y*2.0f + 2.0f);
    smallButtonFrameHeight = sizeSmallButton.y;
    if (G_SmallIconFont) ImGui::PopFont();

    if (G_AuxOverlayIconFont) ImGui::PushFont(G_AuxOverlayIconFont);
    float aux_overlay_icon_line_height_sp = ImGui::GetTextLineHeightWithSpacing();
    sizeAuxOverlayButton = ImVec2(aux_overlay_icon_line_height_sp + G_AUX_OVERLAY_BUTTON_PADDING.x*2.0f + 3.0f, aux_overlay_icon_line_height_sp + G_AUX_OVERLAY_BUTTON_PADDING.y*2.0f + 1.5f);
    auxOverlayButtonFrameHeight = sizeAuxOverlayButton.y;
    if (G_AuxOverlayIconFont) ImGui::PopFont();

    const float general_inter_button_gap = style.ItemSpacing.x * 1.0f;
    const float tight_inter_button_gap = style.ItemSpacing.x * 0.3f;
    float main_panel_buttons_total_width = sizeLargeButton.x + tight_inter_button_gap + sizeSmallButton.x + general_inter_button_gap + sizePlayPauseButton.x + general_inter_button_gap + sizeSmallButton.x + tight_inter_button_gap + sizeLargeButton.x;
    std::string currentTimeStr = format_mm_ss(ui.currentVideoTimeSec); std::string totalTimeStr = (ui.totalDurationSec > 0) ? format_mm_ss(ui.totalDurationSec) : "00:00"; float current_time_text_width_calc = ImGui::CalcTextSize(currentTimeStr.c_str()).x; float total_time_text_width_calc = ImGui::CalcTextSize(totalTimeStr.c_str()).x; const float min_scrubber_width_pref = 150.0f * io.FontGlobalScale; float time_row_min_width_calc = current_time_text_width_calc + style.ItemSpacing.x + min_scrubber_width_pref + style.ItemSpacing.x + total_time_text_width_calc; float time_row_text_height_calc = ImGui::CalcTextSize("00:00").y;
    float aux_button_effective_item_spacing_x = style.ItemSpacing.x * 0.25f;
    float aux_vertical_spacing_tight = style.ItemSpacing.y * 0.25f;
    float aux_buttons_grid_width = (sizeAuxOverlayButton.x * 2.0f) + aux_button_effective_item_spacing_x;
    float aux_buttons_grid_height = (sizeAuxOverlayButton.y * 3.0f) + (aux_vertical_spacing_tight * 2.0f);

    float base_desired_panel_content_width = std::max(main_panel_buttons_total_width, time_row_min_width_calc);
    float min_content_for_main_and_aux_grid = main_panel_buttons_total_width + aux_buttons_grid_width + style.ItemSpacing.x * 2.0f;
    base_desired_panel_content_width = std::max(base_desired_panel_content_width, min_content_for_main_and_aux_grid);
    base_desired_panel_content_width = std::max(base_desired_panel_content_width, 380.0f * io.FontGlobalScale);

    float final_desired_panel_content_width = base_desired_panel_content_width * 1.15f;
    const float actual_panel_total_width = final_desired_panel_content_width + 2.0f * PANEL_HORIZONTAL_PADDING;
    float actual_main_button_row_max_height = playPauseButtonFrameHeight;
    float main_panel_estimated_content_height = time_row_text_height_calc + style.ItemSpacing.y * 0.5f + std::max(actual_main_button_row_max_height, aux_buttons_grid_height);
    float main_panel_height_for_positioning = main_panel_estimated_content_height + 2.0f * PANEL_VERTICAL_PADDING;
    float main_panel_center_x_coord = viewport->WorkPos.x + viewport->WorkSize.x * 0.5f;
    float main_panel_pos_x_top_left = main_panel_center_x_coord - actual_panel_total_width / 2.0f;
    main_panel_pos_x_top_left = std::max(main_panel_pos_x_top_left, viewport->WorkPos.x + style.WindowPadding.x);
    if (playlist_window_is_visible) {
        float available_width_for_main = viewport->WorkSize.x - current_playlist_window_width - (style.WindowPadding.x * 2.0f);
        if (actual_panel_total_width > available_width_for_main) {
            main_panel_pos_x_top_left = viewport->WorkPos.x + style.WindowPadding.x;
        } else {
            main_panel_center_x_coord = viewport->WorkPos.x + available_width_for_main / 2.0f;
            main_panel_pos_x_top_left = main_panel_center_x_coord - actual_panel_total_width / 2.0f;
        }
        main_panel_pos_x_top_left = std::max(main_panel_pos_x_top_left, viewport->WorkPos.x + style.WindowPadding.x);
    }
    float bottom_margin_percentage = 0.12f;
    float main_panel_pos_y_top_left = viewport->WorkPos.y + viewport->WorkSize.y * (1.0f - bottom_margin_percentage) - main_panel_height_for_positioning;
    main_panel_pos_y_top_left = std::max(main_panel_pos_y_top_left, viewport->WorkPos.y + style.WindowPadding.y);

    ImGui::SetNextWindowPos(ImVec2(main_panel_pos_x_top_left, main_panel_pos_y_top_left), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(actual_panel_total_width, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.10f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.14f, 0.15f, 0.16f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, PILL_RADIUS);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PANEL_HORIZONTAL_PADDING, PANEL_VERTICAL_PADDING));
    ImGuiWindowFlags control_panel_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    float y_base_for_button_row_content_area = 0.0f;

    if (ImGui::Begin("ControlsPanel", nullptr, control_panel_flags)) {
        const float panel_content_width_for_layout = ImGui::GetContentRegionAvail().x;
        const float thinner_slider_padding_y = 1.0f * io.FontGlobalScale;
        float current_time_width = current_time_text_width_calc; float total_time_width = total_time_text_width_calc; float scrubber_width = panel_content_width_for_layout - current_time_width - total_time_width - 2 * style.ItemSpacing.x; scrubber_width = std::max(scrubber_width, 10.0f * io.FontGlobalScale); float full_time_row_width = current_time_width + style.ItemSpacing.x + scrubber_width + style.ItemSpacing.x + total_time_width; float center_x_offset_time_row = (panel_content_width_for_layout - full_time_row_width) / 2.0f; float initial_cursor_y_for_time_row = ImGui::GetCursorPosY(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x_offset_time_row); ImGui::SetCursorPosY(initial_cursor_y_for_time_row); ImGui::TextUnformatted(currentTimeStr.c_str()); ImGui::SameLine(0.0f, style.ItemSpacing.x);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, thinner_slider_padding_y)); float slider_height_with_padding = ImGui::GetFrameHeight(); ImGui::PopStyleVar(); float slider_y_offset = (time_row_text_height_calc - slider_height_with_padding) / 2.0f;
        ImGui::SetCursorPosY(initial_cursor_y_for_time_row + slider_y_offset); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, thinner_slider_padding_y)); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f); ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 8.0f * io.FontGlobalScale); ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f,0.14f,0.16f,1.0f)); ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.02f,0.56f,0.98f,1.0f)); ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.02f,0.56f,0.98f,1.0f)); ImGui::PushItemWidth(scrubber_width);
        if (ui.totalFramesInFile > 0 && appInstance->m_playbackController) {
            int current_frame_idx_slider = static_cast<int>(ui.currentFrameIndex);
            int total_frames_slider = std::max(0, static_cast<int>(ui.totalFramesInFile) -1);
            bool slider_changed = ImGui::SliderInt("##Scrubber", &current_frame_idx_slider, 0, total_frames_slider, "", ImGuiSliderFlags_AlwaysClamp);
            static bool paused_for_scrub = false;
            if (ImGui::IsItemActivated() && !appInstance->m_playbackController->isPaused()) {
                paused_for_scrub = true; appInstance->handleKey(GLFW_KEY_SPACE, 0);
            }
            if (slider_changed) {
                appInstance->m_playbackController->seekFrame(static_cast<size_t>(current_frame_idx_slider), ui.totalFramesInFile);
                if(appInstance->m_playbackController->isPaused()) { appInstance->anchorPlaybackTimeForResume(); }
            }
            if (ImGui::IsItemDeactivated()) {
                if (paused_for_scrub) { appInstance->handleKey(GLFW_KEY_SPACE, 0); paused_for_scrub = false; }
                else if (appInstance->m_playbackController->isPaused()) { appInstance->anchorPlaybackTimeForResume(); }
            }
        } else { int dummy = 0; ImGui::SliderInt("##Timeline", &dummy, 0, 0, "", ImGuiSliderFlags_AlwaysClamp); }
        ImGui::PopItemWidth(); ImGui::PopStyleColor(3); ImGui::PopStyleVar(4); ImGui::SameLine(0.0f, style.ItemSpacing.x); ImGui::SetCursorPosY(initial_cursor_y_for_time_row); ImGui::TextUnformatted(totalTimeStr.c_str());
        ImGui::Dummy(ImVec2(0,style.ItemSpacing.y * 0.5f));
        y_base_for_button_row_content_area = ImGui::GetCursorPosY();
        float center_x_offset_button_row = (panel_content_width_for_layout - main_panel_buttons_total_width) / 2.0f;
        float current_x_for_buttons = ImGui::GetCursorPosX() + center_x_offset_button_row;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
        float y_for_play_pause_button = y_base_for_button_row_content_area;
        float y_for_skip_buttons = y_base_for_button_row_content_area + (playPauseButtonFrameHeight - largeButtonFrameHeight) / 2.0f;
        float y_for_small_buttons = y_base_for_button_row_content_area + (playPauseButtonFrameHeight - smallButtonFrameHeight) / 2.0f;
        ImGui::PushFont(G_LargeIconFont); ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_skip_buttons)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, G_LARGE_BUTTON_PADDING); if (ImGui::Button(ICON_MD_SKIP_PREVIOUS, sizeLargeButton)) appInstance->handleKey(GLFW_KEY_LEFT_BRACKET, 0); ImGui::PopStyleVar(); current_x_for_buttons += sizeLargeButton.x + tight_inter_button_gap; ImGui::PopFont();
        ImGui::PushFont(G_SmallIconFont); ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_small_buttons)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, G_SMALL_BUTTON_PADDING); if (ImGui::Button(ICON_MD_KEYBOARD_ARROW_LEFT, sizeSmallButton)) appInstance->handleKey(GLFW_KEY_LEFT, 0); ImGui::PopStyleVar(); current_x_for_buttons += sizeSmallButton.x + general_inter_button_gap; ImGui::PopFont();
        ImGui::PushFont(G_LargeIconFont); ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_play_pause_button));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sizePlayPauseButton.x * 0.5f); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.01f, 0.45f, 0.88f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.02f, 0.55f, 1.00f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.01f, 0.35f, 0.70f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, G_LARGE_BUTTON_PADDING); if (ImGui::Button(ui.isPaused ? ICON_MD_PLAY_ARROW : ICON_MD_PAUSE, sizePlayPauseButton)) appInstance->handleKey(GLFW_KEY_SPACE, 0);
        ImGui::PopStyleVar(2); ImGui::PopStyleColor(3); current_x_for_buttons += sizePlayPauseButton.x + general_inter_button_gap; ImGui::PopFont();
        ImGui::PushFont(G_SmallIconFont); ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_small_buttons)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, G_SMALL_BUTTON_PADDING); if (ImGui::Button(ICON_MD_KEYBOARD_ARROW_RIGHT, sizeSmallButton)) appInstance->handleKey(GLFW_KEY_RIGHT, 0); ImGui::PopStyleVar(); current_x_for_buttons += sizeSmallButton.x + tight_inter_button_gap; ImGui::PopFont();
        ImGui::PushFont(G_LargeIconFont); ImGui::SetCursorPos(ImVec2(current_x_for_buttons, y_for_skip_buttons)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, G_LARGE_BUTTON_PADDING); if (ImGui::Button(ICON_MD_SKIP_NEXT, sizeLargeButton)) appInstance->handleKey(GLFW_KEY_RIGHT_BRACKET, 0); ImGui::PopStyleVar(); ImGui::PopFont();
        ImGui::PopStyleColor();

        if (G_AuxOverlayIconFont && y_base_for_button_row_content_area > 0.0f) {
            ImVec2 mainPanelWindowPos = ImGui::GetWindowPos();
            ImVec2 contentRegionTopLeftScreenPos = mainPanelWindowPos + ImVec2(PANEL_HORIZONTAL_PADDING, PANEL_VERTICAL_PADDING);
            float main_playback_buttons_row_visual_center_y_offset_in_content = y_base_for_button_row_content_area + playPauseButtonFrameHeight / 2.0f;
            float screen_y_for_aux_grid_center = contentRegionTopLeftScreenPos.y + main_playback_buttons_row_visual_center_y_offset_in_content;
            float screen_y_for_aux_grid_top_row = screen_y_for_aux_grid_center - aux_buttons_grid_height / 2.0f;
            float screen_x_of_content_right_edge = contentRegionTopLeftScreenPos.x + panel_content_width_for_layout;
            float screen_x_for_aux_grid_left_col = screen_x_of_content_right_edge - aux_buttons_grid_width;

            ImGui::PushFont(G_AuxOverlayIconFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,0.75f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, G_AUX_OVERLAY_BUTTON_PADDING);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(aux_button_effective_item_spacing_x, aux_vertical_spacing_tight));
            ImGui::SetCursorScreenPos(ImVec2(screen_x_for_aux_grid_left_col, screen_y_for_aux_grid_top_row));
            bool muted = appInstance->m_audio && appInstance->m_audio->isEffectivelyMuted();
            if (ImGui::Button(muted ? ICON_MD_VOLUME_OFF : ICON_MD_VOLUME_UP, sizeAuxOverlayButton)) { if (appInstance->m_audio) appInstance->m_audio->setForceMute(!muted); }
            ImGui::SameLine(0.0f, aux_button_effective_item_spacing_x);
            if (ImGui::Button(ICON_MD_INSIGHTS, sizeAuxOverlayButton)) { appInstance->m_showMetrics = !appInstance->m_showMetrics; }
            float screen_y_for_aux_grid_middle_row = screen_y_for_aux_grid_top_row + sizeAuxOverlayButton.y + aux_vertical_spacing_tight;
            ImGui::SetCursorScreenPos(ImVec2(screen_x_for_aux_grid_left_col, screen_y_for_aux_grid_middle_row));
            if (ImGui::Button(ICON_MD_HELP_OUTLINE, sizeAuxOverlayButton)) { appInstance->toggleHelpPage(); if (appInstance->m_showHelpPage) GuiOverlay::show_playlist_aux = false; }
            ImGui::SameLine(0.0f, aux_button_effective_item_spacing_x);
            if (ImGui::Button(ICON_MD_MENU, sizeAuxOverlayButton)) { GuiOverlay::show_playlist_aux = !GuiOverlay::show_playlist_aux; if (GuiOverlay::show_playlist_aux && appInstance->m_showHelpPage) appInstance->toggleHelpPage(); }
            ImGui::PopStyleVar(2); ImGui::PopStyleColor(); ImGui::PopFont();
        }
        ImGui::End();
    }
    ImGui::PopStyleVar(3); ImGui::PopStyleColor(2);


    if (ui.showMetrics) {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + style.WindowPadding.x, viewport->WorkPos.y + style.WindowPadding.y), ImGuiCond_Appearing);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::SetNextWindowSizeConstraints(ImVec2(200 * io.FontGlobalScale, 100 * io.FontGlobalScale), ImVec2(viewport->WorkSize.x * 0.8f, viewport->WorkSize.y * 0.8f));
        ImGuiWindowFlags metrics_window_flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("METRICS", &appInstance->m_showMetrics, metrics_window_flags)) {
            ImGui::Text("File: %s", ui.currentFileName.c_str());
            ImGui::Text("Frame: %zu / %zu", ui.currentFrameIndex + (ui.totalFramesInFile > 0 ? 1:0), ui.totalFramesInFile);
            ImGui::Text("Time: %s / %s", ui.videoTimestampStr.c_str(), formatHMS(static_cast<int64_t>(ui.totalDurationSec * 1e9)).c_str());
            ImGui::Separator();
            ImGui::Text("Captured FPS: %.2f", ui.capturedFps);
            ImGui::Text("Display FPS: %.1f", ui.actualDisplayFps);
            ImGui::Text("Audio TS: %s", ui.audioTimestampStr.c_str());
            ImGui::Text("A/V Sync: %s", ui.avSyncDeltaStr.c_str());
            ImGui::Separator();
            ImGui::Text("Loop Times (ms): Total %.1f", appInstance->m_totalLoopTimeMs);
            ImGui::Text("  Decode: %.2f, RenderRec: %.2f", appInstance->m_decodingTimeMs, appInstance->m_renderSubmitTimeMs);
            ImGui::Text("  GPU Wait(Present): %.2f, Sleep: %.1f", appInstance->m_gpuWaitTimeMs, appInstance->m_sleepTimeMs);
            ImGui::Separator();
            ImGui::Text("CFA: %s (Meta: %s)", ui.cfaOverride.has_value() ? std::to_string(ui.cfaOverride.value()).c_str() : "Auto", ui.cfaFromMetadataStr.c_str());
            ImGui::Text("Mode: %s, Zoom: %s", ui.isFullscreen ? "Fullscreen" : "Windowed", ui.isZoomedToNative ? "Native Pixels" : "Fit to Window");
        }
        ImGui::End();
    }
}

void GuiOverlay::endFrame(VkCommandBuffer commandBuffer) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}
