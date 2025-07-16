#ifndef GUI_BATCHER_H
#define GUI_BATCHER_H

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;
class App;

namespace GuiBatcher {
    void setup(GLFWwindow* window, App* appInstance);
    void cleanup();
    void beginFrame();
    void render(App* appInstance);
    void endFrame(VkCommandBuffer cmd);
}

#endif // GUI_BATCHER_H
