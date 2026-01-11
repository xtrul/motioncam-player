#include "Decoder/DecoderWrapper.h"
#include "Utils/DebugLog.h" // For LogToFile

#include <stdexcept>
#include <filesystem> // For fs::exists, fs::is_regular_file, fs::path

namespace fs = std::filesystem;

DecoderWrapper::DecoderWrapper(const std::string& filePath)
    : m_filePath(filePath) {
    LogToFile(std::string("[DecoderWrapper] Constructor for: ") + filePath);
    if (!fs::exists(m_filePath)) {
        std::string errMsg = "DecoderWrapper: Input file does not exist: " + m_filePath;
        LogToFile(errMsg);
        throw std::runtime_error(errMsg);
    }
    if (!fs::is_regular_file(m_filePath)) {
        std::string errMsg = "DecoderWrapper: Input path is not a regular file: " + m_filePath;
        LogToFile(errMsg);
        throw std::runtime_error(errMsg);
    }
    if (fs::path(m_filePath).extension() != ".mcraw") {
        // This is a soft check, decoder library might handle other extensions if format is mcraw
        // LogToFile("[DecoderWrapper] Warning: Input file does not have a .mcraw extension: " + m_filePath);
        // For strictness, uncomment the throw below:
        // throw std::runtime_error("DecoderWrapper: Input file must have a .mcraw extension: " + m_filePath);
    }

    try {
        m_decoder = std::make_unique<motioncam::Decoder>(m_filePath);
        LogToFile(std::string("[DecoderWrapper] motioncam::Decoder initialized for: ") + m_filePath);
    }
    catch (const std::exception& e) {
        std::string errMsg = "DecoderWrapper: Failed to initialize motioncam::Decoder for '" + m_filePath + "': " + e.what();
        LogToFile(errMsg);
        throw std::runtime_error(errMsg);
    }

    m_containerMetadata = m_decoder->getContainerMetadata();
    LogToFile(std::string("[DecoderWrapper] Container metadata loaded. Frame count: ") + std::to_string(m_decoder->getFrames().size()));


    if (m_decoder->getFrames().empty()) {
        // This could be a warning or an error depending on expected content.
        // For a player, usually an error.
        std::string warnMsg = "DecoderWrapper: Decoder initialized but found no frames in file: " + m_filePath;
        LogToFile(warnMsg);
        // Consider if this should throw an error. If an mcraw can be valid but empty (e.g. audio only),
        // then this might not be an error. For a video player, it's problematic.
        // throw std::runtime_error(warnMsg); 
    }
}

motioncam::AudioChunkLoader* DecoderWrapper::makeFreshAudioLoader() {
    LogToFile(std::string("[DecoderWrapper] makeFreshAudioLoader called for: ") + m_filePath);
    // This operation is potentially costly as it re-initializes the decoder.
    // It's necessary if the underlying library doesn't support seeking/resetting audio stream independently.
    m_decoder.reset();
    try {
        m_decoder = std::make_unique<motioncam::Decoder>(m_filePath);
        LogToFile(std::string("[DecoderWrapper] motioncam::Decoder re-initialized for fresh audio loader."));
    }
    catch (const std::exception& e) {
        std::string errMsg = "DecoderWrapper: Failed to re-open motioncam::Decoder for audio rewind: " + m_filePath + "': " + e.what();
        LogToFile(errMsg);
        throw std::runtime_error(errMsg);
    }

    // The motioncam::Decoder::loadAudio() returns a reference to an internal loader.
    // If the DecoderWrapper's m_decoder is reset again, this loader becomes invalid.
    // The caller (AudioController) must be aware of this lifetime.
    return &m_decoder->loadAudio();
}