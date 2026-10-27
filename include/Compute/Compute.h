//
// Compute.h - run compute shaders on buffers and images, synchronously
//
// A Context owns a command pool and a fence on a Vulkan device: either a
// device it creates without a window, or the device of a running engine.
// Buffers and images live in device memory; upload() and download() copy
// through a staging buffer and return once the copy is done. A Kernel is a
// compute shader (.spv) whose descriptor bindings, push constant size and
// workgroup size are read from the SPIR-V. run() records one dispatch,
// submits it to the graphics queue and waits for it.
//
//     Shoonyakasha::Compute::Context ctx;                 // no window
//     auto kernel = ctx.loadKernel("square.comp.spv");
//     auto in  = ctx.upload(values.data(), values.size() * sizeof(float));
//     auto out = ctx.createBuffer(values.size() * sizeof(float));
//     ctx.runThreads(kernel, {in, out}, uint32_t(values.size()));
//     std::vector<float> result = out.download<float>();
//
// Kernels use set 0 only, with storage buffers, uniform buffers, storage
// images and combined image samplers. A shader that declares other sets or
// other descriptor types is rejected by loadKernel.
//
// shutdown() (also run by ~Context) frees every buffer, image and kernel
// the context made; using one afterwards throws std::runtime_error. Handles
// are cheap to copy and share one GPU object.
//
// A context may be used from one thread at a time. A context borrowed from
// a running engine must be used on the thread that runs the engine's
// callbacks, which is the thread that submits the engine's frames.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

class VulkanDevice;

namespace Compute {

namespace detail {
struct ContextState;
struct BufferState;
struct ImageState;
struct KernelState;
}

/// One descriptor binding of a kernel, in set 0.
struct KernelBinding {
    enum class Type { StorageBuffer, UniformBuffer, StorageImage, SampledImage };
    enum class Dim { None, Image2D, Image3D, Cube };

    uint32_t binding = 0;
    Type type = Type::StorageBuffer;
    /// Storage buffer or image declared readonly; uniform buffers and
    /// sampled images are always read-only.
    bool readOnly = false;
    std::string name;          ///< the variable's name, or its type name when it has none
    Dim dim = Dim::None;       ///< images: the shader's image type
    bool arrayed = false;      ///< images: an array type (image2DArray, samplerCubeArray)
    /// Storage images: the format the shader's layout qualifier names, as a
    /// pipeline JSON format name ("R32G32B32A32_SFLOAT"); empty when it
    /// names none.
    std::string format;
};

/// What a kernel's SPIR-V declares.
struct KernelInfo {
    std::vector<KernelBinding> bindings;      ///< ascending by binding number
    uint32_t pushConstantSize = 0;            ///< bytes, 0 without push constants
    std::array<uint32_t, 3> localSize{1, 1, 1};
};

/// Reads the bindings, push constant size and workgroup size of a compute
/// shader. Throws std::runtime_error when the module cannot be parsed, is
/// not a compute shader, or declares a set other than 0, an array of
/// descriptors, or a descriptor type other than those listed in
/// KernelBinding::Type.
KernelInfo reflectKernel(const void* spirv, size_t sizeBytes);

/// A buffer in device memory, usable as a storage or uniform buffer.
/// Default-constructed, it holds nothing (valid() is false).
class Buffer {
public:
    Buffer() = default;

    bool valid() const;
    size_t size() const;

    /// Copies `bytes` from `data` into the buffer at `offset`.
    void upload(const void* data, size_t bytes, size_t offset = 0);

    /// Copies `bytes` from the buffer at `offset` into `out`.
    void download(void* out, size_t bytes, size_t offset = 0) const;

    /// The whole buffer as Ts; size() must be a multiple of sizeof(T).
    template <typename T>
    std::vector<T> download() const {
        std::vector<T> out(size() / sizeof(T));
        download(out.data(), out.size() * sizeof(T));
        return out;
    }

private:
    friend class Context;
    friend class Binding;
    std::shared_ptr<detail::BufferState> m_state;
};

/// An image in device memory with one mip level: 2D (depth 1, layers 1),
/// a 2D array (layers > 1), or 3D (depth > 1). An array of 6 x n square
/// layers can also be bound as a cube or cube array. Usable as a storage
/// image and as a sampled image, as far as its format supports each on the
/// device. Default-constructed, it holds nothing (valid() is false).
///
/// Its texels are copied tightly packed: rows of `width` texels, `height`
/// rows to a slice, slices (3D) or layers (arrays) one after another.
class Image {
public:
    Image() = default;

