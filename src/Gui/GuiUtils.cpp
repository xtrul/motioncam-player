#include "Gui/GuiUtils.h"
#include <sstream>
#include <iomanip> // For std::setfill, std::setw, std::fixed, std::setprecision

namespace GuiUtils {

    std::string formatHMS(int64_t ns) {
        if (ns < 0) ns = 0;
        double s_total = static_cast<double>(ns) * 1e-9;
        bool is_negative = s_total < 0;
        if (is_negative) s_total = -s_total;

        int h = static_cast<int>(s_total / 3600.0);
        s_total -= static_cast<double>(h) * 3600.0;
        int m = static_cast<int>(s_total / 60.0);
        s_total -= static_cast<double>(m) * 60.0;
        double sec_frac = s_total;

        std::ostringstream oss;
        if (is_negative) oss << "-";
        oss << std::setfill('0') << std::setw(2) << h << ':'
            << std::setw(2) << m << ':'
            << std::fixed << std::setprecision(3) << std::setw(6) << sec_frac;
        return oss.str();
    }

    std::string format_mm_ss(double total_seconds) {
        if (total_seconds < 0) total_seconds = 0;
        int minutes = static_cast<int>(total_seconds) / 60;
        int seconds = static_cast<int>(total_seconds) % 60;
        char buf[16];
        // Using snprintf for safety, though with fixed format, sprintf would also work.
        snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
        return std::string(buf);
    }

} // namespace GuiUtils