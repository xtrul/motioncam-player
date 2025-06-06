#ifndef DECODER_WRAPPER_H
#define DECODER_WRAPPER_H

#include <string>
#include <memory> // For std::unique_ptr

#include <motioncam/Decoder.hpp> // Provides motioncam::Decoder and motioncam::AudioChunkLoader
#include <nlohmann/json.hpp>     // For nlohmann::json (assuming used by or with the decoder)

// No specific DecoderTypes.h include needed here if DecodedFrame is not directly part of its public API.
// If it were, #include "DecoderTypes.h" would be appropriate.

/**
 * @class DecoderWrapper
 * @brief Wraps the motioncam::Decoder to manage its lifecycle and provide access
 *        to decoded data and metadata.
 */
    class DecoderWrapper {
    public:
        /**
         * @brief Constructs the DecoderWrapper and initializes the underlying motioncam::Decoder.
         * @param filePath Path to the .mcraw file.
         * @throws std::runtime_error if the file cannot be opened or is invalid.
         */
        explicit DecoderWrapper(const std::string& filePath);

        /**
         * @brief Default destructor.
         */
        ~DecoderWrapper() = default;

        // Prevent copy and assignment
        DecoderWrapper(const DecoderWrapper&) = delete;
        DecoderWrapper& operator=(const DecoderWrapper&) = delete;
        DecoderWrapper(DecoderWrapper&&) = default; // Allow move
        DecoderWrapper& operator=(DecoderWrapper&&) = default; // Allow move

        /**
         * @brief Gets the container-level metadata from the .mcraw file.
         * @return Const reference to the nlohmann::json object containing metadata.
         */
        const nlohmann::json& getContainerMetadata() const { return m_containerMetadata; }

        /**
         * @brief Gets a const pointer to the underlying motioncam::Decoder.
         * @return Const pointer to the decoder, or nullptr if not initialized.
         */
        const motioncam::Decoder* getDecoder() const { return m_decoder.get(); }

        /**
         * @brief Gets a non-const pointer to the underlying motioncam::Decoder.
         * @return Pointer to the decoder, or nullptr if not initialized.
         */
        motioncam::Decoder* getDecoder() { return m_decoder.get(); }

        /**
         * @brief Creates a new motioncam::AudioChunkLoader instance for the current file.
         * This typically involves re-opening the file to reset the audio stream.
         * @return Pointer to a new AudioChunkLoader. Ownership might be an issue if
         *         the decoder is destroyed; ensure lifetime management.
         * @throws std::runtime_error if the decoder cannot be re-initialized.
         */
        motioncam::AudioChunkLoader* makeFreshAudioLoader();

    private:
        std::string                         m_filePath;
        std::unique_ptr<motioncam::Decoder> m_decoder;
        nlohmann::json                      m_containerMetadata;
};

#endif // DECODER_WRAPPER_H