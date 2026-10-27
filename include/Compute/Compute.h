//
// Compute.h - run compute shaders on buffers, synchronously
//
// A Context owns a command pool and a fence on a Vulkan device: either a
// device it creates without a window, or the device of a running engine.
// Buffers live in device memory; upload() and download() copy through a
// staging buffer and return once the copy is done. A Kernel is a compute
// shader (.spv) whose descriptor bindings, push constant size and workgroup
// size are read from the SPIR-V. run() records one dispatch, submits it to
// the graphics queue and waits for it.
//
//     Shoonyakasha::Compute::Context ctx;                 // no window
//     auto kernel = ctx.loadKernel("square.comp.spv");
//     auto in  = ctx.upload(values.data(), values.size() * sizeof(float));
//     auto out = ctx.createBuffer(values.size() * sizeof(float));
//     ctx.runThreads(kernel, {in, out}, uint32_t(values.size()));
//     std::vector<float> result = out.download<float>();
//
// Only set 0 is used, and only storage and uniform buffers. A shader that
// declares other sets or other descriptor types is rejected by loadKernel.
//
// shutdown() (also run by ~Context) frees every buffer and kernel the
// context made; using one afterwards throws std::runtime_error. Handles are
// cheap to copy and share one GPU object.
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
struct KernelState;
}

/// One descriptor binding of a kernel, in set 0.
struct KernelBinding {
    enum class Type { StorageBuffer, UniformBuffer };
    uint32_t binding = 0;
    Type type = Type::StorageBuffer;
    bool readOnly = false;     ///< storage buffer declared readonly; uniform buffers are always read-only
    std::string name;          ///< the block's instance name, or its type name when it has none
};

/// What a kernel's SPIR-V declares.
struct KernelInfo {
    std::vector<KernelBinding> bindings;      ///< ascending by binding number
    uint32_t pushConstantSize = 0;            ///< bytes, 0 without push constants
    std::array<uint32_t, 3> localSize{1, 1, 1};
};

/// Reads the bindings, push constant size and workgroup size of a compute
/// shader. Throws std::runtime_error when the module cannot be parsed, is
/// not a compute shader, or declares something other than storage and
/// uniform buffers in set 0.
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
    std::shared_ptr<detail::BufferState> m_state;
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

    /// Frees every buffer and kernel made by this context, then the device
    /// if the context created it. Calling it again does nothing.
    void shutdown();
    bool isShutdown() const;

    /// The device the context runs on.
    VulkanDevice& device() const;

    /// A buffer of `bytes` bytes (at least 1), filled with zeros.
    Buffer createBuffer(size_t bytes);

    /// A buffer of `bytes` bytes holding a copy of `data`.
    Buffer upload(const void* data, size_t bytes);

    /// A kernel from a SPIR-V file. Throws when the file cannot be read or
    /// reflectKernel rejects it.
    Kernel loadKernel(const std::string& spvPath);

    /// Dispatches `groups` workgroups and waits for them to finish.
    /// `bindings` holds one buffer per binding of the kernel, in the order
    /// of KernelInfo::bindings. `push` is copied into the push constants;
    /// it may be shorter than KernelInfo::pushConstantSize, and the rest is
    /// zero. Throws std::invalid_argument when the bindings or push
    /// constants do not match the kernel.
    void run(const Kernel& kernel, const std::vector<Buffer>& bindings,
             std::array<uint32_t, 3> groups,
             const void* push = nullptr, size_t pushBytes = 0);

    /// run() with enough workgroups to cover `threads` invocations in each
    /// dimension: threads divided by the kernel's local size, rounded up.
    /// The shader must ignore invocations past the end itself.
    void runThreads(const Kernel& kernel, const std::vector<Buffer>& bindings,
                    uint32_t threadsX, uint32_t threadsY = 1, uint32_t threadsZ = 1,
                    const void* push = nullptr, size_t pushBytes = 0);

private:
    std::shared_ptr<detail::ContextState> m_state;
};

} // namespace Compute
} // namespace Shoonyakasha
