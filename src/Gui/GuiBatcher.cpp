#include "Gui/GuiBatcher.h"
#include "App/App.h"
#include "Gui/GuiStyles.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace GuiBatcher {

static int selectedIndex = -1;
static int formatIndex = 0; // 0=ProRes,1=DNxHR,2=HEVC
static std::string outputFolder;
static bool processing = false;
static std::vector<std::string> logLines;
static std::thread worker;

static void batchThread(App* app) {
    processing = true;
    logLines.clear();
    auto files = app->m_fileList; // copy
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string& f = files[i];
        logLines.push_back("Converting " + fs::path(f).filename().string() + "...");
        std::string folder = outputFolder.empty() ? fs::path(f).parent_path().string() : outputFolder;
        std::string stem = fs::path(f).stem().string();
        std::string outPath;
        switch(formatIndex){
            case 0: outPath = (fs::path(folder) / (stem + ".mov")).string();
                    app->m_overrideProResPath = outPath;
                    app->m_fileList = {f};
                    app->loadFileAtIndex(0);
                    app->convertCurrentClipToProRes();
                    while(app->m_proResStatus.active.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if(app->m_proResThread.joinable()) app->m_proResThread.join();
                    if(app->m_proResStatus.errorMsg.empty()) logLines.push_back("Export complete: ProRes");
                    else logLines.push_back("Error: " + app->m_proResStatus.errorMsg);
                    break;
            case 1: outPath = (fs::path(folder) / (stem + ".mov")).string();
                    app->m_overrideDNxHRPath = outPath;
                    app->m_fileList = {f};
                    app->loadFileAtIndex(0);
                    app->convertCurrentClipToDNxHR();
                    while(app->m_dnxhrStatus.active.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if(app->m_dnxhrThread.joinable()) app->m_dnxhrThread.join();
                    if(app->m_dnxhrStatus.errorMsg.empty()) logLines.push_back("Export complete: DNxHR");
                    else logLines.push_back("Error: " + app->m_dnxhrStatus.errorMsg);
                    break;
            case 2: outPath = (fs::path(folder) / (stem + ".mp4")).string();
                    app->m_overrideHevcPath = outPath;
                    app->m_fileList = {f};
                    app->loadFileAtIndex(0);
                    app->convertCurrentClipToHEVC_AMD();
                    while(app->m_hevcStatus.active.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if(app->m_hevcThread.joinable()) app->m_hevcThread.join();
                    if(app->m_hevcStatus.errorMsg.empty()) logLines.push_back("Export complete: HEVC");
                    else logLines.push_back("Error: " + app->m_hevcStatus.errorMsg);
                    break;
        }
    }
    processing = false;
}

void setup(GLFWwindow* window, App* app){
    GuiOverlay::setup(window, app); // reuse fonts/style
}

void cleanup(){
    if(worker.joinable()) worker.join();
    GuiOverlay::cleanup();
}

void beginFrame(){
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void render(App* app){
    ImGui::Begin("File Queue");
    if(ImGui::BeginChild("filelist", ImVec2(0,150), true)){
        for(size_t i=0;i<app->m_fileList.size();++i){
            const std::string name = fs::path(app->m_fileList[i]).filename().string();
            if(ImGui::Selectable(name.c_str(), selectedIndex==(int)i)) selectedIndex=i;
        }
        ImGui::EndChild();
    }
    if(ImGui::Button("Add")) app->triggerOpenFileViaDialog();
    ImGui::SameLine();
    if(ImGui::Button("Remove") && selectedIndex>=0 && selectedIndex < (int)app->m_fileList.size()){
        app->m_fileList.erase(app->m_fileList.begin()+selectedIndex);
        if(selectedIndex >= (int)app->m_fileList.size()) selectedIndex = (int)app->m_fileList.size()-1;
    }
    ImGui::SameLine();
    if(ImGui::Button("Clear")){ app->m_fileList.clear(); selectedIndex=-1; }
    ImGui::Separator();
    ImGui::RadioButton("ProRes", &formatIndex,0); ImGui::SameLine();
    ImGui::RadioButton("DNxHR", &formatIndex,1); ImGui::SameLine();
    ImGui::RadioButton("HEVC (GPU)", &formatIndex,2);
    ImGui::InputText("Output Folder", outputFolder.data(), 256);
    if(ImGui::Button("Convert All") && !processing && !app->m_fileList.empty()){
        if(worker.joinable()) worker.join();
        worker = std::thread(batchThread, app);
    }
    ImGui::End();

    ImGui::Begin("Conversion Log");
    if(ImGui::BeginChild("log", ImVec2(0,150), true)){
        for(const auto& l : logLines) ImGui::TextUnformatted(l.c_str());
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::SetCursorPosY(ImGui::GetIO().DisplaySize.y - 30);
    ImGui::Text("MotionCam Batcher v0.36.0 — RAW VIDEO CONVERTER");
    ImGui::SetCursorPosY(ImGui::GetIO().DisplaySize.y - 15);
    ImGui::Text("www.motioncamapp.com");
}

void endFrame(VkCommandBuffer cmd){
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
}

} // namespace GuiBatcher

