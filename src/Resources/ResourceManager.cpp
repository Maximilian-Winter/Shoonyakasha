//
// ResourceManager.cpp - Asset and resource management implementation
//
// 黃帝之德合其萬方
// The Yellow Emperor's virtue unifies all directions
//

#include "Resources/ResourceManager.h"
#include "Vulkan/VulkanDevice.h"
#include <iostream>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════
// Windows File Handle Limit Fix
// 玄武固根基 — The Black Tortoise secures the foundation
// Default Windows CRT limit is 512 file handles, which is too low
// for loading large glTF scenes with hundreds of textures.
// ═══════════════════════════════════════════════════════════════
#ifdef _WIN32
#include <cstdio>  // For _setmaxstdio

// Static initializer to increase file handle limit before main()
static struct WindowsFileHandleFix {
    WindowsFileHandleFix() {
        // Increase to 8192 handles (Windows max is 8192 for CRT)
        int result = _setmaxstdio(8192);
        if (result == -1) {
            // If 8192 fails, try 2048 which should always work
            result = _setmaxstdio(2048);
        }
        if (result > 0) {
            std::cout << "[ResourceManager] Windows file handle limit increased to " << result << std::endl;
        }
    }
} s_windowsFileHandleFix;
#endif

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// ResourceCache Implementation
// ═══════════════════════════════════════════════════════════════

ResourceCache::ResourceCache(size_t maxMemoryBytes)
    : m_maxMemoryBytes(maxMemoryBytes), m_currentMemoryUsage(0) {
}

ResourceCache::~ResourceCache() {
    clear();
}

void ResourceCache::setMaxMemory(size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxMemoryBytes = bytes;
}

size_t ResourceCache::getMaxMemory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxMemoryBytes;
}

size_t ResourceCache::getCurrentMemory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentMemoryUsage;
}

void ResourceCache::store(const ResourceHandle& handle, const ResourceDescriptor& desc,
                           std::shared_ptr<void> data, size_t memoryUsage) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Replace if already exists
    if (containsUnlocked(handle)) {
        removeUnlocked(handle);
    }

    // Evict until we have room (if memory budget is set)
    while (m_maxMemoryBytes > 0 &&
           m_currentMemoryUsage + memoryUsage > m_maxMemoryBytes &&
           !m_resources.empty()) {
        auto candidates = findEvictionCandidates();
        if (candidates.empty()) break;
        evictResource(candidates.front());
    }

    auto entry = std::make_unique<ResourceEntry>(handle, desc);
    entry->data = std::move(data);
    entry->state = ResourceState::Loaded;
    entry->memoryUsage = memoryUsage;
    entry->lastAccessed = std::chrono::steady_clock::now();

    m_currentMemoryUsage += memoryUsage;
    m_resources[handle] = std::move(entry);
}

std::shared_ptr<void> ResourceCache::retrieve(const ResourceHandle& handle) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_resources.find(handle);
    if (it != m_resources.end() && it->second->state == ResourceState::Loaded) {
        it->second->lastAccessed = std::chrono::steady_clock::now();
        it->second->referenceCount++;
        return it->second->data;
    }
    return nullptr;
}

bool ResourceCache::contains(const ResourceHandle& handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return containsUnlocked(handle);
}

void ResourceCache::remove(const ResourceHandle& handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    removeUnlocked(handle);
}

void ResourceCache::evictLeastRecentlyUsed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto candidates = findEvictionCandidates();
    if (!candidates.empty()) {
        evictResource(candidates.front());
    }
}

void ResourceCache::evictUnreferenced() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ResourceHandle> toEvict;
    for (const auto& [handle, entry] : m_resources) {
        if (entry->referenceCount == 0) {
            toEvict.push_back(handle);
        }
    }
    for (const auto& handle : toEvict) {
        evictResource(handle);
    }
}

void ResourceCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_resources.clear();
    m_currentMemoryUsage = 0;
}

size_t ResourceCache::getResourceCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_resources.size();
}

std::vector<ResourceHandle> ResourceCache::getLoadedResources() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ResourceHandle> handles;
    handles.reserve(m_resources.size());
    for (const auto& [handle, entry] : m_resources) {
        if (entry->state == ResourceState::Loaded) {
            handles.push_back(handle);
        }
    }
    return handles;
}

const ResourceDescriptor* ResourceCache::getDescriptor(const ResourceHandle& handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_resources.find(handle);
    if (it != m_resources.end()) {
        return &it->second->descriptor;
    }
    return nullptr;
}

// ── Private (called under lock) ─────────────────────────────

bool ResourceCache::containsUnlocked(const ResourceHandle& handle) const {
    return m_resources.find(handle) != m_resources.end();
}

