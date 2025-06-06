// --- START OF FILE motioncam/Decoder.cpp ---
#include <motioncam/Decoder.hpp>
#include <motioncam/RawData.hpp>

#include <cstdio>
#include <cstring>
#include <algorithm> // For std::sort, std::min

namespace motioncam {
    constexpr int MOTIONCAM_COMPRESSION_TYPE_LEGACY = 6;
    constexpr int MOTIONCAM_COMPRESSION_TYPE = 7;

    namespace {
        class AudioChunkLoaderImpl : public AudioChunkLoader {
        public:
            AudioChunkLoaderImpl(const mio::mmap_source& src, const std::vector<BufferOffset>& offsets);
            ~AudioChunkLoaderImpl() override = default; // Implement virtual destructor
            bool next(AudioChunk& output) override;

        private:
            const mio::mmap_source& mSrc;
            const std::vector<BufferOffset>& mOffsets;

            size_t mIdx;
        };

        size_t read(const mio::mmap_source& src, size_t offset, void* dst, size_t size, size_t items = 1) {
            // Calculate total bytes to read
            size_t totalBytes = size * items;

            // Check basic validity
            if (!dst || offset >= src.size()) {
                return 0;
            }

            // Calculate available bytes from offset to end of file
            size_t availableBytes = src.size() - offset;

            // Read only what's available
            size_t bytesToRead = (std::min)(totalBytes, availableBytes);
            if (bytesToRead == 0)
                return 0;

            // Get pointer to the data at the specified offset
            const char* srcPtr = src.data() + offset;

            // Copy data from memory-mapped source to destination buffer
            std::memcpy(dst, srcPtr, bytesToRead);

            // Return bytes read
            return bytesToRead;
        }

        bool loadAudioChunk(const mio::mmap_source& src, const BufferOffset& o, AudioChunk& outChunk) {
            size_t offset = o.offset;

            // Get audio data header
            Item audioDataItem{};
            offset += read(src, offset, &audioDataItem, sizeof(Item));

            if (audioDataItem.type != Type::AUDIO_DATA)
                throw IOException("Invalid audio data");

            // Read into temporary buffer
            std::vector<int16_t> tmp;

            tmp.resize((audioDataItem.size + 1) / 2);
            offset += read(src, offset, (void*)tmp.data(), audioDataItem.size);

            // Metadata should follow (this was added later so some files may not have it)
            Item audioMetadataItem{};
            size_t bytes_read_for_meta_item = read(src, offset, &audioMetadataItem, sizeof(Item));
            offset += bytes_read_for_meta_item;

            Timestamp audioTimestamp = -1;

            if (bytes_read_for_meta_item == sizeof(Item) && audioMetadataItem.type == Type::AUDIO_DATA_METADATA) {
                AudioMetadata metadata;

                offset += read(src, offset, &metadata, sizeof(AudioMetadata));
                audioTimestamp = metadata.timestampNs;
            }

            outChunk = std::make_pair(audioTimestamp, std::move(tmp));

            return true;
        }
    }
    //

    AudioChunkLoaderImpl::AudioChunkLoaderImpl(const mio::mmap_source& src, const std::vector<BufferOffset>& offsets) :
        mSrc(src), mOffsets(offsets), mIdx(0) {
    }

    bool AudioChunkLoaderImpl::next(AudioChunk& output) {
        if (mIdx >= mOffsets.size())
            return false;

        if (!loadAudioChunk(mSrc, mOffsets[mIdx], output)) {
            return false;
        }

        ++mIdx;
        return true;
    }

    //

    Decoder::Decoder(const std::string& path) {
        // Memory map input file
        std::error_code error;

        mMemoryMap.map(path, error);
        if (error)
            throw IOException("Failed to memory map " + path);

        init();
    }

