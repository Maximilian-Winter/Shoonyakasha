//
// ResourceManager.h - Asset and resource management system
//
// 黃帝司中 — The Yellow Emperor governs the center
// Harmonizing all resources, coordinating their flow
//

#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <future>
#include <filesystem>
#include <typeindex>
#include <any>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>

namespace Shoonyakasha {

// Forward declarations
class VulkanDevice;

// ═══════════════════════════════════════════════════════════════
// Resource Base Types
// ═══════════════════════════════════════════════════════════════

enum class ResourceType {
    Unknown,
    Buffer,
    Image,
    Texture,
    Model,
    Shader,
    Material,
    Sound,
    Animation,
    Scene
};

enum class ResourceState {
    Unloaded,
    Loading,
    Loaded,
    Failed,
    Unloading
};

// Resource Handle — A lightweight reference to any resource
struct ResourceHandle {
    std::string name;
    ResourceType type;
    size_t id;

    ResourceHandle() : type(ResourceType::Unknown), id(0) {}
    ResourceHandle(const std::string& n, ResourceType t, size_t i)
        : name(n), type(t), id(i) {}

    bool isValid() const { return id != 0 && type != ResourceType::Unknown; }

    bool operator==(const ResourceHandle& other) const {
        return id == other.id && type == other.type;
    }
};

} // namespace Shoonyakasha (temporarily close for std::hash)

// Hash support for ResourceHandle
namespace std {
    template<>
    struct hash<Shoonyakasha::ResourceHandle> {
        size_t operator()(const Shoonyakasha::ResourceHandle& handle) const {
            return hash<size_t>{}(handle.id) ^
                   (hash<int>{}(static_cast<int>(handle.type)) << 1);
        }
    };
}

namespace Shoonyakasha { // reopen

// ═══════════════════════════════════════════════════════════════
// Resource Descriptors — Describing how resources should be loaded
// ═══════════════════════════════════════════════════════════════

struct ResourceDescriptor {
    std::string name;
    ResourceType type;
    std::filesystem::path path;
    std::unordered_map<std::string, std::any> parameters;

    ResourceDescriptor() : type(ResourceType::Unknown) {}

    ResourceDescriptor(const std::string& n, ResourceType t, const std::filesystem::path& p)
        : name(n), type(t), path(p) {}

    // Fluent parameter setting
    template<typename T>
    ResourceDescriptor& setParameter(const std::string& key, const T& value) {
        parameters[key] = value;
        return *this;
    }

    template<typename T>
    T getParameter(const std::string& key, const T& defaultValue = T{}) const {
        auto it = parameters.find(key);
        if (it != parameters.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return defaultValue;
            }
        }
        return defaultValue;
    }
};

// ═══════════════════════════════════════════════════════════════
// Resource Loaders — Pluggable interface for asset loading
// ═══════════════════════════════════════════════════════════════

class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;
    virtual ResourceType getResourceType() const = 0;
    virtual std::vector<std::string> getSupportedExtensions() const = 0;
    virtual std::shared_ptr<void> load(const ResourceDescriptor& desc, VulkanDevice& device) = 0;
    virtual void unload(std::shared_ptr<void> resource, VulkanDevice& device) = 0;
    virtual size_t getMemoryUsage(std::shared_ptr<void> resource) const = 0;
};

// ═══════════════════════════════════════════════════════════════
// Resource Cache — LRU eviction with memory budget tracking
// ═══════════════════════════════════════════════════════════════

struct ResourceEntry {
    ResourceHandle handle;
    ResourceDescriptor descriptor;
    std::shared_ptr<void> data;
    ResourceState state;
    size_t memoryUsage;
    std::chrono::steady_clock::time_point lastAccessed;
    uint32_t referenceCount;

    ResourceEntry(const ResourceHandle& h, const ResourceDescriptor& desc)
        : handle(h), descriptor(desc), state(ResourceState::Unloaded)
        , memoryUsage(0), lastAccessed(std::chrono::steady_clock::now())
        , referenceCount(0) {}
};

class ResourceCache {
public:
    explicit ResourceCache(size_t maxMemoryBytes = 512 * 1024 * 1024);
    ~ResourceCache();

    // Cache management
    void setMaxMemory(size_t bytes);
    size_t getMaxMemory() const;
    size_t getCurrentMemory() const;

    // Resource operations
    void store(const ResourceHandle& handle, const ResourceDescriptor& desc,
               std::shared_ptr<void> data, size_t memoryUsage);
    std::shared_ptr<void> retrieve(const ResourceHandle& handle);
    bool contains(const ResourceHandle& handle) const;
    void remove(const ResourceHandle& handle);

    // Memory management
    void evictLeastRecentlyUsed();
    void evictUnreferenced();
    void clear();

    // Statistics
    size_t getResourceCount() const;
    std::vector<ResourceHandle> getLoadedResources() const;

    // Access descriptor for a cached resource (needed for hotReload)
    const ResourceDescriptor* getDescriptor(const ResourceHandle& handle) const;

private:
    std::unordered_map<ResourceHandle, std::unique_ptr<ResourceEntry>> m_resources;
    size_t m_maxMemoryBytes;
    size_t m_currentMemoryUsage;
    mutable std::mutex m_mutex;

    // Internal unlocked variants — called when lock is already held
    bool containsUnlocked(const ResourceHandle& handle) const;
    void removeUnlocked(const ResourceHandle& handle);
    void evictResource(const ResourceHandle& handle);
    std::vector<ResourceHandle> findEvictionCandidates() const;
};

