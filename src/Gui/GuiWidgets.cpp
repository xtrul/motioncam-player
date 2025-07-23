#include "Gui/GuiWidgets.h"

namespace GuiWidgets {
    bool Splitter(bool vertical, float thickness, float* size1, float* size2, float minSize1, float minSize2)
    {
        ImVec2 backup_pos = ImGui::GetCursorPos();
        ImGui::InvisibleButton("##Splitter", vertical ? ImVec2(*size1 + *size2, thickness) : ImVec2(thickness, *size1 + *size2));
        bool held = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        if (hovered || held)
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (held)
        {
            float delta = vertical ? ImGui::GetIO().MouseDelta.y : ImGui::GetIO().MouseDelta.x;
            *size1 += delta;
            *size2 -= delta;
            if (*size1 < minSize1) *size1 = minSize1;
            if (*size2 < minSize2) *size2 = minSize2;
        }
        ImGui::SetCursorPos(backup_pos);
        return held;
    }
}