    void Decoder::init() {
        Header header{};
        size_t offset = 0;

        // Check validity of file
        offset += read(offset, &header, sizeof(Header));

        // Support current version and also version 3
        if ((header.version != CONTAINER_VERSION))
            throw IOException("Invalid container version");

        if (std::memcmp(header.ident, CONTAINER_ID, sizeof(CONTAINER_ID)) != 0)
            throw IOException("Invalid header id");

        // Read camera metadata
        Item metadataItem{};
        offset += read(offset, &metadataItem, sizeof(Item));

        if (metadataItem.type != Type::METADATA)
            throw IOException("Invalid camera metadata");

        std::vector<uint8_t> metadataJson(metadataItem.size);
        offset += read(offset, metadataJson.data(), metadataItem.size);

        // Keep the camera metadata
        auto cameraMetadataString = std::string(metadataJson.begin(), metadataJson.end());
        mMetadata = nlohmann::json::parse(cameraMetadataString);

        readIndex();

        reindexOffsets();

        readExtra();

        // Create audio loader
        mAudioLoader = std::make_unique<AudioChunkLoaderImpl>(mMemoryMap, mAudioOffsets);
    }

    const std::vector<Timestamp>& Decoder::getFrames() const {
        return mFrameList;
    }

    const nlohmann::json& Decoder::getContainerMetadata() const {
        return mMetadata;
    }

    int Decoder::audioSampleRateHz() const {
        if (mMetadata.contains("extraData") && mMetadata["extraData"].contains("audioSampleRate")) {
            return mMetadata["extraData"]["audioSampleRate"];
        }
        return 0; // Or some default, or throw
    }

    int Decoder::numAudioChannels() const {
        if (mMetadata.contains("extraData") && mMetadata["extraData"].contains("audioChannels")) {
            return mMetadata["extraData"]["audioChannels"];
        }
        return 0; // Or some default, or throw
    }

    void Decoder::loadAudio(std::vector<AudioChunk>& outAudioChunks) {
        for (const auto& o : mAudioOffsets) {
            AudioChunk chunk;

            if (!loadAudioChunk(mMemoryMap, o, chunk))
                continue;

            outAudioChunks.emplace_back(chunk);
        }
    }

    AudioChunkLoader& Decoder::loadAudio() const {
        if (!mAudioLoader) { // Should not happen if init() is successful
            throw MotionCamException("Audio loader not initialized");
        }
        return *mAudioLoader;
    }

    void Decoder::loadFrame(
        const Timestamp           timestamp,
        std::vector<uint8_t>& outData,
        nlohmann::json& outMetadata)
    {
        // 1) Locate the frame in the index
        if (mFrameOffsetMap.find(timestamp) == mFrameOffsetMap.end())
            throw IOException("Frame not found (timestamp: " + std::to_string(timestamp) + ")");
        size_t offset = mFrameOffsetMap.at(timestamp).offset;

        // 2) Read the buffer header
        Item bufferItem{};
        offset += read(offset, &bufferItem, sizeof(Item));
        if (bufferItem.type != Type::BUFFER)
            throw IOException("Invalid buffer type");

        // 3) Load compressed payload
        std::vector<uint8_t> tmpBuffer(bufferItem.size);
        offset += read(offset, tmpBuffer.data(), bufferItem.size);

        // 4) Read the metadata header
        Item metadataItem{};
        offset += read(offset, &metadataItem, sizeof(Item));
        if (metadataItem.type != Type::METADATA)
            throw IOException("Invalid metadata");

        // 5) Parse JSON metadata
        std::vector<uint8_t> metadataJson(metadataItem.size);
        offset += read(offset, metadataJson.data(), metadataItem.size);
        outMetadata = nlohmann::json::parse(
            std::string(metadataJson.begin(), metadataJson.end())
        );

        // 6) Decode into vector<uint8_t>
        int width = outMetadata.value("width", 0);
        int height = outMetadata.value("height", 0);
        int compressionType = outMetadata.value("compressionType", -1);

        if (width <= 0 || height <= 0) throw IOException("Invalid frame dimensions in metadata.");

        outData.resize(sizeof(uint16_t) * static_cast<size_t>(width) * static_cast<size_t>(height));
        uint16_t* pixelPtr = reinterpret_cast<uint16_t*>(outData.data());

        if (compressionType == MOTIONCAM_COMPRESSION_TYPE) {
            if (raw::Decode(pixelPtr, width, height, tmpBuffer.data(), tmpBuffer.size()) <= 0)
                throw IOException("Failed to uncompress frame");
        }
        else if (compressionType == MOTIONCAM_COMPRESSION_TYPE_LEGACY) {
            if (raw::DecodeLegacy(pixelPtr, width, height, tmpBuffer.data(), tmpBuffer.size()) <= 0)
                throw IOException("Failed to uncompress legacy frame");
        }
        else {
            throw IOException("Invalid compression type: " + std::to_string(compressionType));
        }
    }

