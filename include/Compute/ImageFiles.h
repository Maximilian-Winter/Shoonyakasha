//
// ImageFiles.h - write float images as Radiance .hdr and OpenEXR .exr files
//
// Texels are rows of `channels` floats, top row first. .hdr files store
// RGB in 8-bit mantissas with a shared exponent (RGBE), so they keep the
// range of HDR values but not their full precision; 1 channel is written
// as grey, 2 as red and green with blue zero, and alpha is dropped. .exr
// files store each channel as a 16-bit (Half) or 32-bit (Float) float,
// uncompressed, in scanlines; 1 channel is written as Y, 2 as R and G,
// 3 as R, G and B, 4 as R, G, B and A.
//
// Both writers write beside `path` and rename the file over it, and throw
// std::invalid_argument for a zero size or a channel count outside 1..4,
// std::runtime_error when the file cannot be written.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Shoonyakasha::Compute {

class Image;

enum class ExrPixelType { Half, Float };

void saveHdr(const std::string& path, const float* texels, uint32_t width, uint32_t height, uint32_t channels);

void saveExr(const std::string& path, const float* texels, uint32_t width, uint32_t height, uint32_t channels,
             ExrPixelType type = ExrPixelType::Half);

/// One layer (of an array) or slice (of a 3D image) at mip level `mip`, as
/// rows of floats: float formats as they are, UNORM formats divided to
/// 0..1, sRGB formats decoded to linear (alpha as it is), B8G8R8A8
/// reordered to RGBA, and the packed B10G11R11 and A2B10G10R10 formats
/// unpacked. `channels` is set to the floats per texel. Throws
/// std::invalid_argument for integer formats, or a layer or mip the image
/// does not have.
std::vector<float> imageTexels(const Image& image, uint32_t mip, uint32_t layer, uint32_t& channels);

/// Writes one layer or slice of `image` at mip level `mip` (imageTexels)
/// to `path`: .hdr, .exr (with `type`), or .npy (float32, shaped height x
/// width x channels). Throws std::invalid_argument for any other extension.
void saveImage(const std::string& path, const Image& image, uint32_t mip = 0, uint32_t layer = 0,
               ExrPixelType type = ExrPixelType::Half);

} // namespace Shoonyakasha::Compute
