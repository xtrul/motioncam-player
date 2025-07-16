#include "App/App.h"
#include "Gui/BatcherOverlay.h"
#include <chrono>

bool App::runBatcher() {
    LogToFile("[App::runBatcher] start");
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        drawFrameSimple();
    }
    vkDeviceWaitIdle(m_device);
    LogToFile("[App::runBatcher] end");
    return true;
}

bool App::loadFileForBatch(const std::string& path) {
    try {
        m_decoderWrapper = std::make_unique<DecoderWrapper>(path);
        m_decoderWrapper_ptr = m_decoderWrapper.get();
        const auto& meta = m_decoderWrapper->getContainerMetadata();
        auto blk = meta.value("blackLevel", std::vector<double>{0.0});
        m_staticBlack = blk.empty() ? 0.0 : blk[0];
        m_staticWhite = meta.value("whiteLevel", 65535.0);
        m_cfaStringFromMetadata = meta.value("sensorArrangement", meta.value("sensorArrangment", "BGGR"));
        m_cfaTypeFromMetadata = Renderer_VK::getCfaType(m_cfaStringFromMetadata);
        return true;
    } catch (const std::exception& e) {
        LogToFile(std::string("[loadFileForBatch] error: ") + e.what());
        return false;
    }
}

void App::drawFrameSimple() {
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                           m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(); return; }
    else if (result == VK_SUBOPTIMAL_KHR) { m_framebufferResized = true; }
    else if (result != VK_SUCCESS) { throw std::runtime_error("Failed to acquire swap chain image!"); }

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_swapChainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = {0,0};
    rpInfo.renderArea.extent = m_swapChainExtent;
    VkClearValue clear{}; clear.color = {{0.1f,0.1f,0.1f,1.0f}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    BatcherOverlay::beginFrame();
    BatcherOverlay::render(this);
    BatcherOverlay::endFrame(cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitS[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitS;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    vkQueueSubmit(m_graphicsQueue, 1, &submit, m_inFlightFences[m_currentFrame]);

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapChain;
    present.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(m_presentQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false; recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image!");
    }
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
