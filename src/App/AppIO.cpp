// FILE: src/App/AppIO.cpp
#include "App/App.h"
#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"
#include "Playback/PlaybackController.h"
#include "Graphics/Renderer_VK.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"
#include <motioncam/Decoder.hpp>
#include <motioncam/RawData.hpp>

#include "App/AppConfig.h" 

#ifndef TINY_DNG_WRITER_IMPLEMENTATION
#define TINY_DNG_WRITER_IMPLEMENTATION
#endif
#include <tinydng/tiny_dng_writer.h>


#include <filesystem>
#include <iostream>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream> 

namespace fs = std::filesystem;

namespace {
    bool writeDngInternal(
        const std::string& outputPath,
        const RawBytes& data,
        const nlohmann::json& frameMetadata,
        const nlohmann::json& containerMetadata,
        std::string& errorMsg)
    {
        const unsigned int width = frameMetadata.value("width", 0);
        const unsigned int height = frameMetadata.value("height", 0);

        if (width == 0 || height == 0) {
            errorMsg = "Invalid frame dimensions (width or height is zero).";
            return false;
        }

        if (data.size() < static_cast<size_t>(width) * height * sizeof(uint16_t)) {
            errorMsg = "Insufficient image data for given dimensions. Expected bytes: " +
                std::to_string(static_cast<size_t>(width) * height * sizeof(uint16_t)) +
                ", Got: " + std::to_string(data.size());
            return false;
        }
        std::vector<double> asShotNeutralDouble = frameMetadata.value("asShotNeutral", std::vector<double>{1.0, 1.0, 1.0});
        std::vector<float> asShotNeutral;
        asShotNeutral.reserve(asShotNeutralDouble.size());
        for (double val : asShotNeutralDouble) {
            asShotNeutral.push_back(static_cast<float>(val));
        }

        std::vector<double> blackLevelDouble = containerMetadata.value("blackLevel", std::vector<double>{0.0, 0.0, 0.0, 0.0});
        if (blackLevelDouble.empty()) {
            blackLevelDouble = { 0.0, 0.0, 0.0, 0.0 };
        }
        else if (blackLevelDouble.size() == 1) {
            double val = blackLevelDouble[0];
            blackLevelDouble = { val, val, val, val };
        }
        else if (blackLevelDouble.size() != 4) {
#ifndef NDEBUG
            std::cerr << "Warning: Unexpected number of black levels (" << blackLevelDouble.size() << ") in metadata. Adjusting to 4." << std::endl;
#endif
            double fill_val = blackLevelDouble.empty() ? 0.0 : blackLevelDouble[0];
            blackLevelDouble.resize(4, fill_val);
        }
        std::vector<uint16_t> blackLevelUint16;
        blackLevelUint16.reserve(blackLevelDouble.size());
        for (double d_val : blackLevelDouble) {
            uint16_t u_val = 0;
            if (d_val < 0.0) u_val = 0;
            else if (d_val > 65535.0) u_val = 65535;
            else u_val = static_cast<uint16_t>(std::round(d_val));
            blackLevelUint16.push_back(u_val);
        }
        double whiteLevel = containerMetadata.value("whiteLevel", 65535.0);
        std::string sensorArrangement = containerMetadata.value("sensorArrangement",
            containerMetadata.value("sensorArrangment", "BGGR"));
        nlohmann::json ccm1_json = containerMetadata.value("ColorMatrix", containerMetadata.value("colorMatrix1", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        nlohmann::json ccm2_json = containerMetadata.value("ColorMatrix2", containerMetadata.value("colorMatrix2", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        std::vector<float> colorMatrix1 = { 1,0,0, 0,1,0, 0,0,1 };
        std::vector<float> colorMatrix2 = { 1,0,0, 0,1,0, 0,0,1 };
        if (ccm1_json.is_array() && ccm1_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) colorMatrix1[i] = ccm1_json[i].get<float>();
        }
        if (ccm2_json.is_array() && ccm2_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) colorMatrix2[i] = ccm2_json[i].get<float>();
        }
        nlohmann::json fwd1_json = containerMetadata.value("ForwardMatrix1", containerMetadata.value("forwardMatrix1", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        nlohmann::json fwd2_json = containerMetadata.value("ForwardMatrix2", containerMetadata.value("forwardMatrix2", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        std::vector<float> forwardMatrix1 = { 1,0,0, 0,1,0, 0,0,1 };
        std::vector<float> forwardMatrix2 = { 1,0,0, 0,1,0, 0,0,1 };
        if (fwd1_json.is_array() && fwd1_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) forwardMatrix1[i] = fwd1_json[i].get<float>();
        }
        if (fwd2_json.is_array() && fwd2_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) forwardMatrix2[i] = fwd2_json[i].get<float>();
        }
        tinydngwriter::DNGImage dng;
        dng.SetBigEndian(false);
        dng.SetDNGVersion(1, 4, 0, 0);
        dng.SetDNGBackwardVersion(1, 1, 0, 0);
        dng.SetImageData(data.data(), data.size());
        dng.SetImageWidth(width);
        dng.SetImageLength(height);
        dng.SetPlanarConfig(tinydngwriter::PLANARCONFIG_CONTIG);
        dng.SetPhotometric(tinydngwriter::PHOTOMETRIC_CFA);
        dng.SetRowsPerStrip(height);
        dng.SetSamplesPerPixel(1);
        dng.SetCFARepeatPatternDim(2, 2);
        dng.SetBlackLevelRepeatDim(2, 2);
        dng.SetBlackLevel(blackLevelUint16.size(), blackLevelUint16.data());
        dng.SetWhiteLevel(static_cast<float>(whiteLevel));
        dng.SetCompression(tinydngwriter::COMPRESSION_NONE);
        std::vector<unsigned char> cfa_pattern_values;
        std::string cfa_upper = sensorArrangement;
        std::transform(cfa_upper.begin(), cfa_upper.end(), cfa_upper.begin(), ::toupper);
        if (cfa_upper == "RGGB")        cfa_pattern_values = { 0, 1, 1, 2 };
        else if (cfa_upper == "BGGR")   cfa_pattern_values = { 2, 1, 1, 0 };
        else if (cfa_upper == "GRBG")   cfa_pattern_values = { 1, 0, 2, 1 };
        else if (cfa_upper == "GBRG")   cfa_pattern_values = { 1, 2, 0, 1 };
        else {
            errorMsg = "Invalid or unsupported sensorArrangement for DNG CFA pattern: " + sensorArrangement;
            return false;
        }
        dng.SetCFAPattern(cfa_pattern_values.size(), cfa_pattern_values.data());
        dng.SetCFALayout(1);
        const uint16_t bps[1] = { 16 };
        dng.SetBitsPerSample(1, bps);
        dng.SetColorMatrix1(3, colorMatrix1.data());
        dng.SetColorMatrix2(3, colorMatrix2.data());
        dng.SetForwardMatrix1(3, forwardMatrix1.data());
        dng.SetForwardMatrix2(3, forwardMatrix2.data());
        dng.SetAsShotNeutral(asShotNeutral.size(), asShotNeutral.data());
        dng.SetCalibrationIlluminant1(21);
        dng.SetCalibrationIlluminant2(17);
        dng.SetUniqueCameraModel("MotionCam App Player Export");
        dng.SetSubfileType(false, false, false);
        const uint32_t activeArea[4] = { 0, 0, height, width };
        dng.SetActiveArea(activeArea);

        tinydngwriter::DNGWriter writer(false);
        writer.AddImage(&dng);

        if (!writer.WriteToFile(outputPath.c_str(), &errorMsg)) {
            return false;
        }
        return true;
    }
}


void App::ioWorkerLoop() {
    LogToFile("[App::ioWorkerLoop] I/O thread started.");
    std::unique_ptr<motioncam::Decoder> threadLocalDecoder;
    std::string currentFileBeingProcessed_io;
    std::vector<motioncam::Timestamp> frameTimestampsForCurrentFile_io;
    size_t frameIndexInCurrentFile_io = 0;
    size_t currentFileLoadID_io = 0;

    while (!m_threadsShouldStop.load(std::memory_order_relaxed)) {
        bool fileStateChanged_io = false;
        std::string nextFileToProcessIfChanged_io;

        {
            std::unique_lock<std::mutex> lock(m_ioThreadFileMutex);
            m_ioThreadFileCv.wait(lock, [&] {
                bool is_pb_paused = m_playbackController_ptr ? m_playbackController_ptr->isPaused() : true;
                bool can_push_to_decode_q = m_decodeQueue.size() < m_decodeQueue.get_max_size_debug();

                if (m_threadsShouldStop.load(std::memory_order_relaxed)) return true;
                if (m_ioThreadFileChanged.load(std::memory_order_relaxed)) return true;

                if (!threadLocalDecoder || frameTimestampsForCurrentFile_io.empty()) return false;

                if (!is_pb_paused) {
                    return can_push_to_decode_q && (frameIndexInCurrentFile_io < frameTimestampsForCurrentFile_io.size());
                }
                else {
                    if (m_playbackController_ptr) {
                        return can_push_to_decode_q && (frameIndexInCurrentFile_io != m_playbackController_ptr->getCurrentFrameIndex() && frameIndexInCurrentFile_io < frameTimestampsForCurrentFile_io.size());
                    }
                    return false;
                }
                });

            if (m_threadsShouldStop.load(std::memory_order_relaxed)) { LogToFile("[App::ioWorkerLoop] Stop signal received, exiting."); break; }

            if (m_ioThreadFileChanged.load(std::memory_order_acquire)) {
                nextFileToProcessIfChanged_io = m_ioThreadCurrentFilePath;
                fileStateChanged_io = true;
                size_t newAppLoadID = m_activeFileLoadID.load(std::memory_order_relaxed);

                if (currentFileBeingProcessed_io != nextFileToProcessIfChanged_io || currentFileLoadID_io != newAppLoadID) {
                    LogToFile(std::string("[App::ioWorkerLoop] File/LoadID changed. PrevFile: '") + (currentFileBeingProcessed_io.empty() ? "<N/A>" : fs::path(currentFileBeingProcessed_io).filename().string()) +
                        "', PrevLoadID: " + std::to_string(currentFileLoadID_io) +
                        ". NewFile: '" + (nextFileToProcessIfChanged_io.empty() ? "<EMPTY>" : fs::path(nextFileToProcessIfChanged_io).filename().string()) +
                        "', NewLoadID: " + std::to_string(newAppLoadID));
                    currentFileBeingProcessed_io = nextFileToProcessIfChanged_io;
                    currentFileLoadID_io = newAppLoadID;
                    threadLocalDecoder.reset();
                    frameTimestampsForCurrentFile_io.clear();
                }
                else {
                    LogToFile(std::string("[App::ioWorkerLoop] SEEK/STATE_CHANGE directive within current file: '") + (currentFileBeingProcessed_io.empty() ? "<EMPTY>" : fs::path(currentFileBeingProcessed_io).filename().string()) + "', Current LoadID: " + std::to_string(currentFileLoadID_io));
                }
                m_ioThreadFileChanged.store(false, std::memory_order_release);
            }
        }

        if (fileStateChanged_io) {
            if (currentFileBeingProcessed_io.empty()) {
                threadLocalDecoder.reset(); frameTimestampsForCurrentFile_io.clear(); frameIndexInCurrentFile_io = 0;
                continue;
            }
            if (!threadLocalDecoder) {
                try {
                    threadLocalDecoder = std::make_unique<motioncam::Decoder>(currentFileBeingProcessed_io);
                    frameTimestampsForCurrentFile_io = threadLocalDecoder->getFrames();
                    std::ostringstream log_oss_dec;
                    log_oss_dec << "[App::ioWorkerLoop] Decoder setup complete for '" << fs::path(currentFileBeingProcessed_io).filename().string()
                        << "'. Frames: " << frameTimestampsForCurrentFile_io.size();
                    if (!frameTimestampsForCurrentFile_io.empty()) {
                        log_oss_dec << ", FirstTS: " << frameTimestampsForCurrentFile_io.front();
                    }
                    LogToFile(log_oss_dec.str());
                }
                catch (const std::exception& e) {
                    LogToFile(std::string("[App::ioWorkerLoop] EXCEPTION during decoder setup for '") + fs::path(currentFileBeingProcessed_io).filename().string() + "': " + e.what());
                    threadLocalDecoder.reset(); currentFileBeingProcessed_io.clear(); frameTimestampsForCurrentFile_io.clear(); frameIndexInCurrentFile_io = 0;
                    continue;
                }
            }

            if (m_playbackController_ptr && m_activeFileLoadID.load(std::memory_order_acquire) == currentFileLoadID_io) {
                frameIndexInCurrentFile_io = m_playbackController_ptr->getCurrentFrameIndex();
                LogToFile(std::string("[App::ioWorkerLoop] IO loop index synced to PlaybackController's index: ") + std::to_string(frameIndexInCurrentFile_io) + " for LoadID: " + std::to_string(currentFileLoadID_io));
            }
            else if (m_playbackController_ptr) {
                LogToFile(std::string("[App::ioWorkerLoop] Post-Change Signal: LoadID mismatch or no PB. IO LoadID: ") + std::to_string(currentFileLoadID_io) + ", App LoadID: " + std::to_string(m_activeFileLoadID.load(std::memory_order_acquire)) + ". Will not sync index from PB yet.");
            }
        }

        if (!threadLocalDecoder || frameTimestampsForCurrentFile_io.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        bool shouldLoadThisFrame_io = false;
        if (m_playbackController_ptr) {
            size_t pb_current_idx = m_playbackController_ptr->getCurrentFrameIndex();
            bool pb_is_paused = m_playbackController_ptr->isPaused();

            if (m_activeFileLoadID.load(std::memory_order_acquire) != currentFileLoadID_io) {
                shouldLoadThisFrame_io = false;
            }
            else if (pb_is_paused) {
                if (frameIndexInCurrentFile_io == pb_current_idx && frameIndexInCurrentFile_io < frameTimestampsForCurrentFile_io.size()) {
                    shouldLoadThisFrame_io = true;
                }
            }
            else {
                if (frameIndexInCurrentFile_io < frameTimestampsForCurrentFile_io.size()) {
                    if (frameIndexInCurrentFile_io >= pb_current_idx && frameIndexInCurrentFile_io < pb_current_idx + MAX_LEAD_FRAMES_IO_WORKER) {
                        shouldLoadThisFrame_io = true;
                    }
                    else if (frameIndexInCurrentFile_io < pb_current_idx) {
                        frameIndexInCurrentFile_io = pb_current_idx;
                        if (frameIndexInCurrentFile_io < frameTimestampsForCurrentFile_io.size()) shouldLoadThisFrame_io = true;
                    }
                }
            }
        }
        else {
            shouldLoadThisFrame_io = frameIndexInCurrentFile_io < frameTimestampsForCurrentFile_io.size();
        }

        if (!shouldLoadThisFrame_io) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        motioncam::Timestamp ts = frameTimestampsForCurrentFile_io[frameIndexInCurrentFile_io];
        CompressedFramePacket packet;
        packet.timestamp = ts;
        packet.frameIndex = frameIndexInCurrentFile_io;
        packet.fileLoadID = currentFileLoadID_io;

        bool payloadSuccess = false;
        try {
            payloadSuccess = threadLocalDecoder->getRawFramePayloads(ts, packet.compressedPayload, packet.metadataPayload, packet.width, packet.height, packet.compressionType);
        }
        catch (const std::exception& e) {
            LogToFile(std::string("[App::ioWorkerLoop] EXCEPTION in getRawFramePayloads for TS ") + std::to_string(ts) + " (idx " + std::to_string(frameIndexInCurrentFile_io) + "): " + e.what());
            payloadSuccess = false;
        }

        if (payloadSuccess) {
            m_decodeQueue.push(std::move(packet));
        }
        else {
            LogToFile(std::string("[App::ioWorkerLoop] Failed to get raw payloads for TS ") + std::to_string(ts) + " file '" + fs::path(currentFileBeingProcessed_io).filename().string() + "', frame " + std::to_string(frameIndexInCurrentFile_io) + ". Skipping.");
        }

        if (m_playbackController_ptr) {
            if (!m_playbackController_ptr->isPaused()) {
                frameIndexInCurrentFile_io++;
            }
        }
        else {
            frameIndexInCurrentFile_io++;
        }
    }
    LogToFile("[App::ioWorkerLoop] I/O thread finished.");
}


void App::loadFileAtIndex(int index) {
    std::ostringstream log_entry_start;
    log_entry_start << "[App::loadFileAtIndex] START. Index: " << index;
    LogToFile(log_entry_start.str());
    auto functionStartTime = std::chrono::high_resolution_clock::now();

    size_t new_load_id = m_fileLoadIDGenerator.fetch_add(1, std::memory_order_relaxed) + 1;

    if (m_fileList.empty()) {
        LogToFile("[App::loadFileAtIndex] File list is empty, cannot load. Closing window.");
        if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }
    if (index < 0 || static_cast<size_t>(index) >= m_fileList.size()) {
        LogToFile(std::string("[App::loadFileAtIndex] Index ") + std::to_string(index) + " out of bounds for file list size " + std::to_string(m_fileList.size()) + ". Defaulting to 0.");
        index = 0;
        if (m_fileList.empty()) {
            if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            return;
        }
    }

    m_currentFileIndex = index;
    const std::string& newFilePath = m_fileList[m_currentFileIndex];
    LogToFile(std::string("[App::loadFileAtIndex] Target file: '") + fs::path(newFilePath).filename().string() + "', New LoadID: " + std::to_string(new_load_id));

    if (m_audio) m_audio->setForceMute(true);

    LogToFile("App::loadFileAtIndex Stopping worker threads (if running)...");
    m_threadsShouldStop.store(true, std::memory_order_release);
    m_ioThreadFileCv.notify_all();
    m_decodeQueue.stop_operations();
    m_gpuUploadQueue.stop_operations();
    m_availableStagingBufferIndices.stop_operations();

    auto joinStartTime = std::chrono::high_resolution_clock::now();
    if (m_ioThread.joinable())   m_ioThread.join();
    if (m_decodeThread.joinable()) m_decodeThread.join();
    auto joinEndTime = std::chrono::high_resolution_clock::now();
    LogToFile(std::string("App::loadFileAtIndex Worker threads joined in ") + std::to_string(std::chrono::duration<double, std::milli>(joinEndTime - joinStartTime).count()) + " ms");

    if (m_device != VK_NULL_HANDLE) {
        auto gpuIdleStartTime = std::chrono::high_resolution_clock::now();
        vkDeviceWaitIdle(m_device);
        auto gpuIdleEndTime = std::chrono::high_resolution_clock::now();
        LogToFile(std::string("App::loadFileAtIndex vkDeviceWaitIdle completed in ") + std::to_string(std::chrono::duration<double, std::milli>(gpuIdleEndTime - gpuIdleStartTime).count()) + " ms");
    }

    LogToFile("App::loadFileAtIndex Clearing queues and resetting states.");
    m_decodeQueue.clear();
    m_gpuUploadQueue.clear();
    m_availableStagingBufferIndices.clear();

    m_availableStagingBufferIndices.resume_operations();
    for (size_t i = 0; i < kNumPersistentStagingBuffers; ++i) {
        m_availableStagingBufferIndices.push(i);
    }

    std::fill(m_inFlightStagingBufferIndices.begin(), m_inFlightStagingBufferIndices.end(), std::nullopt);
    m_hasLastSuccessfullyUploadedPacket.store(false, std::memory_order_release);

    m_decodedWidth = 0;
    m_decodedHeight = 0;

    m_decoderWrapper.reset();
    m_decoderWrapper_ptr = nullptr;
    std::vector<motioncam::Timestamp> video_frames_from_main_decoder;
    nlohmann::json containerMetaForFile;

    try {
        m_decoderWrapper = std::make_unique<DecoderWrapper>(newFilePath);
        m_decoderWrapper_ptr = m_decoderWrapper.get();
        video_frames_from_main_decoder = m_decoderWrapper->getDecoder()->getFrames();
        containerMetaForFile = m_decoderWrapper->getContainerMetadata();
        std::ostringstream log_oss_dec_main;
        log_oss_dec_main << "[App::loadFileAtIndex] Main DecoderWrapper re-created for: '" << fs::path(newFilePath).filename().string()
            << "'. Frames: " << video_frames_from_main_decoder.size();
        if (!video_frames_from_main_decoder.empty()) {
            log_oss_dec_main << ", First Main Decoder VideoTS: " << video_frames_from_main_decoder.front();
        }
        LogToFile(log_oss_dec_main.str());
    }
    catch (const std::exception& e) {
        LogToFile(std::string("[App::loadFileAtIndex] ERROR loading file (main decoder): '") + fs::path(newFilePath).filename().string() + "' - " + e.what());
        m_decoderWrapper_ptr = nullptr;
        if (m_playbackController_ptr) {
            m_playbackController_ptr->processNewSegment({}, 0, std::chrono::steady_clock::now());
        }
        {
            std::lock_guard<std::mutex> lock(m_ioThreadFileMutex);
            m_ioThreadCurrentFilePath = "";
            m_activeFileLoadID.store(new_load_id, std::memory_order_release);
            m_ioThreadFileChanged.store(true, std::memory_order_release);
        }
        m_ioThreadFileCv.notify_all();
        m_threadsShouldStop.store(false, std::memory_order_release);
        m_decodeQueue.resume_operations();
        m_gpuUploadQueue.resume_operations();
        launchWorkerThreads();
        return;
    }

    auto blackLevelVec = containerMetaForFile.value("blackLevel", std::vector<double>{0.0});
    m_staticBlack = blackLevelVec.empty() ? 0.0 : std::accumulate(blackLevelVec.begin(), blackLevelVec.end(), 0.0) / blackLevelVec.size();
    m_staticWhite = containerMetaForFile.value("whiteLevel", 65535.0);
    m_cfaStringFromMetadata = containerMetaForFile.value("sensorArrangment", containerMetaForFile.value("sensorArrangement", "BGGR"));
    m_cfaTypeFromMetadata = Renderer_VK::getCfaType(m_cfaStringFromMetadata);
    LogToFile(std::string("[App::loadFileAtIndex] Metadata parsed: Black=") + std::to_string(m_staticBlack) + ", White=" + std::to_string(m_staticWhite) + ", CFA=" + m_cfaStringFromMetadata + " (type " + std::to_string(m_cfaTypeFromMetadata) + ")");

    if (!m_firstFileLoaded && !m_isFullscreen && m_window) {
        // ... (your existing window resize logic) ...
    }

    if (!m_playbackController_ptr) {
        m_playbackController = std::make_unique<PlaybackController>();
        m_playbackController_ptr = m_playbackController.get();
    }

    m_playbackStartTime = std::chrono::steady_clock::now();
    m_pauseBegan = m_playbackStartTime;

    std::ostringstream log_oss_pb;
    log_oss_pb << "[App::loadFileAtIndex] -> PlaybackController::processNewSegment for '" << fs::path(newFilePath).filename().string()
        << "' WallTime Anchor: " << m_playbackStartTime.time_since_epoch().count();
    LogToFile(log_oss_pb.str());

    nlohmann::json firstFrameMetaForPB;
    int64_t firstVideoFrameTimestampNs = 0;

    if (!video_frames_from_main_decoder.empty()) {
        firstVideoFrameTimestampNs = video_frames_from_main_decoder.front();
        RawBytes dummyPixelData;
        try {
            m_decoderWrapper_ptr->getDecoder()->loadFrame(firstVideoFrameTimestampNs, dummyPixelData, firstFrameMetaForPB);
        }
        catch (const std::exception& e) {
            LogToFile(std::string("[App::loadFileAtIndex] Error loading first frame metadata for PB (main decoder): ") + e.what());
            firstFrameMetaForPB["timestamp"] = firstVideoFrameTimestampNs;
        }
    }
    else {
        LogToFile("[App::loadFileAtIndex] No frames in main decoder for PB::processNewSegment, passing empty meta.");
    }
    m_playbackController_ptr->processNewSegment(firstFrameMetaForPB, video_frames_from_main_decoder.size(), m_playbackStartTime);
    LogToFile("[App::loadFileAtIndex] PlaybackController processed new segment.");

    {
        std::lock_guard<std::mutex> lock(m_ioThreadFileMutex);
        m_ioThreadCurrentFilePath = newFilePath;
        m_activeFileLoadID.store(new_load_id, std::memory_order_release);
        m_ioThreadFileChanged.store(true, std::memory_order_release);
        std::ostringstream log_oss_io_signal;
        log_oss_io_signal << "[App::loadFileAtIndex] Signaling IO thread. Path: " << fs::path(m_ioThreadCurrentFilePath).filename().string()
            << ", LoadID: " << new_load_id;
        LogToFile(log_oss_io_signal.str());
    }

    LogToFile("App::loadFileAtIndex Restarting worker threads and resuming queues (after state update).");
    m_threadsShouldStop.store(false, std::memory_order_release);
    m_decodeQueue.resume_operations();
    m_gpuUploadQueue.resume_operations();
    launchWorkerThreads();

    if (m_decoderWrapper_ptr && m_decoderWrapper_ptr->getDecoder() && m_audio) {
        auto* audio_loader_ref_ptr = m_decoderWrapper_ptr->makeFreshAudioLoader();
        if (audio_loader_ref_ptr) {
            LogToFile(std::string("[App::loadFileAtIndex] -> AudioController::reset for '") + fs::path(newFilePath).filename().string() + "' with firstVideoFrameTsNs: " + std::to_string(firstVideoFrameTimestampNs));
            m_audio->setForceMute(false);
            m_audio->reset(audio_loader_ref_ptr, firstVideoFrameTimestampNs);
        }
        else {
            LogToFile("[App::loadFileAtIndex] Failed to get fresh audio loader for new file.");
        }
    }

    if (m_rendererVk) {
        m_rendererVk->resetPanOffsets();
        m_rendererVk->resetDimensions();
        if (m_playbackController_ptr) {
            m_rendererVk->setZoomNativePixels(m_playbackController_ptr->isZoomNativePixels());
        }
    }

    if (m_playbackController_ptr && m_audio) {
        m_audio->setPaused(m_playbackController_ptr->isPaused());
    }

    m_ioThreadFileCv.notify_all();

    auto functionEndTime = std::chrono::high_resolution_clock::now();
    LogToFile(std::string("App::loadFileAtIndex Total execution time: ") + std::to_string(std::chrono::duration<double, std::milli>(functionEndTime - functionStartTime).count()) + " ms");
    LogToFile(std::string("[App::loadFileAtIndex] File loading setup complete for: '") + fs::path(newFilePath).filename().string() + "' with LoadID: " + std::to_string(new_load_id));
}


void App::performSeek(size_t new_frame_index) {
    if (!m_playbackController_ptr || !m_decoderWrapper_ptr || !m_decoderWrapper_ptr->getDecoder()) {
        LogToFile("[App::performSeek] Conditions not met for seek (no playback controller or decoder).");
        return;
    }

    const auto& media_timestamps = m_decoderWrapper_ptr->getDecoder()->getFrames();
    if (media_timestamps.empty()) {
        LogToFile("[App::performSeek] Cannot seek, no media timestamps available.");
        return;
    }

    size_t current_load_id_on_entry = m_activeFileLoadID.load(std::memory_order_acquire);
    LogToFile(std::string("[App::performSeek] START. TargetIdx: ") + std::to_string(new_frame_index) +
        ", CurrentFileLoadID (before update): " + std::to_string(current_load_id_on_entry) +
        ". Current PB paused state: " + (m_playbackController_ptr->isPaused() ? "Paused" : "Playing"));

    m_playbackController_ptr->seekToFrame(new_frame_index, media_timestamps);
    LogToFile(std::string("[App::performSeek] PB seekToFrame done. New PB WallClockAnchor: ") + std::to_string(m_playbackController_ptr->getWallClockAnchorForSegment().time_since_epoch().count()));

    LogToFile("[App::performSeek] Flushing queues and resetting packet state after PB update.");
    m_gpuUploadQueue.stop_operations(); m_gpuUploadQueue.clear(); m_gpuUploadQueue.resume_operations();
    m_decodeQueue.stop_operations(); m_decodeQueue.clear(); m_decodeQueue.resume_operations();

    m_availableStagingBufferIndices.stop_operations();
    m_availableStagingBufferIndices.clear();
    m_availableStagingBufferIndices.resume_operations();
    for (size_t i = 0; i < kNumPersistentStagingBuffers; ++i) {
        m_availableStagingBufferIndices.push(i);
    }
    std::fill(m_inFlightStagingBufferIndices.begin(), m_inFlightStagingBufferIndices.end(), std::nullopt);
    m_hasLastSuccessfullyUploadedPacket.store(false, std::memory_order_release);

    size_t new_seek_load_id = m_fileLoadIDGenerator.fetch_add(1, std::memory_order_relaxed) + 1;

    {
        std::lock_guard<std::mutex> lock(m_ioThreadFileMutex);
        m_activeFileLoadID.store(new_seek_load_id, std::memory_order_release);
        LogToFile(std::string("[App::performSeek] New ActiveFileLoadID for seek: ") + std::to_string(new_seek_load_id));
        m_ioThreadFileChanged.store(true, std::memory_order_release);
    }
    m_ioThreadFileCv.notify_all();

    if (m_audio && m_decoderWrapper_ptr && m_decoderWrapper_ptr->getDecoder()) {
        auto* freshAudioLoader = m_decoderWrapper_ptr->makeFreshAudioLoader();
        if (freshAudioLoader) {
            // For seek, the audio anchor should be the timestamp of the *new current frame*.
            std::optional<int64_t> currentFrameMediaTsOpt = m_playbackController_ptr->getCurrentFrameMediaTimestamp(media_timestamps);
            if (currentFrameMediaTsOpt.has_value()) {
                LogToFile(std::string("[App::performSeek] -> AudioController::reset with new current video frame TS: ") + std::to_string(currentFrameMediaTsOpt.value()));
                m_audio->reset(freshAudioLoader, currentFrameMediaTsOpt.value());
            }
            else {
                // Fallback if somehow the current frame TS isn't available (should not happen if media_timestamps is not empty and new_frame_index is valid)
                std::optional<int64_t> firstFrameMediaTsOpt = m_playbackController_ptr->getFirstFrameMediaTimestampOfSegment();
                LogToFile("[App::performSeek] WARNING: currentFrameMediaTsOpt was null during seek for audio reset. Falling back to segment's first frame TS or 0.");
                m_audio->reset(freshAudioLoader, firstFrameMediaTsOpt.value_or(0));
            }
            if (m_playbackController_ptr) {
                m_audio->setPaused(m_playbackController_ptr->isPaused());
                LogToFile(std::string("[App::performSeek] Audio pause state synced to PB: ") + (m_playbackController_ptr->isPaused() ? "Paused" : "Playing"));
            }
        }
        else {
            LogToFile("[App::performSeek] Failed to get fresh audio loader for audio reset during seek.");
        }
    }
    LogToFile(std::string("[App::performSeek] Seek processing complete. Current PB state (paused?): ") + (m_playbackController_ptr->isPaused() ? "Yes" : "No"));
}

void App::recordPauseTime() {
    m_pauseBegan = std::chrono::steady_clock::now();
    LogToFile(std::string("[App::recordPauseTime] Playback paused. Storing pause time. m_pauseBegan epoch ns: ") + std::to_string(m_pauseBegan.time_since_epoch().count()));
}

void App::anchorPlaybackTimeForResume() {
    std::ostringstream log_stream;
    log_stream << "[App::anchorPlaybackTimeForResume] Called.";

    if (!m_playbackController_ptr) {
        log_stream << " No playback controller, returning.";
        LogToFile(log_stream.str());
        return;
    }

    std::chrono::steady_clock::time_point new_wall_clock_anchor;
    const std::vector<motioncam::Timestamp>* pFrames = nullptr;
    bool has_decoder_and_frames = false;

    if (m_decoderWrapper_ptr && m_decoderWrapper_ptr->getDecoder()) {
        pFrames = &m_decoderWrapper_ptr->getDecoder()->getFrames();
        if (pFrames && !pFrames->empty()) {
            has_decoder_and_frames = true;
        }
    }

    std::optional<int64_t> currentFrameMediaTsOpt;
    std::optional<int64_t> firstFrameMediaTsOpt = m_playbackController_ptr->getFirstFrameMediaTimestampOfSegment();

    if (has_decoder_and_frames) {
        currentFrameMediaTsOpt = m_playbackController_ptr->getCurrentFrameMediaTimestamp(*pFrames);

        log_stream << " CurrentFrameIdxForAnchor: " << m_playbackController_ptr->getCurrentFrameIndex()
            << ", currentFrameMediaTsOpt: " << (currentFrameMediaTsOpt.has_value() ? std::to_string(currentFrameMediaTsOpt.value()) : "null")
            << ", firstFrameMediaTsOpt (Segment Start): " << (firstFrameMediaTsOpt.has_value() ? std::to_string(firstFrameMediaTsOpt.value()) : "null");

        if (currentFrameMediaTsOpt.has_value() && firstFrameMediaTsOpt.has_value()) {
            int64_t deltaVideoNsFromSegmentStart = currentFrameMediaTsOpt.value() - firstFrameMediaTsOpt.value();
            if (deltaVideoNsFromSegmentStart < 0) {
                log_stream << " | WARN: Negative deltaVideoNsFromSegmentStart (" << deltaVideoNsFromSegmentStart << ") for frame "
                    << m_playbackController_ptr->getCurrentFrameIndex() << ". Clamping to 0.";
                deltaVideoNsFromSegmentStart = 0;
            }
            auto now_for_anchor = std::chrono::steady_clock::now();
            new_wall_clock_anchor = now_for_anchor - std::chrono::nanoseconds(deltaVideoNsFromSegmentStart);

            log_stream << " | Calculated for PLAYING state. DeltaVideoNsFromSegmentStart: " << deltaVideoNsFromSegmentStart
                << ", NowEpochNs: " << now_for_anchor.time_since_epoch().count()
                << ", CalculatedNewAnchorEpochNs: " << new_wall_clock_anchor.time_since_epoch().count();
        }
        else {
            log_stream << " | Media TS missing for precise anchor. Falling back to pause duration if available.";
            if (m_pauseBegan.time_since_epoch().count() != 0) {
                auto prev_anchor = m_playbackController_ptr->getWallClockAnchorForSegment();
                auto now_val = std::chrono::steady_clock::now();
                auto pause_duration = now_val - m_pauseBegan;
                new_wall_clock_anchor = prev_anchor + pause_duration;
                log_stream << " prev_anchor_ns:" << prev_anchor.time_since_epoch().count() << " pause_duration_ns:" << pause_duration.count();
            }
            else {
                new_wall_clock_anchor = std::chrono::steady_clock::now();
                log_stream << " | Fallback: m_pauseBegan not set. Using current time as anchor.";
            }
        }
    }
    else {
        log_stream << " | No decoder/frames. Anchoring based on pause duration if available.";
        if (m_pauseBegan.time_since_epoch().count() != 0) {
            auto prev_anchor = m_playbackController_ptr->getWallClockAnchorForSegment();
            auto now_val = std::chrono::steady_clock::now();
            auto pause_duration = now_val - m_pauseBegan;
            new_wall_clock_anchor = prev_anchor + pause_duration;
            log_stream << " prev_anchor_ns:" << prev_anchor.time_since_epoch().count() << " pause_duration_ns:" << pause_duration.count();
        }
        else {
            new_wall_clock_anchor = std::chrono::steady_clock::now();
            log_stream << " | No decoder/frames & m_pauseBegan not set. Using current time as anchor.";
        }
    }
    LogToFile(log_stream.str());

    m_playbackStartTime = new_wall_clock_anchor;
    m_playbackController_ptr->setWallClockAnchorForSegment(m_playbackStartTime);

    if (m_audio && m_decoderWrapper_ptr && m_decoderWrapper_ptr->getDecoder()) {
        auto* freshAudioLoader = m_decoderWrapper_ptr->makeFreshAudioLoader();
        if (freshAudioLoader) {
            // currentFrameMediaTsOpt was populated above if has_decoder_and_frames was true
            if (currentFrameMediaTsOpt.has_value()) {
                LogToFile(std::string("[App::anchorPlaybackTimeForResume] -> AudioController::reset with CURRENT (paused) video frame TS: ") + std::to_string(currentFrameMediaTsOpt.value()));
                m_audio->reset(freshAudioLoader, currentFrameMediaTsOpt.value());
            }
            else {
                // Fallback if currentFrameMediaTsOpt is not available 
                // (e.g. no frames loaded yet, or some error in getting the current frame's TS)
                // In this case, firstFrameMediaTsOpt (which is the segment's start) is the best guess.
                LogToFile("[App::anchorPlaybackTimeForResume] WARNING: currentFrameMediaTsOpt was null for audio reset on resume. Falling back to segment's first frame TS or 0.");
                m_audio->reset(freshAudioLoader, firstFrameMediaTsOpt.value_or(0));
            }
            if (m_playbackController_ptr) {
                m_audio->setPaused(m_playbackController_ptr->isPaused());
            }
        }
        else {
            LogToFile("[App::anchorPlaybackTimeForResume] Failed to get fresh audio loader for audio reset on resume.");
        }
    }
    m_pauseBegan = {};
}

void App::softDeleteCurrentFile() {
    if (m_fileList.empty() || m_currentFileIndex < 0 || static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) {
        LogToFile("[App::softDeleteCurrentFile] No valid file to delete or index out of bounds.");
        return;
    }

    fs::path currentFilePathFs = m_fileList[m_currentFileIndex];
    LogToFile(std::string("[App::softDeleteCurrentFile] Attempting to soft delete: ") + currentFilePathFs.string());

    if (m_playbackController_ptr && !m_playbackController_ptr->isPaused()) {
        m_playbackController_ptr->togglePause();
        if (m_audio) m_audio->setPaused(true);
        recordPauseTime();
    }

    {
        std::lock_guard<std::mutex> lock(m_ioThreadFileMutex);
        m_ioThreadCurrentFilePath = "";
        m_activeFileLoadID.store(m_fileLoadIDGenerator.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
        LogToFile(std::string("[App::softDeleteCurrentFile] New ActiveFileLoadID for delete op: ") + std::to_string(m_activeFileLoadID.load(std::memory_order_relaxed)));
        m_ioThreadFileChanged.store(true, std::memory_order_release);
    }
    m_ioThreadFileCv.notify_all();

    m_decodeQueue.stop_operations(); m_decodeQueue.clear(); m_decodeQueue.resume_operations();
    m_gpuUploadQueue.stop_operations(); m_gpuUploadQueue.clear(); m_gpuUploadQueue.resume_operations();

    m_availableStagingBufferIndices.stop_operations();
    m_availableStagingBufferIndices.clear();
    m_availableStagingBufferIndices.resume_operations();
    for (size_t i = 0; i < kNumPersistentStagingBuffers; ++i) {
        m_availableStagingBufferIndices.push(i);
    }
    std::fill(m_inFlightStagingBufferIndices.begin(), m_inFlightStagingBufferIndices.end(), std::nullopt);
    m_hasLastSuccessfullyUploadedPacket.store(false, std::memory_order_release);

    m_decoderWrapper.reset();
    m_decoderWrapper_ptr = nullptr;
    if (m_audio) { m_audio->setForceMute(true); m_audio->reset(nullptr, 0); }

    fs::path folder = currentFilePathFs.parent_path();
    fs::path deletedFolder = folder / "_deleted_mcraw_files_";

    try {
        if (!fs::exists(deletedFolder)) {
            fs::create_directory(deletedFolder);
            LogToFile(std::string("[App::softDeleteCurrentFile] Created directory: ") + deletedFolder.string());
        }
        fs::path destinationPath = deletedFolder / currentFilePathFs.filename();

        if (fs::exists(destinationPath)) {
            std::string base = destinationPath.stem().string();
            std::string ext = destinationPath.extension().string();
            int counter = 1;
            fs::path tempDestPath;
            do {
                std::ostringstream oss;
                oss << base << "_(" << counter++ << ")" << ext;
                tempDestPath = deletedFolder / oss.str();
            } while (fs::exists(tempDestPath));
            destinationPath = tempDestPath;
        }

        fs::rename(currentFilePathFs, destinationPath);
        LogToFile(std::string("[App::softDeleteCurrentFile] Moved '") + currentFilePathFs.string() + "' to '" + destinationPath.string() + "'");
#ifndef NDEBUG
        std::cout << "Moved " << currentFilePathFs.string() << " to " << destinationPath.string() << std::endl;
#endif

        m_fileList.erase(m_fileList.begin() + m_currentFileIndex);

        if (m_fileList.empty()) {
            LogToFile("[App::softDeleteCurrentFile] Playlist empty after delete. Closing window.");
#ifndef NDEBUG
            std::cout << "Playlist empty after delete. Closing window." << std::endl;
#endif
            if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            return;
        }

        if (static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) {
            m_currentFileIndex = static_cast<int>(m_fileList.size()) - 1;
        }

        bool tempFirstLoaded = m_firstFileLoaded;
        m_firstFileLoaded = true;
        loadFileAtIndex(m_currentFileIndex);
        m_firstFileLoaded = tempFirstLoaded;

    }
    catch (const fs::filesystem_error& e) {
        LogToFile(std::string("[App::softDeleteCurrentFile] Error during soft delete: ") + e.what() + ". For file: " + currentFilePathFs.string());
        std::cerr << "Error during soft delete for '" << currentFilePathFs.string() << "': " << e.what() << std::endl;

        std::string originalAnchorFilePath = this->m_filePath;
        if (m_currentFileIndex >= 0 && static_cast<size_t>(m_currentFileIndex) < m_fileList.size() && fs::exists(m_fileList[m_currentFileIndex])) {
            originalAnchorFilePath = m_fileList[m_currentFileIndex];
        }
        else if (!m_fileList.empty() && fs::exists(m_fileList[0])) {
            originalAnchorFilePath = m_fileList[0];
        }

        m_fileList.clear();
        fs::path anchorPathFs = fs::absolute(originalAnchorFilePath);
        fs::path parent_folder_of_anchor = anchorPathFs.parent_path();
        if (!fs::exists(parent_folder_of_anchor)) {
            parent_folder_of_anchor = fs::current_path();
            LogToFile(std::string("[App::softDeleteCurrentFile] Anchor parent folder not found, using CWD: ") + parent_folder_of_anchor.string());
        }

        LogToFile(std::string("[App::softDeleteCurrentFile] Rebuilding playlist from folder: ") + parent_folder_of_anchor.string());
        for (const auto& entry : fs::directory_iterator(parent_folder_of_anchor)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mcraw") {
                m_fileList.push_back(entry.path().string());
            }
        }
        std::sort(m_fileList.begin(), m_fileList.end());

        auto it = std::find(m_fileList.begin(), m_fileList.end(), anchorPathFs.string());
        if (it != m_fileList.end()) {
            m_currentFileIndex = static_cast<int>(std::distance(m_fileList.begin(), it));
        }
        else if (!m_fileList.empty()) {
            m_currentFileIndex = 0;
        }
        else {
            LogToFile("[App::softDeleteCurrentFile] Playlist empty after attempting rebuild. Closing window.");
            if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            return;
        }

        bool tempFirstLoaded = m_firstFileLoaded;
        m_firstFileLoaded = true;
        loadFileAtIndex(m_currentFileIndex);
        m_firstFileLoaded = tempFirstLoaded;
    }
}

void App::saveCurrentFrameAsDng() {
    if (!m_decoderWrapper_ptr || !m_decoderWrapper_ptr->getDecoder() || !m_playbackController_ptr || m_fileList.empty() || m_currentFileIndex < 0 || static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) {
        LogToFile("[App::saveCurrentFrameAsDng] Conditions not met for DNG save.");
        std::cerr << "DNG Save: Conditions not met." << std::endl;
        return;
    }

    std::string currentMcrawPathStr = m_fileList[m_currentFileIndex];
    fs::path currentMcrawPath = currentMcrawPathStr;
    fs::path dngOutputDir = currentMcrawPath.parent_path() / (currentMcrawPath.stem().string() + "_DNG_Exports");

    try {
        if (!fs::exists(dngOutputDir)) {
            fs::create_directories(dngOutputDir);
        }
    }
    catch (const fs::filesystem_error& e) {
        LogToFile(std::string("[App::saveCurrentFrameAsDng] Failed to create output directory ") + dngOutputDir.string() + ": " + e.what());
        return;
    }

    size_t frameIdxToSave = m_playbackController_ptr->getCurrentFrameIndex();
    const auto& frameTimestamps = m_decoderWrapper_ptr->getDecoder()->getFrames();

    if (frameIdxToSave >= frameTimestamps.size()) {
        LogToFile(std::string("[App::saveCurrentFrameAsDng] Frame index out of bounds. Index: ") + std::to_string(frameIdxToSave) + ", Total frames: " + std::to_string(frameTimestamps.size()));
        return;
    }

    motioncam::Timestamp ts = frameTimestamps[frameIdxToSave];
    RawBytes rawFrameDataBuffer;
    nlohmann::json frameMetadata;

    try {
        m_decoderWrapper_ptr->getDecoder()->loadFrame(ts, rawFrameDataBuffer, frameMetadata);
    }
    catch (const std::exception& e) {
        LogToFile(std::string("[App::saveCurrentFrameAsDng] Failed to load frame data for DNG export: ") + e.what());
        return;
    }

    const auto& containerMetadata = m_decoderWrapper_ptr->getContainerMetadata();
    char dngFilename[256];
    const std::string stem_str = currentMcrawPath.stem().string();
    snprintf(dngFilename, sizeof(dngFilename), "%s_frame_%06zu_ts_%lld.dng",
        stem_str.c_str(),
        frameIdxToSave,
        static_cast<long long>(ts));

    fs::path outputDngPath = dngOutputDir / dngFilename;
    std::string errorMsg;

    LogToFile(std::string("[App::saveCurrentFrameAsDng] Attempting to save to ") + outputDngPath.string());
    if (writeDngInternal(outputDngPath.string(), rawFrameDataBuffer, frameMetadata, containerMetadata, errorMsg)) {
        LogToFile(std::string("[App::saveCurrentFrameAsDng] Successfully saved DNG: ") + outputDngPath.string());
    }
    else {
        LogToFile(std::string("[App::saveCurrentFrameAsDng] Failed to write DNG: ") + errorMsg);
    }
}

void App::convertCurrentFileToDngs() {
    if (!m_decoderWrapper_ptr || !m_decoderWrapper_ptr->getDecoder() || m_fileList.empty() || m_currentFileIndex < 0 || static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) {
        LogToFile("[App::convertCurrentFileToDngs] Conditions not met for DNG export.");
        return;
    }

    std::string currentMcrawPathStr = m_fileList[m_currentFileIndex];
    fs::path currentMcrawPath = currentMcrawPathStr;
    fs::path dngOutputDir = currentMcrawPath.parent_path() / (currentMcrawPath.stem().string() + "_DNG_Exports");

    try {
        if (!fs::exists(dngOutputDir)) {
            fs::create_directories(dngOutputDir);
        }
    }
    catch (const fs::filesystem_error& e) {
        LogToFile(std::string("[App::convertCurrentFileToDngs] Failed to create output dir: ") + dngOutputDir.string() + " - " + e.what());
        return;
    }

    const auto& frameTimestamps = m_decoderWrapper_ptr->getDecoder()->getFrames();
    const auto& containerMetadata = m_decoderWrapper_ptr->getContainerMetadata();

    LogToFile(std::string("[App::convertCurrentFileToDngs] Starting DNG conversion for ") + std::to_string(frameTimestamps.size()) + " frames from " + currentMcrawPathStr);

    bool wasPausedOriginalState = true;
    if (m_playbackController_ptr) {
        wasPausedOriginalState = m_playbackController_ptr->isPaused();
        if (!wasPausedOriginalState) {
            m_playbackController_ptr->togglePause();
            if (m_audio) m_audio->setPaused(true);
            recordPauseTime();
        }
    }

    int successCount = 0;
    int failCount = 0;

    for (size_t i = 0; i < frameTimestamps.size(); ++i) {
        if (m_window && glfwWindowShouldClose(m_window)) {
            LogToFile("[App::convertCurrentFileToDngs] DNG export interrupted by window close request.");
            break;
        }

        glfwPollEvents();

        motioncam::Timestamp ts = frameTimestamps[i];
        RawBytes rawFrameDataBuffer;
        nlohmann::json frameMetadata;
        try {
            m_decoderWrapper_ptr->getDecoder()->loadFrame(ts, rawFrameDataBuffer, frameMetadata);

            char dngFilename[256];
            const std::string stem_str_all = currentMcrawPath.stem().string();
            snprintf(dngFilename, sizeof(dngFilename), "%s_frame_%06zu_ts_%lld.dng",
                stem_str_all.c_str(),
                i,
                static_cast<long long>(ts));
            fs::path outputDngPath = dngOutputDir / dngFilename;
            std::string errorMsg;

            if (!writeDngInternal(outputDngPath.string(), rawFrameDataBuffer, frameMetadata, containerMetadata, errorMsg)) {
                LogToFile(std::string("[App::convertCurrentFileToDngs] Failed DNG write for frame ") + std::to_string(i) + ": " + errorMsg);
                failCount++;
            }
            else {
                successCount++;
            }
            if ((i + 1) % 20 == 0 || i == frameTimestamps.size() - 1) {
                LogToFile(std::string("[App::convertCurrentFileToDngs] Converted ") + std::to_string(i + 1) + "/" + std::to_string(frameTimestamps.size()) + " frames. Success: " + std::to_string(successCount) + ", Fail: " + std::to_string(failCount));
            }
        }
        catch (const std::exception& e) {
            LogToFile(std::string("[App::convertCurrentFileToDngs] Error processing frame ") + std::to_string(i) + " for DNG export: " + e.what());
            failCount++;
        }
    }

    LogToFile(std::string("[App::convertCurrentFileToDngs] Conversion complete for ") + currentMcrawPathStr + ". Success: " + std::to_string(successCount) + ", Failed: " + std::to_string(failCount));

    if (m_playbackController_ptr && m_playbackController_ptr->isPaused() && !wasPausedOriginalState) {
        m_playbackController_ptr->togglePause();
        if (m_audio) m_audio->setPaused(false);
        anchorPlaybackTimeForResume();
    }
}

void App::sendCurrentFileToMotionCamFS()
{
    if (m_fileList.empty() ||
        m_currentFileIndex < 0 ||
        static_cast<size_t>(m_currentFileIndex) >= m_fileList.size())
    {
        LogToFile("[App::sendToMotionCamFS] No valid file to send.");
        std::cerr << "Send to motioncam-fs: no valid file selected.\n";
        return;
    }

    const std::string currentMcrawPathStr = m_fileList[m_currentFileIndex];
    LogToFile(std::string("[App::sendToMotionCamFS] Preparing to send: ")
        + currentMcrawPathStr);

    std::string motionCamFsExePath;
#ifdef _WIN32
    char modulePathChars[MAX_PATH];
    GetModuleFileNameA(nullptr, modulePathChars, MAX_PATH);
    fs::path currentExePath(modulePathChars);
    motionCamFsExePath =
        (currentExePath.parent_path() / "motioncam-fs.exe").string();
#else
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    fs::path currentExePath = (count > 0) ? fs::path(std::string(result, count)) : fs::path("");

    fs::path рядом_exe = currentExePath.parent_path() / "motioncam-fs";
    if (fs::exists(рядом_exe)) {
        motionCamFsExePath = рядом_exe.string();
    }
    else {
        motionCamFsExePath = "motioncam-fs"; // Try PATH
    }
#endif

    bool motionCamFsFound = fs::exists(motionCamFsExePath);
#ifndef _WIN32 
    // If not found via relative path, try checking if it's in PATH
    if (!motionCamFsFound && motionCamFsExePath.find('/') == std::string::npos) {
        motionCamFsFound = (system(("command -v " + motionCamFsExePath + " >/dev/null 2>&1").c_str()) == 0);
    }
#endif

    if (!motionCamFsFound)
    {
        LogToFile(std::string("[App::sendToMotionCamFS] ERROR: motioncam-fs not "
            "found at expected location or in system PATH. Tried: ") + motionCamFsExePath);
        return;
    }

    std::string command = std::string("\"") + motionCamFsExePath +
        "\" -f \"" + currentMcrawPathStr + "\"";
    LogToFile(std::string("[App::sendToMotionCamFS] Command: ") + command);

#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    if (wlen == 0) { LogToFile("[App::sendToMotionCamFS] UTF-16 len fail."); return; }
    std::wstring wcmd(wlen, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wcmd.data(), wlen) == 0)
    {
        LogToFile("[App::sendToMotionCamFS] UTF-16 conv fail."); return;
    }

    STARTUPINFOW si{};  PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr, &si, &pi))
    {
        LogToFile(std::string("[App::sendToMotionCamFS] CreateProcessW failed. ")
            + std::to_string(GetLastError()));
    }
    else { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
#else
    // Run in background on Linux/macOS
    if (system((command + " &").c_str()) != 0)
        LogToFile("[App::sendToMotionCamFS] system() failed or returned non-zero.");
#endif
}
void App::sendAllPlaylistFilesToMotionCamFS()
{
    if (m_fileList.empty())
    {
        LogToFile("[App::sendAllToMotionCamFS] Playlist is empty. Nothing to send.");
        return;
    }

    std::string motionCamFsExePath;
#ifdef _WIN32
    char modulePathChars[MAX_PATH];
    GetModuleFileNameA(nullptr, modulePathChars, MAX_PATH);
    fs::path exeDir(modulePathChars);
    motionCamFsExePath =
        (exeDir.parent_path() / "motioncam-fs.exe").string();
#else
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    fs::path currentExePath = (count > 0) ? fs::path(std::string(result, count)) : fs::path("");

    fs::path рядом_exe = currentExePath.parent_path() / "motioncam-fs";
    if (fs::exists(рядом_exe)) {
        motionCamFsExePath = рядом_exe.string();
    }
    else {
        motionCamFsExePath = "motioncam-fs"; // Try PATH
    }
#endif

    bool motionCamFsFound = fs::exists(motionCamFsExePath);
#ifndef _WIN32
    // If not found via relative path, try checking if it's in PATH
    if (!motionCamFsFound && motionCamFsExePath.find('/') == std::string::npos) {
        motionCamFsFound = (system(("command -v " + motionCamFsExePath + " >/dev/null 2>&1").c_str()) == 0);
    }
#endif

    if (!motionCamFsFound)
    {
        LogToFile(std::string("[App::sendAllToMotionCamFS] ERROR: motioncam-fs "
            "not found at expected location or in system PATH. Tried: ") + motionCamFsExePath);
        return;
    }

    std::size_t ok = 0, fail = 0;

    for (const std::string& mcrawPathStr : m_fileList)
    {
        if (m_window && glfwWindowShouldClose(m_window)) {
            LogToFile("[App::sendAllToMotionCamFS] Operation interrupted by window close request.");
            break;
        }
        glfwPollEvents(); // Keep UI responsive

        LogToFile(std::string("[App::sendAllToMotionCamFS] Processing: ")
            + mcrawPathStr);

        std::string command = std::string("\"") + motionCamFsExePath +
            "\" -f \"" + mcrawPathStr + "\"";
        LogToFile(std::string("[App::sendAllToMotionCamFS] Command: ") + command);

#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
        if (wlen == 0) { ++fail; LogToFile("[App::sendAllToMotionCamFS] UTF-16 len fail for: " + mcrawPathStr); continue; }
        std::wstring wcmd(wlen, L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wcmd.data(), wlen) == 0)
        {
            ++fail; LogToFile("[App::sendAllToMotionCamFS] UTF-16 conv fail for: " + mcrawPathStr); continue;
        }

        STARTUPINFOW si{}; PROCESS_INFORMATION pi{}; si.cb = sizeof(si);
        if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            nullptr, nullptr, &si, &pi)) {
            ++fail; LogToFile(std::string("[App::sendAllToMotionCamFS] CreateProcessW failed for: ") + mcrawPathStr + " Err: " + std::to_string(GetLastError()));
        }
        else { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); ++ok; }
#else
        if (system((command + " &").c_str()) == 0) ++ok;
        else { ++fail; LogToFile("[App::sendAllToMotionCamFS] system() call failed for: " + mcrawPathStr); }
#endif
        // Optional: Small delay to avoid overwhelming system or motioncam-fs if it's not handling rapid launches well
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LogToFile(std::string("[App::sendAllToMotionCamFS] Done. Success: ")
        + std::to_string(ok) + ", Fail: " + std::to_string(fail));
}