    void Decoder::loadFrame(
        const Timestamp  timestamp,
        uint16_t* externalOutputBuffer,
        size_t           externalBufferSize,
        nlohmann::json& outMetadata)
    {
        // 1) Locate the frame in the index
        if (mFrameOffsetMap.find(timestamp) == mFrameOffsetMap.end())
            throw IOException("Frame not found (timestamp: " + std::to_string(timestamp) + ")");
        size_t offset = mFrameOffsetMap.at(timestamp).offset;

        // 2) Read the buffer header
        Item bufferItem{};
        offset += read(offset, &bufferItem, sizeof(Item));
        if (bufferItem.type != Type::BUFFER)
            throw IOException("Invalid buffer type");

        // 3) Load compressed payload
        std::vector<uint8_t> tmpBuffer(bufferItem.size);
        offset += read(offset, tmpBuffer.data(), bufferItem.size);

        // 4) Read the metadata header
        Item metadataItem{};
        offset += read(offset, &metadataItem, sizeof(Item));
        if (metadataItem.type != Type::METADATA)
            throw IOException("Invalid metadata");

        // 5) Parse JSON metadata
        std::vector<uint8_t> metadataJson(metadataItem.size);
        offset += read(offset, metadataJson.data(), metadataItem.size);
        outMetadata = nlohmann::json::parse(
            std::string(metadataJson.begin(), metadataJson.end())
        );

        // 6) Validate external buffer size
        int width = outMetadata.value("width", 0);
        int height = outMetadata.value("height", 0);
        if (width <= 0 || height <= 0) throw IOException("Invalid frame dimensions in metadata.");
        size_t needed = sizeof(uint16_t) * static_cast<size_t>(width) * static_cast<size_t>(height);
        if (externalBufferSize < needed)
            throw IOException("Provided buffer too small (need " + std::to_string(needed) + " bytes, got " + std::to_string(externalBufferSize) + ")");

        // 7) Decode directly into externalOutputBuffer
        int compressionType = outMetadata.value("compressionType", -1);
        if (compressionType == MOTIONCAM_COMPRESSION_TYPE) {
            if (raw::Decode(externalOutputBuffer, width, height,
                tmpBuffer.data(), tmpBuffer.size()) <= 0)
                throw IOException("Failed to uncompress frame");
        }
        else if (compressionType == MOTIONCAM_COMPRESSION_TYPE_LEGACY) {
            if (raw::DecodeLegacy(externalOutputBuffer, width, height,
                tmpBuffer.data(), tmpBuffer.size()) <= 0)
                throw IOException("Failed to uncompress legacy frame");
        }
        else {
            throw IOException("Invalid compression type: " + std::to_string(compressionType));
        }
    }

