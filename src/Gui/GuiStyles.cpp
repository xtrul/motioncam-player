#include "Gui/GuiStyles.h"
#include "Utils/IconsMaterial.h" // For ICON_MIN_MD, ICON_MAX_16_MD
#include "Utils/DebugLog.h"      // For LogToFile

#include <string>
#include <filesystem> // For path manipulation if needed for font paths

#ifdef _WIN32
#include <windows.h> // For GetModuleFileNameA, _splitpath_s
#endif

namespace fs = std::filesystem;

namespace GuiStyles {

    // Define extern font pointers
    ImFont* G_TextFont = nullptr;
    ImFont* G_LargeIconFont = nullptr;
    ImFont* G_SmallIconFont = nullptr;
    ImFont* G_AuxOverlayIconFont = nullptr;

    // Define extern font sizes
    float G_BaseTextFontSize = 18.0f;
    float G_LargeIconFontSize = G_BaseTextFontSize * 1.8f;
    float G_SmallIconFontSize = G_BaseTextFontSize * 0.90f;
    float G_AuxOverlayIconFontSize = G_BaseTextFontSize * 0.80f;


    void LoadFonts(ImGuiIO& io) {
        std::string roboto_regular_font_path_str;
        std::string icon_font_load_path_str;

#ifdef _WIN32
        char modulePath[MAX_PATH];
        GetModuleFileNameA(NULL, modulePath, MAX_PATH);
        char drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];
        _splitpath_s(modulePath, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
        std::string exeDir = std::string(drive) + std::string(dir);
        if (!exeDir.empty() && (exeDir.back() == '\\' || exeDir.back() == '/')) {
            exeDir.pop_back();
        }
        try {
            fs::path full_roboto_path = fs::path(exeDir) / "assets" / "Roboto-Regular.ttf";
            roboto_regular_font_path_str = full_roboto_path.string();
            LogToFile(std::string("[GuiStyles::LoadFonts] Roboto font path (Win32): ") + roboto_regular_font_path_str);
        }
        catch (...) {
            roboto_regular_font_path_str = "assets/Roboto-Regular.ttf"; // Fallback
            LogToFile(std::string("[GuiStyles::LoadFonts] Roboto font path (Win32 fallback): ") + roboto_regular_font_path_str);
        }
        try {
            fs::path full_icon_path = fs::path(exeDir) / "assets" / "MaterialIcons-Regular.ttf";
            icon_font_load_path_str = full_icon_path.string();
            LogToFile(std::string("[GuiStyles::LoadFonts] Icon font path (Win32): ") + icon_font_load_path_str);
        }
        catch (...) {
            icon_font_load_path_str = "assets/MaterialIcons-Regular.ttf"; // Fallback
            LogToFile(std::string("[GuiStyles::LoadFonts] Icon font path (Win32 fallback): ") + icon_font_load_path_str);
        }
#else
        // For non-Windows, assume assets are relative to executable or CWD
        // This might need adjustment based on deployment (e.g., using resource paths from CMake)
        roboto_regular_font_path_str = "assets/Roboto-Regular.ttf";
        icon_font_load_path_str = "assets/MaterialIcons-Regular.ttf";
        LogToFile(std::string("[GuiStyles::LoadFonts] Roboto font path (Unix-like): ") + roboto_regular_font_path_str);
        LogToFile(std::string("[GuiStyles::LoadFonts] Icon font path (Unix-like): ") + icon_font_load_path_str);
#endif

        if (!fs::exists(roboto_regular_font_path_str)) LogToFile(std::string("WARNING: Roboto font file not found at: ") + roboto_regular_font_path_str);
        if (!fs::exists(icon_font_load_path_str)) LogToFile(std::string("WARNING: Icon font file not found at: ") + icon_font_load_path_str);


        ImFontConfig text_font_config;
        text_font_config.SizePixels = G_BaseTextFontSize;
        G_TextFont = io.Fonts->AddFontFromFileTTF(roboto_regular_font_path_str.c_str(), G_BaseTextFontSize, &text_font_config);
        IM_ASSERT(G_TextFont != nullptr && "Failed to load text font!");

        static const ImWchar icons_ranges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };
        float dpi_scale = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;

        ImFontConfig large_icons_config;
        large_icons_config.PixelSnapH = true;
        large_icons_config.GlyphOffset.y = -1.0f * dpi_scale; // Adjust based on font and desired alignment
        G_LargeIconFont = io.Fonts->AddFontFromFileTTF(icon_font_load_path_str.c_str(), G_LargeIconFontSize, &large_icons_config, icons_ranges);
        IM_ASSERT(G_LargeIconFont != nullptr && "Failed to load large icon font!");

        ImFontConfig small_icons_config;
        small_icons_config.PixelSnapH = true;
        small_icons_config.GlyphOffset.y = 0.0f * dpi_scale;
        G_SmallIconFont = io.Fonts->AddFontFromFileTTF(icon_font_load_path_str.c_str(), G_SmallIconFontSize, &small_icons_config, icons_ranges);
        IM_ASSERT(G_SmallIconFont != nullptr && "Failed to load small icon font!");

        ImFontConfig aux_overlay_icons_config;
        aux_overlay_icons_config.PixelSnapH = true;
        aux_overlay_icons_config.GlyphOffset.y = 0.0f * dpi_scale;
        G_AuxOverlayIconFont = io.Fonts->AddFontFromFileTTF(icon_font_load_path_str.c_str(), G_AuxOverlayIconFontSize, &aux_overlay_icons_config, icons_ranges);
        IM_ASSERT(G_AuxOverlayIconFont != nullptr && "Failed to load auxiliary overlay icon font!");
    }


    void ApplyCustomStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.ChildRounding = 8.0f;
        style.PopupRounding = 8.0f;
        style.FrameRounding = 16.0f; // For pill-shaped buttons if height allows
        style.GrabRounding = 16.0f;
        style.ScrollbarRounding = 8.0f;

        style.WindowBorderSize = 0.0f; // Main control panel has custom border
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;

        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(8.0f, 6.0f); // Default, can be overridden
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);

        style.ScrollbarSize = 16.0f;
        style.GrabMinSize = 12.0f;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f); // Darker main bg
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.95f);
        colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.00f); // Darker frame
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.30f, 0.53f, 1.00f); // Accent color for active title
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.35f, 0.38f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // Accent
        colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f); // Accent
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.25f, 0.55f, 0.95f, 1.00f); // Accent
        colors[ImGuiCol_Button] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Transparent base for icon buttons
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.05f); // Subtle hover
        colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f); // Subtle active
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.85f, 0.45f); // Accent for selectable headers
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.50f, 0.90f, 0.80f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);
        colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
    }

} // namespace GuiStyles