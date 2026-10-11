//
// IBLCacheTest.cpp - .ibl files, the cache's check, and the BRDF table
//
// Tier 1: files and the CPU only, no GPU.
//

#include <gtest/gtest.h>

#include "IBL/IBLCache.h"
#include "IBL/IBLGenerator.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Shoonyakasha;

namespace {

/// A directory of its own under the system's temporary directory, removed after.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("shoonyakasha_ibl_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

IBLCubeData cube(uint32_t size, uint32_t mips, uint16_t seed) {
    IBLCubeData c;
    c.size = size;
    c.mipLevels = mips;
    c.texels.resize(IBLCubeData::texelCount(size, mips));
    for (size_t i = 0; i < c.texels.size(); ++i) c.texels[i] = static_cast<uint16_t>(seed + i * 7);
    return c;
}

IBLData sample() {
    IBLData data;
    data.irradiance = cube(4, 1, 1);
    data.prefilter = cube(8, 4, 2);
    data.brdfSize = 4;
    data.brdf.resize(4 * 4 * 4);
    for (size_t i = 0; i < data.brdf.size(); ++i) data.brdf[i] = static_cast<float>(i) * 0.25f;
    return data;
}

void writeBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(IBLCache, TexelCountCoversEveryMipsSixFaces) {
    // 4x4, 2x2 and 1x1 faces, six of each, four halves a texel.
    EXPECT_EQ(IBLCubeData::texelCount(4, 3), (16u + 4u + 1u) * 6u * 4u);
    EXPECT_EQ(IBLCubeData::texelCount(1, 1), 6u * 4u);
}

TEST(IBLCache, AFileReadsBackAsItWasWritten) {
    TempDir dir;
    const auto file = dir.path / "sky.ibl";
    IBLGenerationParams params;
    params.prefilterSamples = 77;
    ASSERT_TRUE(writeIBLFile(file, sample(), {0x1234ABCDull, params}));
    EXPECT_FALSE(std::filesystem::exists(dir.path / "sky.ibl.partial"));

    IBLSource source;
    const auto read = readIBLFile(file, &source);
    ASSERT_TRUE(read.has_value());
    const IBLData expected = sample();
    EXPECT_EQ(read->irradiance.size, 4u);
    EXPECT_EQ(read->prefilter.mipLevels, 4u);
    EXPECT_EQ(read->irradiance.texels, expected.irradiance.texels);
    EXPECT_EQ(read->prefilter.texels, expected.prefilter.texels);
    EXPECT_EQ(read->brdfSize, 4u);
    EXPECT_EQ(read->brdf, expected.brdf);
    EXPECT_EQ(source.hash, 0x1234ABCDull);
    EXPECT_EQ(source.params.prefilterSamples, 77u);
    EXPECT_TRUE(iblSourceMatches(source, 0x1234ABCDull, params));
}

TEST(IBLCache, OtherSourcesParametersAndBrokenFilesAreRejected) {
    TempDir dir;
    const auto file = dir.path / "sky.ibl";
    const IBLGenerationParams params;
    ASSERT_TRUE(writeIBLFile(file, sample(), {42, params}));
    IBLSource source;
    ASSERT_TRUE(readIBLFile(file, &source).has_value());

    // Another HDR, or other parameters, make the cache stale.
    EXPECT_FALSE(iblSourceMatches(source, 43, params));
    IBLGenerationParams more = params;
    more.prefilterSize = 256;
    EXPECT_FALSE(iblSourceMatches(source, 42, more));

    const std::string bytes = readBytes(file);
    // Cut short.
    writeBytes(dir.path / "short.ibl", bytes.substr(0, bytes.size() - 1));
    EXPECT_FALSE(readIBLFile(dir.path / "short.ibl").has_value());
    // Another version.
    std::string otherVersion = bytes;
    otherVersion[8] = static_cast<char>(kIBLFileVersion + 1);
    writeBytes(dir.path / "version.ibl", otherVersion);
    EXPECT_FALSE(readIBLFile(dir.path / "version.ibl").has_value());
    // Not an .ibl file at all.
    writeBytes(dir.path / "text.ibl", "#?RADIANCE\nnot a cache");
    EXPECT_FALSE(readIBLFile(dir.path / "text.ibl").has_value());
    EXPECT_FALSE(readIBLFile(dir.path / "missing.ibl").has_value());

    // Data of the wrong length is not written.
    IBLData broken = sample();
    broken.brdf.pop_back();
    EXPECT_FALSE(writeIBLFile(dir.path / "broken.ibl", broken, {42, params}));
}

TEST(IBLCache, TheHashFollowsTheBytesAndTheCacheSitsBesideTheMap) {
    TempDir dir;
    writeBytes(dir.path / "a.hdr", "same bytes");
    writeBytes(dir.path / "b.hdr", "same bytes");
    writeBytes(dir.path / "c.hdr", "other bytes");
    const auto a = hashFile(dir.path / "a.hdr");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a, hashFile(dir.path / "b.hdr"));
    EXPECT_NE(a, hashFile(dir.path / "c.hdr"));
    EXPECT_FALSE(hashFile(dir.path / "missing.hdr").has_value());
    EXPECT_EQ(iblCachePath(dir.path / "env" / "sky.hdr"), dir.path / "env" / "sky.hdr.ibl");
}

TEST(IBLCache, TheBRDFTableIsTheSplitSumIntegral) {
    const std::vector<float> lut = IBLGenerator::computeBRDFLUT(16, 256);
    ASSERT_EQ(lut.size(), 16u * 16u * 4u);
    auto at = [&](uint32_t x, uint32_t y, int channel) { return lut[(y * 16 + x) * 4 + static_cast<uint32_t>(channel)]; };
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            EXPECT_GE(at(x, y, 0), 0.0f);
            EXPECT_GE(at(x, y, 1), 0.0f);
            EXPECT_LE(at(x, y, 0) + at(x, y, 1), 1.0f + 1e-3f);
            EXPECT_FLOAT_EQ(at(x, y, 3), 1.0f);
        }
    }
    // Seen head-on on a smooth surface, almost all light is reflected.
    EXPECT_GT(at(15, 0, 0) + at(15, 0, 1), 0.9f);
    // Every row is worked out the same however the rows are shared out.
    EXPECT_EQ(lut, IBLGenerator::computeBRDFLUT(16, 256));
}
