#ifndef BATCHER_OVERLAY_H
#define BATCHER_OVERLAY_H
#include <string>
#include <vector>
struct GLFWwindow;
class App;
namespace BatcherOverlay {
    void setup(GLFWwindow* window, App* appInstance);
    void cleanup();
    void beginFrame();
    void render(App* appInstance);
    void endFrame(VkCommandBuffer cmd);
}
#endif
