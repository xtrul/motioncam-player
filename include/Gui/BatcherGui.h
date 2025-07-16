#ifndef BATCHER_GUI_H
#define BATCHER_GUI_H

#include <vulkan/vulkan.h>
struct GLFWwindow;
class App;

namespace BatcherGui {
    void setup(GLFWwindow* window, App* appInstance);
    void cleanup();
    void beginFrame();
    void render(App* appInstance);
    void endFrame(VkCommandBuffer commandBuffer);
}

#endif // BATCHER_GUI_H