    bool Decoder::getRawFramePayloads(
        Timestamp timestamp,
        std::vector<uint8_t>& outCompressedPayload,
        std::vector<uint8_t>& outMetadataPayload,
        int& outWidth, int& outHeight, int& outCompressionType
    ) {
        // 1) Locate the frame in the index
        auto it_offset = mFrameOffsetMap.find(timestamp);
        if (it_offset == mFrameOffsetMap.end()) {
            // Consider logging or a more specific error if this path is taken often
            return false;
        }
        size_t offset = it_offset->second.offset;

        // 2) Read the buffer header
        Item bufferItem{};
        offset += read(offset, &bufferItem, sizeof(Item));
        if (bufferItem.type != Type::BUFFER) {
            return false; // Or throw
        }

        // 3) Load compressed payload
        outCompressedPayload.resize(bufferItem.size);
        if (read(offset, outCompressedPayload.data(), bufferItem.size) != bufferItem.size) {
            return false; // Or throw
        }
        offset += bufferItem.size;

        // 4) Read the metadata header
        Item metadataItem{};
        offset += read(offset, &metadataItem, sizeof(Item));
        if (metadataItem.type != Type::METADATA) {
            return false; // Or throw
        }

        // 5) Load raw metadata JSON bytes
        outMetadataPayload.resize(metadataItem.size);
        if (read(offset, outMetadataPayload.data(), metadataItem.size) != metadataItem.size) {
            return false; // Or throw
        }

        // 6) Parse essential info from metadata for the caller (optional, but useful)
        try {
            nlohmann::json frameMeta = nlohmann::json::parse(
                std::string(outMetadataPayload.begin(), outMetadataPayload.end())
            );
            outWidth = frameMeta.value("width", 0);
            outHeight = frameMeta.value("height", 0);
            outCompressionType = frameMeta.value("compressionType", -1);

            if (outWidth <= 0 || outHeight <= 0 || outCompressionType == -1) {
                // Essential metadata missing or invalid
                return false;
            }
        }
        catch (const nlohmann::json::parse_error& e) {
            // Failed to parse metadata JSON
            return false;
        }

        return true;
    }


    void Decoder::readIndex() {
        // Seek to index item
        size_t offset = mMemoryMap.size() - static_cast<long>(sizeof(BufferIndex) + sizeof(Item));

        Item bufferIndexItem{};
        offset += read(offset, &bufferIndexItem, sizeof(Item));

        if (bufferIndexItem.type != Type::BUFFER_INDEX)
            throw IOException("Invalid file: Missing buffer index item or wrong type.");

        BufferIndex index{};
        offset += read(offset, &index, sizeof(BufferIndex));

        // Check validity of index
        if (index.magicNumber != INDEX_MAGIC_NUMBER)
            throw IOException("Corrupted file: Index magic number mismatch.");

        if (index.numOffsets < 0) // numOffsets is int32_t, could be negative if corrupt
            throw IOException("Corrupted file: Negative number of offsets in index.");

        mOffsets.resize(static_cast<size_t>(index.numOffsets));

        // Read the index
        if (index.numOffsets > 0) { // Only read if there are offsets
            if (read(index.indexDataOffset, mOffsets.data(), sizeof(BufferOffset), mOffsets.size()) != (sizeof(BufferOffset) * mOffsets.size())) {
                throw IOException("Corrupted file: Failed to read all offset data.");
            }
        }
    }

    void Decoder::reindexOffsets() {
        // Sort offsets so they are in order of timestamps
        if (!mOffsets.empty()) {
            std::sort(mOffsets.begin(), mOffsets.end(), [](const auto& a, const auto& b) {
                return a.timestamp < b.timestamp;
                });
        }

        mFrameList.clear();
        mFrameOffsetMap.clear();

        for (const auto& i : mOffsets) {
            mFrameList.push_back(i.timestamp);
            mFrameOffsetMap.insert({ i.timestamp, i });
        }
    }

