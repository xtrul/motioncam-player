#include "App/App.h"
#include "Utils/DebugLog.h"
#include <motioncam/RawData.hpp> 
#include <cstring> 
#include <chrono>  

void App::decodeWorkerLoop() {
    LogToFile("[App::decodeWorkerLoop] Decode thread started.");

    constexpr int LOCAL_MC_COMPRESSION_TYPE_NEW = 7;
    constexpr int LOCAL_MC_COMPRESSION_TYPE_LEGACY = 6;

    while (!m_threadsShouldStop.load()) {
        CompressedFramePacket compressedPacket;

        if (!m_decodeQueue.wait_pop(compressedPacket)) {
            if (m_threadsShouldStop.load()) {
                LogToFile("[App::decodeWorkerLoop] Stop signal received while waiting for decode queue (wait_pop returned false), exiting.");
                break;
            }
#ifndef NDEBUG
            LogToFile("[App::decodeWorkerLoop] wait_pop on m_decodeQueue returned false unexpectedly. Continuing.");
#endif
            continue;
        }

        // Use global constant directly
        const size_t gpuQueueThrottleLimit = kNumPersistentStagingBuffers + 4;
        if (m_gpuUploadQueue.size() >= gpuQueueThrottleLimit) {
#ifndef NDEBUG
            LogToFile(std::string("[App::decodeWorkerLoop] GPU Upload Queue near capacity (") + std::to_string(m_gpuUploadQueue.size()) + "/" + std::to_string(gpuQueueThrottleLimit) + "). Throttling decode.");
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (m_threadsShouldStop.load()) {
                LogToFile("[App::decodeWorkerLoop] Stop signal received during GPU queue throttle. Re-pushing compressed packet TS " + std::to_string(compressedPacket.timestamp) + " and exiting.");
                m_decodeQueue.push_front(std::move(compressedPacket));
                break;
            }
            m_decodeQueue.push_front(std::move(compressedPacket));
            continue;
        }

        size_t stagingIdx;
        if (!m_availableStagingBufferIndices.wait_pop(stagingIdx)) {
            if (m_threadsShouldStop.load()) {
                LogToFile("[App::decodeWorkerLoop] Stop signal: wait_pop for staging buffer returned false. Compressed packet TS " + std::to_string(compressedPacket.timestamp) + " will not be processed further.");
                break;
            }
            else {
                LogToFile("[App::decodeWorkerLoop] CRITICAL: wait_pop for staging buffer returned false unexpectedly without stop signal. Packet TS " + std::to_string(compressedPacket.timestamp) + ". Dropping.");
                continue;
            }
        }

        if (stagingIdx >= m_persistentStagingBuffersMappedPtrs.size() || !m_persistentStagingBuffersMappedPtrs[stagingIdx]) {
            LogToFile("[App::decodeWorkerLoop] ERROR: Invalid stagingIdx " + std::to_string(stagingIdx) + " or null mapped ptr. Dropping packet TS " + std::to_string(compressedPacket.timestamp) + ".");
            m_availableStagingBufferIndices.push(stagingIdx);
            continue;
        }
        uint16_t* targetStagingU16Ptr = static_cast<uint16_t*>(m_persistentStagingBuffersMappedPtrs[stagingIdx]);

        bool decodeSuccess = false;
        nlohmann::json frameMeta;

        try {
            if (compressedPacket.width <= 0 || compressedPacket.height <= 0) {
                LogToFile(std::string("[App::decodeWorkerLoop] Invalid dimensions in compressed packet TS ") + std::to_string(compressedPacket.timestamp) + ": " + std::to_string(compressedPacket.width) + "x" + std::to_string(compressedPacket.height));
            }
            else if (compressedPacket.compressedPayload.empty() && compressedPacket.compressionType != 0) {
                LogToFile(std::string("[App::decodeWorkerLoop] Empty compressed payload for TS ") + std::to_string(compressedPacket.timestamp) + " with compression type " + std::to_string(compressedPacket.compressionType));
            }
            else if (!targetStagingU16Ptr) {
                LogToFile(std::string("[App::decodeWorkerLoop] Null target staging pointer for TS ") + std::to_string(compressedPacket.timestamp));
            }
            else if (compressedPacket.compressionType == LOCAL_MC_COMPRESSION_TYPE_NEW) {
                if (motioncam::raw::Decode(targetStagingU16Ptr, compressedPacket.width, compressedPacket.height, compressedPacket.compressedPayload.data(), compressedPacket.compressedPayload.size()) > 0) decodeSuccess = true;
                else LogToFile(std::string("[App::decodeWorkerLoop] motioncam::raw::Decode failed for TS ") + std::to_string(compressedPacket.timestamp));
            }
            else if (compressedPacket.compressionType == LOCAL_MC_COMPRESSION_TYPE_LEGACY) {
                if (motioncam::raw::DecodeLegacy(targetStagingU16Ptr, compressedPacket.width, compressedPacket.height, compressedPacket.compressedPayload.data(), compressedPacket.compressedPayload.size()) > 0) decodeSuccess = true;
                else LogToFile(std::string("[App::decodeWorkerLoop] motioncam::raw::DecodeLegacy failed for TS ") + std::to_string(compressedPacket.timestamp));
            }
            else if (compressedPacket.compressionType == 0) {
                size_t expected_size = static_cast<size_t>(compressedPacket.width) * compressedPacket.height * sizeof(uint16_t);
                if (!compressedPacket.compressedPayload.empty() && compressedPacket.compressedPayload.size() == expected_size) {
                    memcpy(targetStagingU16Ptr, compressedPacket.compressedPayload.data(), expected_size);
                    decodeSuccess = true;
                }
                else if (compressedPacket.compressedPayload.empty() && expected_size == 0) {
                    decodeSuccess = true;
                }
                else {
                    LogToFile(std::string("[App::decodeWorkerLoop] Uncompressed payload size mismatch or empty for non-zero dimensions. TS: ") + std::to_string(compressedPacket.timestamp) +
                        ", Expected: " + std::to_string(expected_size) + ", Got: " + std::to_string(compressedPacket.compressedPayload.size()));
                }
            }
            else {
                LogToFile(std::string("[App::decodeWorkerLoop] Unknown or unhandled compression type: ") + std::to_string(compressedPacket.compressionType) + " for TS " + std::to_string(compressedPacket.timestamp));
            }

            if (decodeSuccess) {
                if (compressedPacket.metadataPayload.empty()) {
                    frameMeta = nlohmann::json::object();
                }
                else {
                    try {
                        frameMeta = nlohmann::json::parse(compressedPacket.metadataPayload.begin(), compressedPacket.metadataPayload.end());
                    }
                    catch (const nlohmann::json::parse_error& e) {
                        LogToFile(std::string("[App::decodeWorkerLoop] JSON metadata parse error for TS ") + std::to_string(compressedPacket.timestamp) + ": " + e.what());
                        decodeSuccess = false;
                    }
                }
            }
        }
        catch (const std::exception& e) {
            LogToFile(std::string("[App::decodeWorkerLoop] EXCEPTION during decode/metadata for TS ") + std::to_string(compressedPacket.timestamp) + ": " + e.what());
            decodeSuccess = false;
        }


        if (decodeSuccess) {
            GpuUploadPacket gpuPacket;
            gpuPacket.timestamp = compressedPacket.timestamp;
            gpuPacket.stagingBufferIndex = stagingIdx;
            gpuPacket.metadata = std::move(frameMeta);
            gpuPacket.width = compressedPacket.width;
            gpuPacket.height = compressedPacket.height;
            gpuPacket.frameIndex = compressedPacket.frameIndex;
            gpuPacket.fileLoadID = compressedPacket.fileLoadID;

            if (m_threadsShouldStop.load()) {
                m_availableStagingBufferIndices.push(stagingIdx);
                LogToFile("[App::decodeWorkerLoop] Stop signal before pushing to GPU queue, returning staging buffer.");
                break;
            }
            m_gpuUploadQueue.push(std::move(gpuPacket));
        }
        else {
            LogToFile(std::string("[App::decodeWorkerLoop] Decode FAILED for TS ") + std::to_string(compressedPacket.timestamp) + ". Returning staging buffer " + std::to_string(stagingIdx) + ".");
            m_availableStagingBufferIndices.push(stagingIdx);
        }
    }
    LogToFile("[App::decodeWorkerLoop] Decode thread finished.");
}
// launchWorkerThreads is defined in AppInit.cpp as part of the constructor logic now
// or should be called from there if it's meant to be run once.
// If it's meant to be callable multiple times, its definition can stay in AppDecode.cpp or move to App.cpp (general part).
// The original App.cpp had it separate. Let's assume it can stay here if it's specifically for decode/IO threads.
// However, loadFileAtIndex calls it, so it should be a general App method.
// The prompt put launchWorkerThreads in App.h private section, so its definition should be in one of the App's .cpp files.
// Let's move its definition to AppInit.cpp as it's typically part of initial setup or file loading.
// For now, I'll keep it here to match the current error scope, but it's a candidate for moving.
// **Correction**: The original App.cpp had `launchWorkerThreads` defined. The refactor implies it's a member of App.
// It's called from `loadFileAtIndex` (in `AppIO.cpp`) and potentially from `App::App` (in `AppInit.cpp`).
// So, its definition should be in a common `App` source file, or `AppInit.cpp` if primarily for init.
// The prompt split App.cpp into multiple files. `launchWorkerThreads` was not explicitly assigned.
// Given its usage, `AppInit.cpp` or a new `App_threads.cpp` would be suitable.
// For now, I'll assume its definition is correctly placed in `AppInit.cpp` or another `App_*.cpp` file.
// The error list doesn't show an unresolved external for `launchWorkerThreads`, so it's likely defined somewhere.
// The `AppDecode.cpp` file only contains `decodeWorkerLoop`.