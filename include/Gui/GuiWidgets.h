#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include <imgui.h>

namespace GuiWidgets {
    bool Splitter(bool vertical, float thickness, float* size1, float* size2, float minSize1, float minSize2);
}

#endif // GUI_WIDGETS_H
