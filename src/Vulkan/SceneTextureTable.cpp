//
// SceneTextureTable.cpp
//

#include "Vulkan/SceneTextureTable.h"

#include "ECS/RenderComponents.h"
#include "GPU/GPUTypes.h"

namespace Shoonyakasha {

void SceneTextureTable::build(entt::registry& registry, uint32_t capacity) {
    m_slots.clear();
    m_slotOfView.clear();
    if (capacity == 0) return;
    auto view = registry.view<MeshComponent, MaterialComponentV5, RenderableTagComponent>();
    for (auto entity : view) {
        if (!view.get<MeshComponent>(entity).isValid() || !view.get<RenderableTagComponent>(entity).shouldRender()) {
            continue;
        }
        for (const auto& [name, texture] : view.get<MaterialComponentV5>(entity).textures) add(texture, capacity);
    }
}

void SceneTextureTable::add(const GPUTexture& texture, uint32_t capacity) {
    if (!texture.exists || texture.view == VK_NULL_HANDLE || m_slots.size() >= capacity) return;
    if (m_slotOfView.emplace(texture.view, static_cast<uint32_t>(m_slots.size())).second) {
        m_slots.push_back({texture.view, texture.sampler});
    }
}

uint32_t SceneTextureTable::slotOf(const GPUTexture* texture) const {
    if (!texture || !texture->exists) return kNoTexture;
    const auto it = m_slotOfView.find(texture->view);
    return it != m_slotOfView.end() ? it->second : kNoTexture;
}

} // namespace Shoonyakasha
