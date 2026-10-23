//
// InstanceBatching.cpp - Grouping an entity pass's draws into instanced draws
//

#include "FrameGraph/InstanceBatching.h"
#include "ECS/RenderComponents.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

namespace Shoonyakasha {

namespace {

uint64_t mix(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    hash ^= hash >> 31;
    return hash * 0xbf58476d1ce4e5b9ull;
}

// Bytes in 8-byte words, the last one zero-padded.
uint64_t mixBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i += 8) {
        uint64_t word = 0;
        std::memcpy(&word, bytes + i, std::min<size_t>(8, size - i));
        hash = mix(hash, word);
    }
    return mix(hash, size);
}

// A parameter or texture name by its length and its first and last eight
// bytes.
uint64_t nameKey(const std::string& name) {
    uint64_t first = 0, last = 0;
    const size_t n = std::min<size_t>(8, name.size());
    std::memcpy(&first, name.data(), n);
    std::memcpy(&last, name.data() + name.size() - n, n);
    return mix(mix(name.size(), first), last);
}

// The bytes a material parameter of each type uses.
size_t paramBytes(MaterialParam::Type type) {
    switch (type) {
        case MaterialParam::Type::Vec2: return 8;
        case MaterialParam::Type::Vec3: return 12;
        case MaterialParam::Type::Vec4: return 16;
        case MaterialParam::Type::Mat3: return 36;
        case MaterialParam::Type::Mat4: return 64;
        default:                        return 4;
    }
}

} // namespace

std::vector<InstanceBatch> batchDraws(const std::vector<uint64_t>& keys, bool keepOrder,
                                      std::vector<uint32_t>& order) {
    std::vector<InstanceBatch> batches;
    const uint32_t count = static_cast<uint32_t>(keys.size());
    order.resize(count);

    if (keepOrder) {
        for (uint32_t i = 0; i < count; ++i) {
            order[i] = i;
            if (keys[i] != 0 && i > 0 && keys[i] == keys[i - 1]) {
                ++batches.back().count;
            } else {
                batches.push_back({i, 1});
            }
        }
        return batches;
    }

    // Each draw's batch, numbered in the order of the batches' first draws;
    // then the draws placed batch by batch.
    std::unordered_map<uint64_t, uint32_t> batchOf;
    batchOf.reserve(count);
    std::vector<uint32_t> batchIndex(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t b = static_cast<uint32_t>(batches.size());
        if (keys[i] != 0) {
            const auto [it, added] = batchOf.try_emplace(keys[i], b);
            b = it->second;
            if (!added) {
                ++batches[b].count;
                batchIndex[i] = b;
                continue;
            }
        }
        batches.push_back({0, 1});
        batchIndex[i] = b;
    }
    uint32_t next = 0;
    for (InstanceBatch& batch : batches) {
        batch.first = next;
        next += batch.count;
    }
    std::vector<uint32_t> filled(batches.size(), 0);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t b = batchIndex[i];
        order[batches[b].first + filled[b]++] = i;
    }
    return batches;
}

uint64_t materialHash(const MaterialComponentV5& material) {
    // Entries are summed, so the maps' iteration order does not matter.
    uint64_t sum = 0;
    for (const auto& [name, param] : material.params) {
        const uint64_t h = mix(nameKey(name), static_cast<uint64_t>(param.type));
        sum += mixBytes(h, param.data.data(), paramBytes(param.type));
    }
    for (const auto& [name, texture] : material.textures) {
        uint64_t h = mix(nameKey(name), 0x7f4a7c15ull);
        h = mix(h, reinterpret_cast<uint64_t>(texture.view));
        sum += mix(h, reinterpret_cast<uint64_t>(texture.sampler));
    }
    uint32_t cutoff = 0;
    std::memcpy(&cutoff, &material.alphaCutoff, sizeof(cutoff));
    uint64_t h = mix(sum, static_cast<uint64_t>(material.alphaMode));
    h = mix(h, cutoff);
    return mix(h, material.doubleSided ? 1u : 0u);
}

uint64_t batchKey(const MeshComponent& mesh, const MaterialComponentV5& material) {
    uint64_t h = mix(reinterpret_cast<uint64_t>(mesh.vertexHandle()), reinterpret_cast<uint64_t>(mesh.indexHandle()));
    h = mix(h, (uint64_t{mesh.vertexCount} << 32) | mesh.indexCount);
    h = mix(h, (uint64_t{mesh.vertexStride} << 8) | static_cast<uint64_t>(mesh.indexType));
    h = mix(h, materialHash(material));
    return h != 0 ? h : 1;
}

} // namespace Shoonyakasha
