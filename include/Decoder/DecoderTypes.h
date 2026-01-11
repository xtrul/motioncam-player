#ifndef DECODER_TYPES_H
#define DECODER_TYPES_H

#include "Utils/RawFrameBuffer.h" // For RawBytes
#include <nlohmann/json.hpp>
#include <motioncam/Decoder.hpp> // For motioncam::Timestamp
#include <atomic>
#include <mutex> 

struct DecodedFrame {
    RawBytes pixelData;
    nlohmann::json metadata;
    motioncam::Timestamp timestamp = 0;

    enum class State { EMPTY, DECODING, READY, FAILED };
    std::atomic<State> state{ State::EMPTY }; // Initialize directly

    // Default constructor
    DecodedFrame() : timestamp(0), state(State::EMPTY) {}

    // Constructor to initialize with a timestamp
    explicit DecodedFrame(motioncam::Timestamp ts) : timestamp(ts), state(State::EMPTY) {}

    // Move constructor
    DecodedFrame(DecodedFrame&& other) noexcept
        : pixelData(std::move(other.pixelData)),
        metadata(std::move(other.metadata)),
        timestamp(other.timestamp),
        state(other.state.load(std::memory_order_acquire)) // Atomically load the state
    {
        other.timestamp = 0;
        other.state.store(State::EMPTY, std::memory_order_release);
    }

    // Move assignment operator
    DecodedFrame& operator=(DecodedFrame&& other) noexcept {
        if (this != &other) {
            pixelData = std::move(other.pixelData);
            metadata = std::move(other.metadata);
            timestamp = other.timestamp;
            state.store(other.state.load(std::memory_order_acquire), std::memory_order_release); // Atomically load and store

            other.timestamp = 0;
            other.state.store(State::EMPTY, std::memory_order_release);
        }
        return *this;
    }

    // Delete copy constructor and copy assignment operator
    DecodedFrame(const DecodedFrame&) = delete;
    DecodedFrame& operator=(const DecodedFrame&) = delete;
};

#endif // DECODER_TYPES_H