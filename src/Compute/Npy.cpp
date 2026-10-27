//
// Npy.cpp - NumPy .npy files (include/Compute/Npy.h)
//

#include "Compute/Npy.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Shoonyakasha::Compute {

namespace {

constexpr char kMagic[] = "\x93NUMPY";
constexpr size_t kMagicSize = 6;

size_t dtypeSize(const std::string& dtype) {
    if (dtype.size() < 3) return 0;
    const char order = dtype[0];
    const char kind = dtype[1];
    if (order != '<' && order != '|') return 0;
    if (kind != 'f' && kind != 'i' && kind != 'u' && kind != 'b') return 0;
    size_t size = 0;
    for (size_t i = 2; i < dtype.size(); ++i) {
        if (dtype[i] < '0' || dtype[i] > '9') return 0;
        size = size * 10 + static_cast<size_t>(dtype[i] - '0');
    }
    if (order == '|' && size != 1) return 0;
    return size;
}

size_t product(const std::vector<size_t>& shape) {
    size_t n = 1;
    for (size_t d : shape) n *= d;
    return n;
}

/// The text after `key` in the header dictionary, with spaces skipped.
size_t valueStart(const std::string& header, const std::string& key) {
    const size_t at = header.find("'" + key + "'");
    if (at == std::string::npos) throw std::runtime_error("npy: the header has no '" + key + "'");
    size_t pos = header.find(':', at);
    if (pos == std::string::npos) throw std::runtime_error("npy: malformed header");
    ++pos;
    while (pos < header.size() && header[pos] == ' ') ++pos;
    return pos;
}

} // namespace

size_t NpyArray::count() const {
    return product(shape);
}

size_t NpyArray::itemSize() const {
    return dtypeSize(dtype);
}

void saveNpy(const std::string& path, const void* data, size_t bytes, const std::string& dtype,
             const std::vector<size_t>& shape) {
    const size_t item = dtypeSize(dtype);
    if (item == 0) {
        throw std::invalid_argument("npy: unsupported dtype '" + dtype + "'");
    }
    if (product(shape) * item != bytes) {
        throw std::invalid_argument("npy: " + std::to_string(bytes) + " bytes do not make an array of " +
                                    std::to_string(product(shape)) + " '" + dtype + "' values");
    }

    std::string shapeText = "(";
    for (size_t i = 0; i < shape.size(); ++i) {
        shapeText += std::to_string(shape[i]);
        shapeText += (shape.size() == 1 || i + 1 < shape.size()) ? "," : "";
        if (i + 1 < shape.size()) shapeText += " ";
    }
    shapeText += ")";
    std::string header = "{'descr': '" + dtype + "', 'fortran_order': False, 'shape': " + shapeText + ", }";

    // Magic, version, header length, header padded with spaces to a
    // multiple of 64 bytes and ended by a newline.
    const bool v2 = header.size() + 1 + kMagicSize + 4 > 65535;
    const size_t prefix = kMagicSize + 2 + (v2 ? 4 : 2);
    const size_t total = (prefix + header.size() + 1 + 63) / 64 * 64;
    header.append(total - prefix - header.size() - 1, ' ');
    header += '\n';

    std::string head(kMagic, kMagicSize);
    head += static_cast<char>(v2 ? 2 : 1);
    head += static_cast<char>(0);
    const uint32_t length = static_cast<uint32_t>(header.size());
    head += static_cast<char>(length & 0xff);
    head += static_cast<char>((length >> 8) & 0xff);
    if (v2) {
        head += static_cast<char>((length >> 16) & 0xff);
        head += static_cast<char>((length >> 24) & 0xff);
    }

    const std::string temp = path + ".partial";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("npy: cannot write '" + temp + "'");
        file.write(head.data(), static_cast<std::streamsize>(head.size()));
        file.write(header.data(), static_cast<std::streamsize>(header.size()));
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
        if (!file) throw std::runtime_error("npy: writing '" + temp + "' failed");
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("npy: cannot replace '" + path + "'");
    }
}

NpyArray parseNpy(const uint8_t* bytes, size_t size) {
    if (size < kMagicSize + 4 || std::memcmp(bytes, kMagic, kMagicSize) != 0) {
        throw std::runtime_error("npy: not an .npy file");
    }
    const uint8_t major = bytes[kMagicSize];
    size_t headerLength = 0;
    size_t prefix = 0;
    if (major == 1) {
        headerLength = bytes[8] | (size_t(bytes[9]) << 8);
        prefix = 10;
    } else if (major == 2 || major == 3) {
        if (size < 12) throw std::runtime_error("npy: truncated header");
        headerLength = bytes[8] | (size_t(bytes[9]) << 8) | (size_t(bytes[10]) << 16) | (size_t(bytes[11]) << 24);
        prefix = 12;
    } else {
        throw std::runtime_error("npy: unsupported format version " + std::to_string(major));
    }
    if (prefix + headerLength > size) throw std::runtime_error("npy: truncated header");
    const std::string header(reinterpret_cast<const char*>(bytes + prefix), headerLength);

    NpyArray out;
    size_t pos = valueStart(header, "descr");
    if (pos >= header.size() || (header[pos] != '\'' && header[pos] != '"')) {
        throw std::runtime_error("npy: unsupported descr (structured dtypes are not read)");
    }
    const size_t close = header.find(header[pos], pos + 1);
    if (close == std::string::npos) throw std::runtime_error("npy: malformed descr");
    out.dtype = header.substr(pos + 1, close - pos - 1);
    if (out.dtype.size() == 3 && out.dtype[0] == '=') out.dtype[0] = '<';   // native order on little-endian
    if (dtypeSize(out.dtype) == 0) throw std::runtime_error("npy: unsupported dtype '" + out.dtype + "'");

    pos = valueStart(header, "fortran_order");
    if (header.compare(pos, 4, "True") == 0) {
        throw std::runtime_error("npy: arrays in Fortran order are not read");
    }

    pos = valueStart(header, "shape");
    if (pos >= header.size() || header[pos] != '(') throw std::runtime_error("npy: malformed shape");
    const size_t end = header.find(')', pos);
    if (end == std::string::npos) throw std::runtime_error("npy: malformed shape");
    size_t value = 0;
    bool inNumber = false;
    for (size_t i = pos + 1; i < end; ++i) {
        const char c = header[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + static_cast<size_t>(c - '0');
            inNumber = true;
        } else if (c == ',' || c == ' ' || c == 'L') {
            if (inNumber && c != 'L') {
                out.shape.push_back(value);
                value = 0;
                inNumber = false;
            }
        } else {
            throw std::runtime_error("npy: malformed shape");
        }
    }
    if (inNumber) out.shape.push_back(value);

    const size_t dataBytes = out.count() * out.itemSize();
    const size_t offset = prefix + headerLength;
    if (size - offset < dataBytes) {
        throw std::runtime_error("npy: the file holds " + std::to_string(size - offset) + " data bytes, the header needs " +
                                 std::to_string(dataBytes));
    }
    out.data.assign(bytes + offset, bytes + offset + dataBytes);
    return out;
}

NpyArray loadNpy(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("npy: cannot open '" + path + "'");
    std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("npy: reading '" + path + "' failed");
    try {
        return parseNpy(bytes.data(), bytes.size());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(e.what()) + " (" + path + ")");
    }
}

} // namespace Shoonyakasha::Compute
