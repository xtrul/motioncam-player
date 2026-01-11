#pragma once
#include <vector>
#include <cstdint>

// Alias for a buffer of raw bytes, typically representing pixel data.
using RawBytes = std::vector<uint8_t>;

/**
 * @brief Helper function to reinterpret a RawBytes buffer as a uint16_t pointer.
 * This is unsafe if the buffer size is not a multiple of sizeof(uint16_t) or
 * if the data is not actually 16-bit pixel data.
 * @param v The RawBytes vector.
 * @return Pointer to the beginning of the data, cast as uint16_t*.
 */
inline       uint16_t* asU16(RawBytes& v) { return reinterpret_cast<uint16_t*>(v.data()); }

/**
 * @brief Helper function to reinterpret a const RawBytes buffer as a const uint16_t pointer.
 * This is unsafe if the buffer size is not a multiple of sizeof(uint16_t) or
 * if the data is not actually 16-bit pixel data.
 * @param v The const RawBytes vector.
 * @return Pointer to the beginning of the data, cast as const uint16_t*.
 */
inline const uint16_t* asU16(const RawBytes& v) { return reinterpret_cast<const uint16_t*>(v.data()); }