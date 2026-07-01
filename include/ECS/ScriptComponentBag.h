//
// ScriptComponentBag.h - Generic opaque component storage for scripting bindings
//
// entt::registry::emplace<T>() needs T known to the C++ compiler, so a
// scripting language (Python today) can't get its own real entt storage
// pool per user-defined type without runtime reflection (entt::meta),
// which this engine doesn't use anywhere (see docs/plans/2026-07-01-
// low-level-python-ecs-bindings.md for the full rationale).
//
// Instead, ALL of an entity's script-defined components live in one
// ScriptComponentBag component, keyed by name, holding type-erased
// std::shared_ptr<void> values. This mirrors the generic name->value map
// pattern MaterialComponentV5 already uses (params/textures) for the same
// reason - the engine core has zero knowledge of what's actually stored
// (a PyObject*, or anything else) - that's entirely the binding layer's
// concern (see python/shoonyakasha/_ecs_bridge.h for the Python side).
//
// Querying "all entities with script component X" is a scan over
// ScriptComponentBag-bearing entities filtered by key presence - O(entities
// with *any* script component), not an indexed lookup. Fine for
// gameplay/UI-level scripting; not appropriate for hot per-frame loops
// over large entity counts (keep those in real C++/entt components).
//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace Shoonyakasha {

struct ScriptComponentBag {
    std::unordered_map<std::string, std::shared_ptr<void>> components;

    bool has(const std::string& name) const {
        return components.find(name) != components.end();
    }

    std::shared_ptr<void> get(const std::string& name) const {
        auto it = components.find(name);
        return it != components.end() ? it->second : nullptr;
    }

    void set(const std::string& name, std::shared_ptr<void> value) {
        components[name] = std::move(value);
    }

    bool remove(const std::string& name) {
        return components.erase(name) > 0;
    }
};

} // namespace Shoonyakasha
