#include <gtest/gtest.h>
#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"
#include <vector>

TEST(GpuConverter, BasicConversion) {
    Renderer_VK renderer;
    ASSERT_TRUE(renderer.initVulkan());
    GpuYuvConverter conv(&renderer);
    ASSERT_TRUE(conv.init(2,2));
    std::vector<uint16_t> pattern = {
        0,65535,
        65535,0
    };
    std::vector<uint16_t> out;
    ASSERT_TRUE(conv.convertAndReadback(pattern.data(),2,2,out,0));
    uint64_t sumU=0,sumV=0;
    for(size_t i=0;i<out.size();i+=4){
        sumU+=out[i+2];
        sumV+=out[i+3];
    }
    EXPECT_NE(sumU,0);
    EXPECT_NE(sumV,0);
    conv.cleanup();
    renderer.cleanupVulkan();
}
