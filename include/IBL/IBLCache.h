//
// IBLCache.h - IBL maps in host memory, and the .ibl files they are saved in
//
// The maps an environment is drawn with: the irradiance and prefiltered
// cubemaps (RGBA16F, all six faces of each mip level) and the BRDF lookup
// table (RGBA32F). Generating them takes seconds; reading them back takes
// milliseconds. An HDR environment's maps are saved beside it (sky.hdr gets
// sky.hdr.ibl) together with what they were made from: a hash of the HDR
// file's bytes and the generation parameters. A cache whose source, parameters
// or file version differ is made again.
//
// File layout, little-endian: the magic "SHKIBL\r\n", the version, the
// source hash, the seven IBLGenerationParams fields, then the irradiance and
// prefiltered cubemaps (face size, mip count, byte count, texels mip by mip,
// each mip's six faces in order) and the lookup table (size, byte count,
// texels).
//

#pragma once

#include "IBL/IBLGenerator.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Shoonyakasha {

/// A cubemap's texels: RGBA16F, mip by mip from the largest, each mip's six
/// faces in Vulkan's order (+X, -X, +Y, -Y, +Z, -Z), each face row by row.
struct IBLCubeData {
    uint32_t size = 0;            // face size of mip 0
    uint32_t mipLevels = 0;
    std::vector<uint16_t> texels;

    /// The number of halves the texels of `size` and `mipLevels` take.
    static size_t texelCount(uint32_t size, uint32_t mipLevels);
};

struct IBLData {
    IBLCubeData irradiance;
    IBLCubeData prefilter;
    uint32_t brdfSize = 0;
    std::vector<float> brdf;      // RGBA32F, brdfSize * brdfSize * 4

    bool valid() const;
};

/// What an .ibl file was made from.
struct IBLSource {
    uint64_t hash = 0;            // of the HDR file's bytes; 0 for a file baked without one to check
    IBLGenerationParams params{};
};

constexpr uint32_t kIBLFileVersion = 1;

/// FNV-1a of the file's bytes, or nothing when it cannot be read.
std::optional<uint64_t> hashFile(const std::filesystem::path& path);

/// Writes `data` to `path`. False when it cannot be written.
bool writeIBLFile(const std::filesystem::path& path, const IBLData& data, const IBLSource& source);

/// Reads an .ibl file. Nothing when it cannot be read, is not one, is of
/// another version, or is cut short.
std::optional<IBLData> readIBLFile(const std::filesystem::path& path, IBLSource* source = nullptr);

/// The cache file of an HDR environment: beside it, with ".ibl" added.
std::filesystem::path iblCachePath(const std::filesystem::path& hdrPath);

/// Is `source` what these parameters make from an HDR file with this hash?
bool iblSourceMatches(const IBLSource& source, uint64_t hash, const IBLGenerationParams& params);

} // namespace Shoonyakasha
