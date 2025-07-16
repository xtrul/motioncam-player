#include "Gui/BatcherGui.h"
#include "Gui/GuiOverlay.h"
#include "Gui/GuiStyles.h"
#include "App/App.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace BatcherGui {

static int selectedIndex = -1;

void setup(GLFWwindow* window, App* appInstance) {
    GuiOverlay::setup(window, appInstance);
}

void cleanup() { GuiOverlay::cleanup(); }
void beginFrame() { GuiOverlay::beginFrame(); }
void endFrame(VkCommandBuffer cmd) { GuiOverlay::endFrame(cmd); }

void render(App* app) {
    ImGui::Begin("Batch Export", nullptr, ImGuiWindowFlags_NoCollapse);

    // File queue panel
    ImGui::Text("Files:");
    ImGui::BeginChild("file_list", ImVec2(0,150), true);
    for(size_t i=0;i<app->m_batchFileQueue.size();++i){
        std::string name = fs::path(app->m_batchFileQueue[i]).filename().string();
        if(ImGui::Selectable(name.c_str(), selectedIndex==static_cast<int>(i)))
            selectedIndex = static_cast<int>(i);
    }
    ImGui::EndChild();
    if(ImGui::Button("Add")){
        std::string p = app->openMcrawDialog();
        if(!p.empty()) app->m_batchFileQueue.push_back(p);
    }
    ImGui::SameLine();
    if(ImGui::Button("Remove") && selectedIndex>=0 && selectedIndex < (int)app->m_batchFileQueue.size()){
        app->m_batchFileQueue.erase(app->m_batchFileQueue.begin()+selectedIndex);
        if(selectedIndex >= (int)app->m_batchFileQueue.size()) selectedIndex = (int)app->m_batchFileQueue.size()-1;
    }
    ImGui::SameLine();
    if(ImGui::Button("Clear")){
        app->m_batchFileQueue.clear();
        selectedIndex = -1;
    }

    ImGui::Separator();

    // Render format
    ImGui::Text("Render Format:");
    ImGui::RadioButton("ProRes", &app->m_selectedFormat, 0); ImGui::SameLine();
    ImGui::RadioButton("DNxHR", &app->m_selectedFormat, 1); ImGui::SameLine();
    ImGui::RadioButton("HEVC (GPU)", &app->m_selectedFormat, 2);

    // Output folder
    char buf[512];
    strncpy(buf, app->m_outputFolder.c_str(), sizeof(buf));
    if(ImGui::InputText("Output Folder", buf, sizeof(buf))) {
        app->m_outputFolder = buf;
    }

    if(ImGui::Button("Convert All") && !app->m_batchActive){
        app->startBatchConversion();
    }

    ImGui::End();

    ImGui::Begin("Conversion Log", nullptr, ImGuiWindowFlags_NoCollapse);
    for(const auto& line : app->m_conversionLog){
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::End();

    ImGui::SetCursorPosY(ImGui::GetIO().DisplaySize.y - 40);
    ImGui::TextUnformatted("MotionCam Batcher v0.36.0 — RAW VIDEO CONVERTER");
    ImGui::TextUnformatted("www.motioncamapp.com");
}

} // namespace BatcherGui
