#include "DecoderWrapper.h"

#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

DecoderWrapper::DecoderWrapper(const std::string& filePath)
    : m_filePath(filePath)
{
    if (!fs::exists(m_filePath) || !fs::is_regular_file(m_filePath)) {
        throw std::runtime_error("Input file not found: " + m_filePath);
    }
    if (fs::path(m_filePath).extension() != ".mcraw") {
        throw std::runtime_error("Input must have .mcraw extension: " + m_filePath);
    }

    // Construct the decoder (its ctor opens the file)
    m_decoder = std::make_unique<motioncam::Decoder>(m_filePath);

    // Pull container-level metadata
    m_containerMetadata = m_decoder->getContainerMetadata();

    if (!m_decoder || m_decoder->getFrames().empty()) {
        throw std::runtime_error("Decoder failed to initialize or no frames in file");
    }
}

motioncam::AudioChunkLoader* DecoderWrapper::makeFreshAudioLoader()
{
    // Destroy old decoder, then re-create it from the same path
    m_decoder.reset();
    m_decoder = std::make_unique<motioncam::Decoder>(m_filePath);
    if (!m_decoder) {
        throw std::runtime_error("Failed to re-open decoder for audio rewind");
    }
    // Update container metadata as well, though it should be the same
    m_containerMetadata = m_decoder->getContainerMetadata();


    // Return the brand-new loader, whose internal index is at zero
    return &m_decoder->loadAudio();
}
