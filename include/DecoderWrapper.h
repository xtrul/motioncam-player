#ifndef DECODER_WRAPPER_H
#define DECODER_WRAPPER_H

#include <motioncam/Decoder.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <memory>

class DecoderWrapper {
public:
    explicit DecoderWrapper(const std::string& filePath);
    ~DecoderWrapper() = default;

    /// Access raw container metadata
    const nlohmann::json& getContainerMetadata() const { return m_containerMetadata; }
    /// Access the underlying Decoder
    const motioncam::Decoder* getDecoder() const { return m_decoder.get(); }
    motioncam::Decoder* getDecoder() { return m_decoder.get(); }

    /// Create a brand-new Decoder+AudioChunkLoader for rewinding audio
    motioncam::AudioChunkLoader* makeFreshAudioLoader();

private:
    std::string                         m_filePath;            ///< Path passed into ctor
    std::unique_ptr<motioncam::Decoder> m_decoder;             ///< Underlying decoder
    nlohmann::json                      m_containerMetadata;   ///< Raw metadata
};

#endif // DECODER_WRAPPER_H
