#ifndef AUDIO_TYPES_H
#define AUDIO_TYPES_H

#include <vector>
#include <cstdint>
#include <utility> // For std::pair

// These types were originally in AudioController.h
// If motioncam::Timestamp is preferred, include <motioncam/Decoder.hpp>
// For now, using the simple typedefs from the original AudioController.h
using Timestamp = int64_t;
using AudioChunk = std::pair<Timestamp, std::vector<int16_t>>;
// If motioncam::AudioChunk is the actual type, this might need adjustment or direct use of motioncam types.
// The provided AudioController.cpp uses motioncam::AudioChunk directly.
// So, these typedefs might be redundant if AudioController.h already includes motioncam headers.
// For consistency with the refactoring goal, we define them here.
// However, if AudioController.cpp uses motioncam::AudioChunk, then AudioController.h should
// probably include <motioncam/Decoder.hpp> and use motioncam::AudioChunk directly.
// Let's assume AudioController.h will manage its own motioncam types for now.

#endif // AUDIO_TYPES_H