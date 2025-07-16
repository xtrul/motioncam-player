#include "Gui/BatcherOverlay.h"
#include "App/App.h"
#include "Utils/DebugLog.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace BatcherOverlay {
static App* g_app = nullptr;

void setup(GLFWwindow* window, App* appInstance) {
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = appInstance->m_vkInstance;
    info.PhysicalDevice = appInstance->m_physicalDevice;
    info.Device = appInstance->m_device;
    info.Queue = appInstance->m_graphicsQueue;
    info.DescriptorPool = appInstance->m_imguiDescriptorPool;
    info.MinImageCount = 2;
    info.ImageCount = 2;
    ImGui_ImplVulkan_Init(&info, appInstance->m_renderPass);
    ImGui::StyleColorsDark();
    g_app = appInstance;
}

void cleanup() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void endFrame(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void render(App* app) {
    ImGui::SetNextWindowPos(ImVec2(10,10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400,300), ImGuiCond_FirstUseEver);
    ImGui::Begin("File Queue");
    if (ImGui::Button("Add")) {
        std::string p = app->openMcrawDialog();
        if(!p.empty()) app->m_fileList.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && app->m_selectedIndex>=0 && app->m_selectedIndex < (int)app->m_fileList.size()) {
        app->m_fileList.erase(app->m_fileList.begin()+app->m_selectedIndex);
        app->m_selectedIndex = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { app->m_fileList.clear(); app->m_selectedIndex=-1; }

    ImGui::BeginChild("queue", ImVec2(0,150), true);
    for(size_t i=0;i<app->m_fileList.size();++i){
        std::string name = fs::path(app->m_fileList[i]).filename().string();
        if(ImGui::Selectable(name.c_str(), app->m_selectedIndex== (int)i)) app->m_selectedIndex=i;
    }
    ImGui::EndChild();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(420,10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300,200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Export Settings");
    ImGui::RadioButton("ProRes", &app->m_renderFormat, 0); ImGui::SameLine();
    ImGui::RadioButton("DNxHR", &app->m_renderFormat, 1); ImGui::SameLine();
    ImGui::RadioButton("HEVC (GPU)", &app->m_renderFormat, 2);
    ImGui::InputText("Output Folder", &app->m_outputFolder);
    if (ImGui::Button("Convert All")) {
        for(const std::string& f : app->m_fileList){
            app->m_conversionLog.push_back("Converting " + fs::path(f).filename().string());
            if(app->loadFileForBatch(f)){
                std::string outFolder = app->m_outputFolder.empty() ? fs::path(f).parent_path().string() : app->m_outputFolder;
                fs::create_directories(outFolder);
                std::string stem = fs::path(f).stem().string();
                std::string out;
                if(app->m_renderFormat==0) out = outFolder + "/" + stem + ".mov";
                else if(app->m_renderFormat==1) out = outFolder + "/" + stem + ".mxf";
                else out = outFolder + "/" + stem + ".mp4";
                if(app->m_renderFormat==0){ app->exportCurrentClipToProRes(); if(app->m_proResThread.joinable()) app->m_proResThread.join(); }
                else if(app->m_renderFormat==1){ app->exportCurrentClipToDNxHR(); if(app->m_dnxhrThread.joinable()) app->m_dnxhrThread.join(); }
                else { app->exportCurrentClipToHEVC_AMD(); if(app->m_hevcThread.joinable()) app->m_hevcThread.join(); }
                app->m_conversionLog.push_back("Done: " + fs::path(f).filename().string());
            } else {
                app->m_conversionLog.push_back("Error loading " + f);
            }
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(10,320), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(710,200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Conversion Log");
    for(const auto& line : app->m_conversionLog) ImGui::TextUnformatted(line.c_str());
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(10,530), ImGuiCond_FirstUseEver);
    ImGui::Begin("Footer", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("MotionCam Batcher v0.36.0 — RAW VIDEO CONVERTER");
    ImGui::Text("www.motioncamapp.com");
    ImGui::End();
}

} // namespace BatcherOverlay