    bool valid() const;
    uint32_t width() const;
    uint32_t height() const;
    uint32_t depth() const;
    uint32_t layers() const;
    /// The pipeline JSON name of its format, such as "R32G32B32A32_SFLOAT".
    const std::string& format() const;
    size_t texelSize() const;
    /// width x height x depth x layers x texelSize.
    size_t byteSize() const;

    /// Copies byteSize() bytes from `data` into the whole image.
    void upload(const void* data, size_t bytes);

    /// Copies the whole image into `out`, which holds byteSize() bytes.
    void download(void* out, size_t bytes) const;

    /// The whole image as Ts; byteSize() must be a multiple of sizeof(T).
    template <typename T>
    std::vector<T> download() const {
        std::vector<T> out(byteSize() / sizeof(T));
        download(out.data(), out.size() * sizeof(T));
        return out;
    }

private:
    friend class Context;
    friend class Binding;
    std::shared_ptr<detail::ImageState> m_state;
};

/// A buffer or an image given to one binding of a kernel.
class Binding {
public:
    Binding() = default;   ///< empty; run() rejects it
    Binding(const Buffer& buffer) : m_buffer(buffer.m_state) {}
    Binding(const Image& image) : m_image(image.m_state) {}

private:
    friend class Context;
    std::shared_ptr<detail::BufferState> m_buffer;
    std::shared_ptr<detail::ImageState> m_image;
};

/// A compute pipeline made from one shader.
class Kernel {
public:
    Kernel() = default;

    bool valid() const;
    const KernelInfo& info() const;

private:
    friend class Context;
    std::shared_ptr<detail::KernelState> m_state;
};

class Context {
public:
    /// Creates its own Vulkan instance and a device without a window or
    /// swapchain.
    explicit Context(bool enableValidation = false);

    /// Uses `device`, which must outlive the context or shutdown().
    explicit Context(VulkanDevice& device);

    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    /// Frees every buffer, image and kernel made by this context, then the
    /// device if the context created it. Calling it again does nothing.
    void shutdown();
    bool isShutdown() const;

    /// The device the context runs on.
    VulkanDevice& device() const;

    /// A buffer of `bytes` bytes (at least 1), filled with zeros.
    Buffer createBuffer(size_t bytes);

    /// A buffer of `bytes` bytes holding a copy of `data`.
    Buffer upload(const void* data, size_t bytes);

    /// An image filled with zeros. `format` is a pipeline JSON format name
    /// with a known texel size. Throws std::invalid_argument for a zero
    /// size, depth and layers both above 1, or a format that is unknown or
    /// that the device can neither store to nor sample in compute shaders.
    Image createImage(const std::string& format, uint32_t width, uint32_t height,
                      uint32_t depth = 1, uint32_t layers = 1);

    /// createImage, then Image::upload(data, bytes).
    Image uploadImage(const std::string& format, uint32_t width, uint32_t height,
                      const void* data, size_t bytes, uint32_t depth = 1, uint32_t layers = 1);

    /// A kernel from a SPIR-V file. Throws when the file cannot be read or
    /// reflectKernel rejects it.
    Kernel loadKernel(const std::string& spvPath);

    /// Dispatches `groups` workgroups and waits for them to finish.
    /// `bindings` holds one buffer or image per binding of the kernel, in
    /// the order of KernelInfo::bindings: a buffer for a buffer binding, an
    /// image for an image binding. An image is bound as the shader's image
    /// type declares: 2D needs depth 1 and layers 1; 2D array, depth 1; 3D,
    /// depth > 1; cube, 6 square layers; cube array, 6 x n square layers. A
    /// sampled image is read with linear filtering where its format allows
    /// it (nearest otherwise) and coordinates clamped to the edge. `push` is
    /// copied into the push constants; it may be shorter than
    /// KernelInfo::pushConstantSize, and the rest is zero. Throws
    /// std::invalid_argument when the bindings or push constants do not
    /// match the kernel.
    void run(const Kernel& kernel, const std::vector<Binding>& bindings,
             std::array<uint32_t, 3> groups,
             const void* push = nullptr, size_t pushBytes = 0);

    /// run() with enough workgroups to cover `threads` invocations in each
    /// dimension: threads divided by the kernel's local size, rounded up.
    /// The shader must ignore invocations past the end itself.
    void runThreads(const Kernel& kernel, const std::vector<Binding>& bindings,
                    uint32_t threadsX, uint32_t threadsY = 1, uint32_t threadsZ = 1,
                    const void* push = nullptr, size_t pushBytes = 0);

private:
    std::shared_ptr<detail::ContextState> m_state;
};

} // namespace Compute
} // namespace Shoonyakasha
