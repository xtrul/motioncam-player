// --- START OF FILE motioncam/Decoder.hpp ---
#pragma once

#include <motioncam/Container.hpp>
#include <nlohmann/json.hpp>
#include <mio/mio.hpp>

#include <string>
#include <vector>
#include <map>
#include <cstdint> // Required for std::vector<uint8_t> etc.

namespace motioncam {
    typedef int64_t Timestamp;
    typedef std::pair<Timestamp, std::vector<int16_t>> AudioChunk;

    class MotionCamException : public std::runtime_error {
    public:
        MotionCamException(const std::string& error) : runtime_error(error) {}
    };

    class IOException : public MotionCamException {
    public:
        IOException(const std::string& error) : MotionCamException(error) {}
    };

    class AudioChunkLoader {
    public:
        virtual bool next(AudioChunk& output) = 0;
        virtual ~AudioChunkLoader() = default; // Add virtual destructor
    };

    class Decoder {
    public:
        /**
         * Open and memory-map the given file path.
         * Throws IOException on error.
         */
        Decoder(const std::string& path);

        /**
         * Get container-level metadata (camera info, container params).
         */
        const nlohmann::json& getContainerMetadata() const;

        /**
         * Retrieve all frame timestamps in the container.
         */
        const std::vector<Timestamp>& getFrames() const;

        /**
         * Load a single decoded frame into a byte vector (little-endian uint16 per pixel).
         * @param timestamp  Frame timestamp to load.
         * @param outData    Byte vector to fill; resized to width*height*2.
         * @param outMetadata JSON object to receive per-frame metadata.
         */
        void loadFrame(const Timestamp timestamp,
            std::vector<uint8_t>& outData,
            nlohmann::json& outMetadata);

        /**
         * Load a single decoded frame directly into caller-provided buffer.
         * This avoids an extra memcpy by decoding straight into GPU-visible memory.
         * @param timestamp  Frame timestamp to load.
         * @param externalOutputBuffer  Pointer to pre-allocated buffer of at least width*height uint16_t slots.
         * @param externalBufferSize    Size of the buffer in bytes (must be >= width*height*sizeof(uint16_t)).
         * @param outMetadata JSON object to receive per-frame metadata.
         * @throws IOException on failure or if buffer too small.
         */
        void loadFrame(const Timestamp timestamp,
            uint16_t* externalOutputBuffer,
            size_t externalBufferSize,
            nlohmann::json& outMetadata);

        /**
         * Gets raw compressed payload and raw metadata payload for a frame.
         * Does not decompress the frame.
         * @param timestamp  Frame timestamp to load.
         * @param outCompressedPayload Byte vector to fill with compressed frame data.
         * @param outMetadataPayload Byte vector to fill with raw JSON metadata string for the frame.
         * @param outWidth Width of the frame (from metadata).
         * @param outHeight Height of the frame (from metadata).
         * @param outCompressionType Compression type of the frame (from metadata).
         * @return true if successful, false if frame not found or error reading.
         */
        bool getRawFramePayloads(
            Timestamp timestamp,
            std::vector<uint8_t>& outCompressedPayload,
            std::vector<uint8_t>& outMetadataPayload,
            int& outWidth, int& outHeight, int& outCompressionType
        );


        /**
         * Audio sample rate in Hz.
         */
        int audioSampleRateHz() const;

        /**
         * Number of audio channels.
         */
        int numAudioChannels() const;

        /**
         * Load all audio chunks into a vector.
         */
        void loadAudio(std::vector<AudioChunk>& outAudioChunks);

        /**
         * Get an AudioChunkLoader to iterate audio chunks lazily.
         */
        AudioChunkLoader& loadAudio() const;

    private:
        void init();
        size_t read(size_t offset, void* dst, size_t size, size_t items = 1) const;
        void readIndex();
        void reindexOffsets();
        void readExtra();
        // void uncompress(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst); // Was unused, removed

    private:
        mio::mmap_source mMemoryMap;
        std::vector<BufferOffset> mOffsets;
        std::vector<BufferOffset> mAudioOffsets;
        std::map<Timestamp, BufferOffset> mFrameOffsetMap;
        std::vector<Timestamp> mFrameList;
        nlohmann::json mMetadata;
        std::unique_ptr<AudioChunkLoader> mAudioLoader;
    };
} // namespace motioncam
// --- END OF FILE motioncam/Decoder.hpp ---