void ResourceCache::removeUnlocked(const ResourceHandle& handle) {
    auto it = m_resources.find(handle);
    if (it != m_resources.end()) {
        m_currentMemoryUsage -= it->second->memoryUsage;
        m_resources.erase(it);
    }
}

void ResourceCache::evictResource(const ResourceHandle& handle) {
    // Called under lock
    auto it = m_resources.find(handle);
    if (it != m_resources.end()) {
        m_currentMemoryUsage -= it->second->memoryUsage;
        m_resources.erase(it);
    }
}

std::vector<ResourceHandle> ResourceCache::findEvictionCandidates() const {
    // Called under lock — returns handles sorted by oldest lastAccessed, unreferenced only
    std::vector<std::pair<ResourceHandle, std::chrono::steady_clock::time_point>> candidates;

    for (const auto& [handle, entry] : m_resources) {
        if (entry->referenceCount == 0) {
            candidates.emplace_back(handle, entry->lastAccessed);
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;  // Oldest first
        });

    std::vector<ResourceHandle> result;
    result.reserve(candidates.size());
    for (const auto& [handle, time] : candidates) {
        result.push_back(handle);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// AsyncLoader Implementation
// ═══════════════════════════════════════════════════════════════

AsyncLoader::AsyncLoader(size_t numThreads) : m_shutdown(false) {
    m_threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_threads.emplace_back(&AsyncLoader::workerThread, this);
    }
}

AsyncLoader::~AsyncLoader() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_shutdown = true;
    }
    m_condition.notify_all();
    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void AsyncLoader::submitTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push(std::move(task));
    }
    m_condition.notify_one();
}

void AsyncLoader::update() {
    // Tasks self-complete on worker threads.
    // This hook exists for future main-thread callbacks if needed.
}

size_t AsyncLoader::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_taskQueue.size();
}

void AsyncLoader::waitForAll() {
    // Spin-wait until queue is empty and all workers idle
    while (true) {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_taskQueue.empty()) break;
        }
        std::this_thread::yield();
    }
}

void AsyncLoader::cancelAll() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::queue<std::function<void()>> empty;
    m_taskQueue.swap(empty);
}

void AsyncLoader::workerThread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] {
                return m_shutdown || !m_taskQueue.empty();
            });

            if (m_shutdown && m_taskQueue.empty()) {
                return;
            }

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[AsyncLoader] Task failed: " << e.what() << std::endl;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// ResourceManager Implementation
// ═══════════════════════════════════════════════════════════════

ResourceManager::ResourceManager(VulkanDevice& device, size_t cacheSize)
    : m_device(device), m_cache(cacheSize), m_asyncLoader(4), m_nextId(1) {
    // No default loaders registered — infrastructure only.
    // Application code registers loaders via registerLoader<T>().
}

ResourceManager::~ResourceManager() {
    m_asyncLoader.waitForAll();
}

// ─── Handle Generation ──────────────────────────────────────

ResourceHandle ResourceManager::generateHandle(const std::string& name, ResourceType type) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto handle = ResourceHandle(name, type, m_nextId++);
    m_nameToHandle[name] = handle;
    m_handleToName[handle] = name;
    return handle;
}

// ─── Lookup ─────────────────────────────────────────────────

ResourceHandle ResourceManager::findHandle(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_nameToHandle.find(name);
    if (it != m_nameToHandle.end()) {
        return it->second;
    }
    return ResourceHandle();
}

bool ResourceManager::isLoaded(const ResourceHandle& handle) const {
    // Delegates to cache (separate mutex) — no deadlock with findHandle
    return m_cache.contains(handle);
}

bool ResourceManager::isLoaded(const std::string& name) const {
    auto handle = findHandle(name);
    return handle.isValid() && isLoaded(handle);
}

// ─── Type Deduction ─────────────────────────────────────────

ResourceDescriptor ResourceManager::createDescriptor(const std::string& name,
                                                       const std::filesystem::path& path) {
    ResourceType type = deduceType(path);
    return ResourceDescriptor(name, type, path);
}

ResourceType ResourceManager::deduceType(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();

    // Check registered extension mappings
    auto it = m_extensionToType.find(ext);
    if (it != m_extensionToType.end()) {
        return it->second;
    }

    return ResourceType::Unknown;
}

// ─── Synchronous Loading ────────────────────────────────────

ResourceHandle ResourceManager::load(const std::string& name, const std::filesystem::path& path) {
    auto desc = createDescriptor(name, path);
    return loadResource(desc);
}

ResourceHandle ResourceManager::load(const ResourceDescriptor& desc) {
    return loadResource(desc);
}

