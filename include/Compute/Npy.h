//
// Npy.h - read and write NumPy .npy files
//
// An .npy file is a short text header (dtype, memory order, shape) followed
// by the raw array bytes, so numpy.load() and numpy.save() read and write
// the same files. Arrays are stored in C order. Formats 1.0 and 2.0 are
// read; 1.0 is written, or 2.0 when the header needs more than 65535 bytes.
//
//     std::vector<float> lut(64 * 256);
//     Shoonyakasha::Compute::saveNpy("lut.npy", lut, {64, 256});
//     auto back = Shoonyakasha::Compute::loadNpy("lut.npy");   // back.shape == {64, 256}
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Shoonyakasha::Compute {

struct NpyArray {
    std::string dtype;            ///< NumPy type string, such as "<f4" or "|u1"
    std::vector<size_t> shape;    ///< empty for a single value
    std::vector<uint8_t> data;    ///< C order

    /// Number of elements: the product of shape.
    size_t count() const;
    /// Bytes per element, from the digits of dtype.
    size_t itemSize() const;
};

/// The NumPy type string of T, for the arithmetic types the engine uses:
/// float, double, int8..int64, uint8..uint64.
template <typename T> const char* npyDtype();
template <> inline const char* npyDtype<float>() { return "<f4"; }
template <> inline const char* npyDtype<double>() { return "<f8"; }
template <> inline const char* npyDtype<int8_t>() { return "|i1"; }
template <> inline const char* npyDtype<uint8_t>() { return "|u1"; }
template <> inline const char* npyDtype<int16_t>() { return "<i2"; }
template <> inline const char* npyDtype<uint16_t>() { return "<u2"; }
template <> inline const char* npyDtype<int32_t>() { return "<i4"; }
template <> inline const char* npyDtype<uint32_t>() { return "<u4"; }
template <> inline const char* npyDtype<int64_t>() { return "<i8"; }
template <> inline const char* npyDtype<uint64_t>() { return "<u8"; }

/// Writes `bytes` bytes from `data` as an array of `dtype` with `shape`.
/// The file is written beside `path` and renamed over it. Throws
/// std::invalid_argument when `bytes` is not the size shape and dtype give,
/// or when dtype is not a little-endian or single-byte integer, float or
/// bool type; std::runtime_error when the file cannot be written.
void saveNpy(const std::string& path, const void* data, size_t bytes,
             const std::string& dtype, const std::vector<size_t>& shape);

/// saveNpy for a vector; an empty shape means one dimension of values.size().
template <typename T>
void saveNpy(const std::string& path, const std::vector<T>& values, std::vector<size_t> shape = {}) {
    if (shape.empty()) shape = {values.size()};
    saveNpy(path, values.data(), values.size() * sizeof(T), npyDtype<T>(), shape);
}

/// Reads an .npy file. Throws std::runtime_error when the file cannot be
/// read, is not an .npy file, is in Fortran order, or is shorter than its
/// header says.
NpyArray loadNpy(const std::string& path);

/// Parses .npy file contents held in memory, as loadNpy does.
NpyArray parseNpy(const uint8_t* bytes, size_t size);

} // namespace Shoonyakasha::Compute
