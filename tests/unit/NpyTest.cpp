//
// NpyTest.cpp - .npy files written and read
//
// Tier 1: files and the CPU only, no GPU. The expected headers are what
// numpy.save writes for the same arrays.
//

#include <gtest/gtest.h>

#include "Compute/Npy.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Shoonyakasha::Compute;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("shoonyakasha_npy_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
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

/// The header numpy writes: magic, version 1.0, length, then the dictionary
/// padded with spaces so the data starts at a multiple of 64 bytes.
std::string numpyHeader(const std::string& dict) {
    std::string padded = dict;
    while ((10 + padded.size() + 1) % 64 != 0) padded += ' ';
    padded += '\n';
    std::string out("\x93NUMPY\x01\x00", 8);
    out += static_cast<char>(padded.size() & 0xff);
    out += static_cast<char>(padded.size() >> 8);
    return out + padded;
}

} // namespace

TEST(NpyTest, WritesTheHeaderNumpyWrites) {
    TempDir dir;
    struct Case {
        std::string dtype;
        std::vector<size_t> shape;
        size_t bytes;
        std::string dict;
    };
    const std::vector<Case> cases = {
        {"<f4", {2, 3}, 24, "{'descr': '<f4', 'fortran_order': False, 'shape': (2, 3), }"},
        {"<u4", {5}, 20, "{'descr': '<u4', 'fortran_order': False, 'shape': (5,), }"},
        {"<i4", {}, 4, "{'descr': '<i4', 'fortran_order': False, 'shape': (), }"},
    };
    for (const auto& c : cases) {
        const std::vector<uint8_t> data(c.bytes, 0x5a);
        const auto path = dir.path / "a.npy";
        saveNpy(path.string(), data.data(), data.size(), c.dtype, c.shape);
        const std::string file = readFile(path);
        const std::string header = numpyHeader(c.dict);
        ASSERT_EQ(file.size(), header.size() + c.bytes) << c.dict;
        EXPECT_EQ(file.substr(0, header.size()), header) << c.dict;
        EXPECT_EQ(file.substr(header.size()), std::string(c.bytes, '\x5a')) << c.dict;
        EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));
    }
}

TEST(NpyTest, RoundTrip) {
    TempDir dir;
    std::vector<float> values(64 * 4);
    for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<float>(i) * 0.5f;
    const auto path = (dir.path / "lut.npy").string();
    saveNpy(path, values, {64, 4});

    const NpyArray array = loadNpy(path);
    EXPECT_EQ(array.dtype, "<f4");
    EXPECT_EQ(array.shape, (std::vector<size_t>{64, 4}));
    EXPECT_EQ(array.count(), values.size());
    EXPECT_EQ(array.itemSize(), 4u);
    ASSERT_EQ(array.data.size(), values.size() * sizeof(float));
    EXPECT_EQ(std::memcmp(array.data.data(), values.data(), array.data.size()), 0);
}

TEST(NpyTest, VectorWithoutShapeIsOneDimensional) {
    TempDir dir;
    const auto path = (dir.path / "v.npy").string();
    saveNpy(path, std::vector<uint32_t>{1, 2, 3});
    const NpyArray array = loadNpy(path);
    EXPECT_EQ(array.dtype, "<u4");
    EXPECT_EQ(array.shape, (std::vector<size_t>{3}));
}

TEST(NpyTest, RejectsSizeMismatchAndUnsupportedDtypes) {
    TempDir dir;
    const auto path = (dir.path / "bad.npy").string();
    const std::vector<uint8_t> data(12);
    EXPECT_THROW(saveNpy(path, data.data(), data.size(), "<f4", {4}), std::invalid_argument);
    EXPECT_THROW(saveNpy(path, data.data(), data.size(), ">f4", {3}), std::invalid_argument);
    EXPECT_THROW(saveNpy(path, data.data(), data.size(), "|f4", {3}), std::invalid_argument);
}

TEST(NpyTest, ParsesVersionTwoAndRejectsBrokenFiles) {
    // Version 2.0: a four-byte header length.
    std::string dict = "{'descr': '<u2', 'fortran_order': False, 'shape': (2,), }";
    while ((12 + dict.size() + 1) % 64 != 0) dict += ' ';
    dict += '\n';
    std::string file("\x93NUMPY\x02\x00", 8);
    for (int i = 0; i < 4; ++i) file += static_cast<char>((dict.size() >> (8 * i)) & 0xff);
    file += dict;
    file += std::string("\x01\x00\x02\x00", 4);
    const auto* bytes = reinterpret_cast<const uint8_t*>(file.data());
    const NpyArray array = parseNpy(bytes, file.size());
    EXPECT_EQ(array.dtype, "<u2");
    EXPECT_EQ(array.shape, (std::vector<size_t>{2}));
    EXPECT_EQ(array.data, (std::vector<uint8_t>{1, 0, 2, 0}));

    EXPECT_THROW(parseNpy(bytes, file.size() - 1), std::runtime_error);   // data cut short
    const std::string notNpy = "PK\x03\x04 not an npy file";
    EXPECT_THROW(parseNpy(reinterpret_cast<const uint8_t*>(notNpy.data()), notNpy.size()), std::runtime_error);

    std::string fortran = file;
    fortran.replace(fortran.find("False"), 5, "True ");
    EXPECT_THROW(parseNpy(reinterpret_cast<const uint8_t*>(fortran.data()), fortran.size()), std::runtime_error);
}

TEST(NpyTest, MissingFileThrows) {
    EXPECT_THROW(loadNpy("does/not/exist.npy"), std::runtime_error);
}