ResourceHandle ResourceManager::loadResource(const ResourceDescriptor& desc) {
    // Check if already loaded
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_nameToHandle.find(desc.name);
        if (it != m_nameToHandle.end() && m_cache.contains(it->second)) {
            return it->second;
        }
    }

    // Find appropriate loader
    std::shared_ptr<IResourceLoader> loader;
    std::string ext = desc.path.extension().string();

    auto extIt = m_extensionToLoader.find(ext);
    if (extIt != m_extensionToLoader.end()) {
        loader = extIt->second;
    } else {
        auto typeIt = m_typeToLoader.find(desc.type);
        if (typeIt != m_typeToLoader.end()) {
            loader = typeIt->second;
        }
    }

    if (!loader) {
        std::cerr << "[ResourceManager] No loader found for: " << desc.name
                  << " (ext: " << ext << ")" << std::endl;
        return ResourceHandle();
    }

    // Load the resource
    auto handle = generateHandle(desc.name, desc.type);
    try {
        auto data = loader->load(desc, m_device);
        if (data) {
            size_t memUsage = loader->getMemoryUsage(data);
            m_cache.store(handle, desc, std::move(data), memUsage);
            return handle;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ResourceManager] Failed to load " << desc.name
                  << ": " << e.what() << std::endl;
    }

    return ResourceHandle();
}

// ─── Asynchronous Loading ───────────────────────────────────

std::future<ResourceHandle> ResourceManager::loadAsync(
    const std::string& name,
    const std::filesystem::path& path,
    std::function<void(ResourceHandle, bool)> callback) {

    auto desc = createDescriptor(name, path);
    return loadResourceAsync(desc, callback);
}

std::future<ResourceHandle> ResourceManager::loadAsync(
    const ResourceDescriptor& desc,
    std::function<void(ResourceHandle, bool)> callback) {

    return loadResourceAsync(desc, callback);
}

std::future<ResourceHandle> ResourceManager::loadResourceAsync(
    const ResourceDescriptor& desc,
    std::function<void(ResourceHandle, bool)> callback) {

    auto promise = std::make_shared<std::promise<ResourceHandle>>();
    auto future = promise->get_future();

    m_asyncLoader.submitTask([this, desc, promise, callback]() {
        auto handle = loadResource(desc);
        bool success = handle.isValid();

        promise->set_value(handle);

        if (callback) {
            callback(handle, success);
        }
    });

    return future;
}

// ─── Resource Management ────────────────────────────────────

void ResourceManager::unload(const ResourceHandle& handle) {
    m_cache.remove(handle);

    std::lock_guard<std::mutex> lock(m_mutex);
    auto nameIt = m_handleToName.find(handle);
    if (nameIt != m_handleToName.end()) {
        m_nameToHandle.erase(nameIt->second);
        m_handleToName.erase(nameIt);
    }
}

void ResourceManager::unload(const std::string& name) {
    auto handle = findHandle(name);
    if (handle.isValid()) {
        unload(handle);
    }
}

void ResourceManager::preload(const std::vector<ResourceDescriptor>& resources) {
    std::vector<std::future<ResourceHandle>> futures;
    futures.reserve(resources.size());

    for (const auto& desc : resources) {
        futures.push_back(loadAsync(desc));
    }

    // Wait for all to complete
    for (auto& future : futures) {
        future.wait();
    }
}

void ResourceManager::hotReload(const ResourceHandle& handle) {
    // Retrieve descriptor before unloading (copy — unload invalidates the pointer)
    const ResourceDescriptor* descPtr = m_cache.getDescriptor(handle);
    if (!descPtr) return;

    ResourceDescriptor desc = *descPtr;

    unload(handle);

    try {
        loadResource(desc);
    } catch (const std::exception& e) {
        std::cerr << "[ResourceManager] Hot-reload failed for " << handle.name
                  << ": " << e.what() << std::endl;
    }
}

// ─── Memory Management ──────────────────────────────────────

void ResourceManager::garbageCollect() {
    m_cache.evictUnreferenced();
}

void ResourceManager::setMemoryBudget(size_t bytes) {
    m_cache.setMaxMemory(bytes);
}

size_t ResourceManager::getMemoryUsage() const {
    return m_cache.getCurrentMemory();
}

// ─── Statistics ─────────────────────────────────────────────

void ResourceManager::printStatistics() const {
    std::cout << "\n[ResourceManager] Statistics:" << std::endl;
    std::cout << "  Loaded resources: " << m_cache.getResourceCount() << std::endl;
    std::cout << "  Memory usage:     " << (m_cache.getCurrentMemory() / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  Memory budget:    " << (m_cache.getMaxMemory() / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  Pending loads:    " << m_asyncLoader.getPendingTaskCount() << std::endl;

    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "  Registered loaders: " << m_extensionToLoader.size() << " extensions" << std::endl;
    std::cout << std::endl;
}

std::vector<ResourceHandle> ResourceManager::getLoadedResources() const {
    return m_cache.getLoadedResources();
}

// ─── Per-Frame Update ───────────────────────────────────────

void ResourceManager::update() {
    m_asyncLoader.update();
}

} // namespace Shoonyakasha
