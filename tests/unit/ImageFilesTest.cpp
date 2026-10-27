//
// ImageFilesTest.cpp - the .exr and .hdr writers
//
// Tier 1: files and the CPU only, no GPU. The .exr file is read back field
// by field as the OpenEXR file layout describes it.
//

#include <gtest/gtest.h>

#include "Compute/ImageFiles.h"

#include <glm/gtc/packing.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Shoonyakasha::Compute;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("shoonyakasha_imagefiles_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

/// A minimal reader for what saveExr writes: header attributes by name,
/// then the offset table and the scanline blocks.
struct Exr {
    std::map<std::string, std::pair<std::string, std::string>> attributes;   // name -> (type, value)
    std::vector<uint64_t> offsets;
    std::string bytes;
    size_t headerEnd = 0;

    explicit Exr(std::string file, uint32_t height) : bytes(std::move(file)) {
        size_t pos = 8;
        while (bytes[pos] != '\0') {
            const std::string name = bytes.c_str() + pos;
            pos += name.size() + 1;
            const std::string type = bytes.c_str() + pos;
            pos += type.size() + 1;
            const uint32_t size = u32(pos);
            pos += 4;
            attributes[name] = {type, bytes.substr(pos, size)};
            pos += size;
        }
        headerEnd = pos + 1;
        for (uint32_t y = 0; y < height; ++y) {
            uint64_t v = 0;
            std::memcpy(&v, bytes.data() + headerEnd + y * 8, 8);
            offsets.push_back(v);
        }
    }
    uint32_t u32(size_t at) const {
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    }
    uint16_t u16(size_t at) const {
        uint16_t v = 0;
        std::memcpy(&v, bytes.data() + at, 2);
        return v;
    }
    float f32(size_t at) const {
        float v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    }
};

/// The channel names of a chlist value, in order, with their pixel types.
std::vector<std::pair<std::string, uint32_t>> channelList(const std::string& chlist) {
    std::vector<std::pair<std::string, uint32_t>> out;
    size_t pos = 0;
    while (chlist[pos] != '\0') {
        const std::string name = chlist.c_str() + pos;
        pos += name.size() + 1;
        uint32_t type = 0;
        std::memcpy(&type, chlist.data() + pos, 4);
        out.emplace_back(name, type);
        pos += 16;
    }
    return out;
}

} // namespace

TEST(ImageFilesTest, ExrHalfRgbaLayout) {
    TempDir dir;
    const uint32_t width = 3, height = 2;
    std::vector<float> texels(width * height * 4);
    for (size_t i = 0; i < texels.size(); ++i) texels[i] = static_cast<float>(i) * 0.25f;
    const auto path = dir.path / "a.exr";
    saveExr(path.string(), texels.data(), width, height, 4);

    const Exr exr(readFile(path), height);
    EXPECT_EQ(exr.bytes.substr(0, 4), std::string("\x76\x2f\x31\x01", 4));
    EXPECT_EQ(exr.u32(4), 2u);
    EXPECT_EQ(exr.attributes.at("compression").second, std::string(1, '\0'));
    const auto channels = channelList(exr.attributes.at("channels").second);
    ASSERT_EQ(channels.size(), 4u);
    EXPECT_EQ(channels[0].first, "A");
    EXPECT_EQ(channels[1].first, "B");
    EXPECT_EQ(channels[2].first, "G");
    EXPECT_EQ(channels[3].first, "R");
    for (const auto& c : channels) EXPECT_EQ(c.second, 1u);   // HALF

    const std::string& window = exr.attributes.at("dataWindow").second;
    ASSERT_EQ(window.size(), 16u);
    int32_t box[4];
    std::memcpy(box, window.data(), 16);
    EXPECT_EQ(box[2], int32_t(width - 1));
    EXPECT_EQ(box[3], int32_t(height - 1));

    const uint32_t rowBytes = width * 4 * 2;
    ASSERT_EQ(exr.offsets.size(), height);
    EXPECT_EQ(exr.offsets[0], exr.headerEnd + height * 8);
    EXPECT_EQ(exr.bytes.size(), exr.offsets[height - 1] + 8 + rowBytes);
    const int order[4] = {3, 2, 1, 0};   // A, B, G, R -> texel channel
    for (uint32_t y = 0; y < height; ++y) {
        const size_t block = exr.offsets[y];
        EXPECT_EQ(exr.u32(block), y);
        EXPECT_EQ(exr.u32(block + 4), rowBytes);
        for (int c = 0; c < 4; ++c) {
            for (uint32_t x = 0; x < width; ++x) {
                const float stored = glm::unpackHalf1x16(exr.u16(block + 8 + (c * width + x) * 2));
                EXPECT_EQ(stored, texels[(y * width + x) * 4 + order[c]]) << "y " << y << " c " << c << " x " << x;
            }
        }
    }
}

TEST(ImageFilesTest, ExrFloatSingleChannelIsY) {
    TempDir dir;
    const std::vector<float> texels = {1.5f, -2.0f, 1e6f, 0.125f};
    const auto path = dir.path / "y.exr";
    saveExr(path.string(), texels.data(), 2, 2, 1, ExrPixelType::Float);
    const Exr exr(readFile(path), 2);
    const auto channels = channelList(exr.attributes.at("channels").second);
    ASSERT_EQ(channels.size(), 1u);
    EXPECT_EQ(channels[0], (std::pair<std::string, uint32_t>{"Y", 2u}));   // FLOAT
    EXPECT_EQ(exr.f32(exr.offsets[1] + 8 + 4), 0.125f);
    EXPECT_EQ(exr.f32(exr.offsets[0] + 8 + 4), -2.0f);
}

TEST(ImageFilesTest, HdrHeaderAndSize) {
    TempDir dir;
    const std::vector<float> texels(4 * 3 * 2, 0.5f);
    const auto path = dir.path / "a.hdr";
    saveHdr(path.string(), texels.data(), 4, 3, 2);
    const std::string file = readFile(path);
    EXPECT_EQ(file.rfind("#?RADIANCE", 0), 0u);
    EXPECT_NE(file.find("-Y 3 +X 4\n"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));
}

TEST(ImageFilesTest, RejectsBadSizes) {
    TempDir dir;
    const std::vector<float> texels(16);
    EXPECT_THROW(saveExr((dir.path / "a.exr").string(), texels.data(), 0, 2, 4), std::invalid_argument);
    EXPECT_THROW(saveExr((dir.path / "a.exr").string(), texels.data(), 2, 2, 5), std::invalid_argument);
    EXPECT_THROW(saveHdr((dir.path / "a.hdr").string(), texels.data(), 2, 2, 0), std::invalid_argument);
}
