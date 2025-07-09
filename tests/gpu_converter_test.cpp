#include <gtest/gtest.h>
#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"

TEST(GpuYuvConverter, AverageChromaNonZero) {
    GTEST_SKIP() << "Vulkan environment not available in test harness";
}
