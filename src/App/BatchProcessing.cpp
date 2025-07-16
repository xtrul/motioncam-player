#include "App/App.h"
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

void App::startBatchConversion() {
#ifdef MOTIONCAM_BATCHER
    if (m_batchActive) return;
    m_batchActive = true;
    if (m_batchThread.joinable()) m_batchThread.join();
    m_batchThread = std::thread([this]() {
        for (const auto& file : m_batchFileQueue) {
            fs::path inPath(file);
            m_conversionLog.push_back("Converting " + inPath.filename().string() + "...");
            m_fileList.clear();
            m_fileList.push_back(file);
            loadFileAtIndex(0);
            fs::path outDir = m_outputFolder.empty() ? inPath.parent_path() : fs::path(m_outputFolder);
            fs::create_directories(outDir);
            std::string outPath;
            if (m_selectedFormat == 0) {
                outPath = (outDir / (inPath.stem().string() + ".mov")).string();
                convertCurrentClipToProRes(outPath);
                while (m_proResStatus.active.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (m_proResStatus.errorMsg.empty())
                    m_conversionLog.push_back("Export complete: ProRes");
                else
                    m_conversionLog.push_back(std::string("Error: ") + m_proResStatus.errorMsg);
            } else if (m_selectedFormat == 1) {
                outPath = (outDir / (inPath.stem().string() + ".mov")).string();
                convertCurrentClipToDNxHR(outPath);
                while (m_dnxhrStatus.active.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (m_dnxhrStatus.errorMsg.empty())
                    m_conversionLog.push_back("Export complete: DNxHR");
                else
                    m_conversionLog.push_back(std::string("Error: ") + m_dnxhrStatus.errorMsg);
            } else {
                outPath = (outDir / (inPath.stem().string() + ".mp4")).string();
                convertCurrentClipToHEVC_AMD(outPath);
                while (m_hevcStatus.active.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (m_hevcStatus.errorMsg.empty())
                    m_conversionLog.push_back("Export complete: HEVC");
                else
                    m_conversionLog.push_back(std::string("Error: ") + m_hevcStatus.errorMsg);
            }
        }
        m_batchActive = false;
    });
#endif
}