// ═══════════════════════════════════════════════════════════════
// Async Resource Loading — Thread pool for non-blocking loads
// ═══════════════════════════════════════════════════════════════

class AsyncLoader {
public:
    explicit AsyncLoader(size_t numThreads = 4);
    ~AsyncLoader();

    // Submit a task for async execution
    void submitTask(std::function<void()> task);

    // Progress tracking
    void update();
    size_t getPendingTaskCount() const;

    // Control
    void waitForAll();
    void cancelAll();

private:
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_taskQueue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_shutdown;

    void workerThread();
};

// ═══════════════════════════════════════════════════════════════
// Resource Manager — The conductor of the asset orchestra
// ═══════════════════════════════════════════════════════════════

class ResourceManager {
public:
    explicit ResourceManager(VulkanDevice& device, size_t cacheSize = 512 * 1024 * 1024);
    ~ResourceManager();

    // ─── Loader Registration ────────────────────────────────────
    template<typename LoaderType>
    void registerLoader() {
        auto loader = std::make_shared<LoaderType>();
        auto type = loader->getResourceType();
        auto extensions = loader->getSupportedExtensions();

        m_typeToLoader[type] = loader;
        for (const auto& ext : extensions) {
            m_extensionToType[ext] = type;
            m_extensionToLoader[ext] = loader;
        }
    }

    // ─── Synchronous Loading ────────────────────────────────────
    ResourceHandle load(const std::string& name, const std::filesystem::path& path);
    ResourceHandle load(const ResourceDescriptor& desc);

    // ─── Asynchronous Loading ───────────────────────────────────
    std::future<ResourceHandle> loadAsync(
        const std::string& name,
        const std::filesystem::path& path,
        std::function<void(ResourceHandle, bool)> callback = nullptr);

    std::future<ResourceHandle> loadAsync(
        const ResourceDescriptor& desc,
        std::function<void(ResourceHandle, bool)> callback = nullptr);

    // ─── Resource Access ────────────────────────────────────────
    template<typename T>
    std::shared_ptr<T> getResource(const ResourceHandle& handle) {
        auto data = m_cache.retrieve(handle);
        if (data) {
            return std::static_pointer_cast<T>(data);
        }
        return nullptr;
    }

    template<typename T>
    std::shared_ptr<T> getResource(const std::string& name) {
        auto handle = findHandle(name);
        return getResource<T>(handle);
    }

    // ─── Direct Registration (for manually created resources) ───
    template<typename T>
    ResourceHandle registerResource(const std::string& name, ResourceType type,
                                     std::shared_ptr<T> resource, size_t memoryUsage = 0) {
        auto handle = generateHandle(name, type);
        ResourceDescriptor desc(name, type, name);
        m_cache.store(handle, desc, std::static_pointer_cast<void>(resource), memoryUsage);
        return handle;
    }

    // ─── Resource Management ────────────────────────────────────
    void unload(const ResourceHandle& handle);
    void unload(const std::string& name);
    void preload(const std::vector<ResourceDescriptor>& resources);
    void hotReload(const ResourceHandle& handle);

    // ─── Lookup ─────────────────────────────────────────────────
    ResourceHandle findHandle(const std::string& name) const;
    bool isLoaded(const ResourceHandle& handle) const;
    bool isLoaded(const std::string& name) const;

    // ─── Memory Management ──────────────────────────────────────
    void garbageCollect();
    void setMemoryBudget(size_t bytes);
    size_t getMemoryUsage() const;

    // ─── Statistics ─────────────────────────────────────────────
    void printStatistics() const;
    std::vector<ResourceHandle> getLoadedResources() const;

    // ─── Per-Frame Update ───────────────────────────────────────
    void update();

private:
    VulkanDevice& m_device;
    ResourceCache m_cache;
    AsyncLoader m_asyncLoader;

    std::unordered_map<std::string, std::shared_ptr<IResourceLoader>> m_extensionToLoader;
    std::unordered_map<ResourceType, std::shared_ptr<IResourceLoader>> m_typeToLoader;
    std::unordered_map<std::string, ResourceType> m_extensionToType;
    std::unordered_map<std::string, ResourceHandle> m_nameToHandle;
    std::unordered_map<ResourceHandle, std::string> m_handleToName;

    size_t m_nextId = 1;
    mutable std::mutex m_mutex;

    // Helpers
    ResourceDescriptor createDescriptor(const std::string& name, const std::filesystem::path& path);
    ResourceType deduceType(const std::filesystem::path& path) const;
    ResourceHandle generateHandle(const std::string& name, ResourceType type);
    ResourceHandle loadResource(const ResourceDescriptor& desc);
    std::future<ResourceHandle> loadResourceAsync(
        const ResourceDescriptor& desc,
        std::function<void(ResourceHandle, bool)> callback);
};

} // namespace Shoonyakasha

// Backward compatibility — global-scope aliases
using Shoonyakasha::ResourceType;
using Shoonyakasha::ResourceState;
using Shoonyakasha::ResourceHandle;
using Shoonyakasha::ResourceDescriptor;
using Shoonyakasha::IResourceLoader;
using Shoonyakasha::ResourceEntry;
using Shoonyakasha::ResourceCache;
using Shoonyakasha::AsyncLoader;
using Shoonyakasha::ResourceManager;
