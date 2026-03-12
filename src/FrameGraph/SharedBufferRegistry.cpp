//
// Shoonyakasha Engine — Shared Buffer Registry Implementation
//

#include "FrameGraph/SharedBufferRegistry.h"

namespace Shoonyakasha {
namespace FrameGraph {

void SharedBufferRegistry::registerBuffer(const std::string& targetName,
                                           const SharedBufferEntry& entry) {
    auto it = m_entries.find(targetName);
    if (it != m_entries.end()) {
        // Overwrite existing entry, bump version
        uint64_t prevVersion = it->second.version;
        it->second = entry;
        it->second.version = prevVersion + 1;
    } else {
        m_entries[targetName] = entry;
    }
}

void SharedBufferRegistry::unregisterBuffer(const std::string& targetName) {
    m_entries.erase(targetName);
}

const SharedBufferEntry* SharedBufferRegistry::getBuffer(const std::string& targetName) const {
    auto it = m_entries.find(targetName);
    return (it != m_entries.end()) ? &it->second : nullptr;
}

bool SharedBufferRegistry::hasBuffer(const std::string& targetName) const {
    return m_entries.contains(targetName);
}

uint64_t SharedBufferRegistry::getVersion(const std::string& targetName) const {
    auto it = m_entries.find(targetName);
    return (it != m_entries.end()) ? it->second.version : 0;
}

// ── Image sharing ──

void SharedBufferRegistry::registerImage(const std::string& targetName, const SharedImageEntry& entry) {
    auto it = m_imageEntries.find(targetName);
    if (it != m_imageEntries.end()) {
        uint64_t prevVersion = it->second.version;
        it->second = entry;
        it->second.version = prevVersion + 1;
    } else {
        m_imageEntries[targetName] = entry;
    }
}

void SharedBufferRegistry::unregisterImage(const std::string& targetName) {
    m_imageEntries.erase(targetName);
}

const SharedImageEntry* SharedBufferRegistry::getImage(const std::string& targetName) const {
    auto it = m_imageEntries.find(targetName);
    return (it != m_imageEntries.end()) ? &it->second : nullptr;
}

bool SharedBufferRegistry::hasImage(const std::string& targetName) const {
    return m_imageEntries.contains(targetName);
}

void SharedBufferRegistry::clear() {
    m_entries.clear();
    m_imageEntries.clear();
}

} // namespace FrameGraph
} // namespace Shoonyakasha
