#include <gtest/gtest.h>
#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"
#include "Utils/DebugLog.h"

TEST(GpuYuvConverter, DISABLED_Simple2x2)
{
    GTEST_SKIP() << "Vulkan context not available in test environment";
}