    void Decoder::readExtra() {
        if (mOffsets.empty()) { // No video frame offsets implies likely no audio either, or very minimal file
            // Try to find audio index even if no video frames, but it's less likely to exist robustly.
            // For now, let's assume audio index reading depends on at least one video frame existing.
            // This could be made more robust by searching from a known point or end of file if truly needed.
            return;
        }

        // Start searching for AUDIO_INDEX from the offset *after* the last known video frame item.
        // The last item in mOffsets (if sorted by offset, not timestamp) would be more robust.
        // However, the current mOffsets is sorted by timestamp.
        // A safer bet is to scan from near the end of the file, before the BufferIndex.
        // For simplicity, let's stick to the original logic's starting point if it was somewhat working.
        // The original code started after the last *processed* item, which is effectively after the last frame related items.
        // If mOffsets is timestamp sorted, finding the largest offset might be better.

        size_t curOffset = 0;
        if (!mOffsets.empty()) {
            // Find the maximum offset among all buffer items to ensure we start scanning *after* all video content.
            auto max_offset_it = std::max_element(mOffsets.begin(), mOffsets.end(),
                [](const BufferOffset& a, const BufferOffset& b) {
                    return a.offset < b.offset;
                });
            if (max_offset_it != mOffsets.end()) {
                curOffset = max_offset_it->offset;
                // Advance offset past this item and its metadata
                Item bufferItem{}, metadataItem{};
                curOffset += read(curOffset, &bufferItem, sizeof(Item));
                curOffset += bufferItem.size;
                curOffset += read(curOffset, &metadataItem, sizeof(Item));
                curOffset += metadataItem.size;
            }
            else { // Should not happen if mOffsets is not empty
                return;
            }
        }
        else {
            // If no video frames, we can't reliably find where audio starts after video.
            // A more complex scan from end-of-file backwards (before main index) might be needed.
            // For now, if no video frames, assume no audio index either.
            return;
        }


        const size_t fileEndOffset = mMemoryMap.size() - (sizeof(BufferIndex) + sizeof(Item));

        while (curOffset < fileEndOffset) { // Ensure we don't read past where the main index is expected
            Item item{};
            size_t bytes_read_for_item = read(curOffset, &item, sizeof(Item));

            if (bytes_read_for_item != sizeof(Item)) { // Could not read a full Item header
                break;
            }
            curOffset += bytes_read_for_item;

            if (curOffset + item.size > fileEndOffset && item.type != Type::AUDIO_INDEX) { // Check if item content goes beyond file end
                break;
            }

            if (item.type == Type::AUDIO_INDEX) {
                AudioIndex index{};
                if (read(curOffset, &index, sizeof(AudioIndex)) != sizeof(AudioIndex)) break;
                curOffset += sizeof(AudioIndex);

                if (index.numOffsets < 0 || index.numOffsets >(fileEndOffset / sizeof(BufferOffset))) { // Sanity check numOffsets
                    break;
                }

                mAudioOffsets.resize(static_cast<size_t>(index.numOffsets));
                if (index.numOffsets > 0) {
                    if (read(curOffset, mAudioOffsets.data(), sizeof(BufferOffset), mAudioOffsets.size()) != (sizeof(BufferOffset) * mAudioOffsets.size())) {
                        mAudioOffsets.clear(); // Clear if read failed
                        break;
                    }
                    curOffset += sizeof(BufferOffset) * mAudioOffsets.size();
                }
                // Found and processed audio index, we can stop scanning for it.
                // Any further items are likely padding or part of the main BufferIndex itself.
                break;
            }
            else if (item.type == Type::BUFFER || item.type == Type::METADATA || item.type == Type::AUDIO_DATA || item.type == Type::AUDIO_DATA_METADATA) {
                curOffset += item.size;
            }
            else { // Unknown item type, or reached the main BufferIndex item, stop.
                break;
            }
        }
    }

    size_t Decoder::read(size_t offset, void* dst, size_t size, size_t items) const {
        return ::motioncam::read(mMemoryMap, offset, dst, size, items);
    }

} // namespace motioncam
// --- END OF FILE motioncam/Decoder.cpp ---