//
// IBLCache.cpp - Reading and writing .ibl files
//

#include "IBL/IBLCache.h"

#include <array>
#include <cstring>
#include <fstream>

namespace Shoonyakasha {

namespace {

constexpr char kMagic[8] = {'S', 'H', 'K', 'I', 'B', 'L', '\r', '\n'};

std::array<uint32_t, 7> paramsOf(const IBLGenerationParams& p) {
    return {p.environmentSize, p.irradianceSize, p.prefilterSize, p.brdfLUTSize,
            p.irradianceSamples, p.prefilterSamples, p.brdfSamples};
}

IBLGenerationParams paramsFrom(const std::array<uint32_t, 7>& v) {
    IBLGenerationParams p;
    p.environmentSize = v[0];
    p.irradianceSize = v[1];
    p.prefilterSize = v[2];
    p.brdfLUTSize = v[3];
    p.irradianceSamples = v[4];
    p.prefilterSamples = v[5];
    p.brdfSamples = v[6];
    return p;
}

template <typename T>
void put(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool get(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
void putVector(std::ofstream& out, const std::vector<T>& values) {
    put(out, static_cast<uint64_t>(values.size() * sizeof(T)));
    out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
}

/// Reads a byte count and that many bytes, which must be `expected` values.
template <typename T>
bool getVector(std::ifstream& in, std::vector<T>& values, size_t expected) {
    uint64_t bytes = 0;
    if (!get(in, bytes) || bytes != expected * sizeof(T)) return false;
    values.resize(expected);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(bytes)));
}

void putCube(std::ofstream& out, const IBLCubeData& cube) {
    put(out, cube.size);
    put(out, cube.mipLevels);
    putVector(out, cube.texels);
}

bool getCube(std::ifstream& in, IBLCubeData& cube) {
    if (!get(in, cube.size) || !get(in, cube.mipLevels)) return false;
    // Sizes past these are not maps this engine makes, but a damaged file.
    if (cube.size == 0 || cube.size > 16384 || cube.mipLevels == 0 || cube.mipLevels > 15) return false;
    return getVector(in, cube.texels, IBLCubeData::texelCount(cube.size, cube.mipLevels));
}

} // namespace

size_t IBLCubeData::texelCount(uint32_t size, uint32_t mipLevels) {
    size_t count = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        const size_t s = std::max<size_t>(size >> mip, 1);
        count += s * s * 6 * 4;
    }
    return count;
}

bool IBLData::valid() const {
    auto cubeValid = [](const IBLCubeData& c) {
        return c.size > 0 && c.mipLevels > 0 && c.texels.size() == IBLCubeData::texelCount(c.size, c.mipLevels);
    };
    return cubeValid(irradiance) && cubeValid(prefilter) && brdfSize > 0 &&
           brdf.size() == static_cast<size_t>(brdfSize) * brdfSize * 4;
}

std::optional<uint64_t> hashFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    uint64_t hash = 14695981039346656037ull;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

bool writeIBLFile(const std::filesystem::path& path, const IBLData& data, const IBLSource& source) {
    if (!data.valid()) return false;
    // Written beside and then moved over the file, so a reader never sees
    // half of one.
    std::filesystem::path partial = path;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(kMagic, sizeof(kMagic));
        put(out, kIBLFileVersion);
        put(out, source.hash);
        for (const uint32_t v : paramsOf(source.params)) put(out, v);
        putCube(out, data.irradiance);
        putCube(out, data.prefilter);
        put(out, data.brdfSize);
        putVector(out, data.brdf);
        if (!out) return false;
    }
    std::error_code error;
    std::filesystem::rename(partial, path, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return false;
    }
    return true;
}

std::optional<IBLData> readIBLFile(const std::filesystem::path& path, IBLSource* source) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    char magic[sizeof(kMagic)] = {};
    uint32_t version = 0;
    if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return std::nullopt;
    if (!get(in, version) || version != kIBLFileVersion) return std::nullopt;

    IBLSource read;
    std::array<uint32_t, 7> params{};
    if (!get(in, read.hash)) return std::nullopt;
    for (uint32_t& v : params) {
        if (!get(in, v)) return std::nullopt;
    }
    read.params = paramsFrom(params);

    IBLData data;
    if (!getCube(in, data.irradiance) || !getCube(in, data.prefilter)) return std::nullopt;
    if (!get(in, data.brdfSize) || data.brdfSize == 0 || data.brdfSize > 4096) return std::nullopt;
    if (!getVector(in, data.brdf, static_cast<size_t>(data.brdfSize) * data.brdfSize * 4)) return std::nullopt;
    if (source) *source = read;
    return data;
}

std::filesystem::path iblCachePath(const std::filesystem::path& hdrPath) {
    std::filesystem::path cache = hdrPath;
    cache += ".ibl";
    return cache;
}

bool iblSourceMatches(const IBLSource& source, uint64_t hash, const IBLGenerationParams& params) {
    return source.hash == hash && paramsOf(source.params) == paramsOf(params);
}

} // namespace Shoonyakasha
