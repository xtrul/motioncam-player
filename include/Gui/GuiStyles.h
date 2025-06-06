#ifndef GUI_STYLES_H
#define GUI_STYLES_H

#include <imgui.h> // For ImVec2, ImVec4, ImFont etc.

namespace GuiStyles {

	// Font related (can be extern and defined in GuiSetup.cpp)
	extern ImFont* G_TextFont;
	extern ImFont* G_LargeIconFont;
	extern ImFont* G_SmallIconFont;
	extern ImFont* G_AuxOverlayIconFont;

	extern float G_BaseTextFontSize;
	extern float G_LargeIconFontSize;
	extern float G_SmallIconFontSize;
	extern float G_AuxOverlayIconFontSize;

	// Style constants (can be const or static const)
	const float  PILL_RADIUS = 18.0f;
	const float  PANEL_HORIZONTAL_PADDING = 24.0f;
	const float  PANEL_VERTICAL_PADDING = 14.0f;
	const ImVec2 G_LARGE_BUTTON_PADDING = ImVec2(5.0f, 5.0f);
	const ImVec2 G_SMALL_BUTTON_PADDING = ImVec2(0.5f, 0.5f);
	const ImVec2 G_AUX_OVERLAY_BUTTON_PADDING = ImVec2(2.0f, 2.0f);

	// Function to apply the custom style (called from GuiSetup.cpp)
	void ApplyCustomStyle();
	void LoadFonts(ImGuiIO& io); // Helper to load fonts

} // namespace GuiStyles

#endif // GUI_STYLES_H