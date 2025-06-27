#ifndef GUI_UTILS_H
#define GUI_UTILS_H

#include <string>
#include <cstdint> // For int64_t

namespace GuiUtils {

	std::string formatHMS(int64_t ns);
	std::string format_mm_ss(double total_seconds);

	// Any other small utility functions for GUI can go here.

} // namespace GuiUtils

#endif // GUI_UTILS_H