//
// GpuComponents.h - rows of bytes per entity, copied into storage buffers
//
// A table per component name holds one row per entity, all rows the same
// size (the table's stride, fixed by its first row). Rows are kept packed in
// one array, in no particular order: removing a row moves the last one into
// its place. A pipeline buffer layout with
// "source": {"type": "scene", "data": "components", "component": <name>}
// receives the rows every frame (RenderGraph).
//

#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha::ECS {

class GpuComponentTables {
public:
    struct Table {
        size_t stride = 0;
        std::vector<uint8_t> rows;                         // count() rows of stride bytes
        std::vector<entt::entity> entities;                // the entity of each row
        std::unordered_map<entt::entity, size_t> index;    // entity -> row

        size_t count() const { return entities.size(); }
    };

    /// Sets the row of `entity` in table `name` to `size` bytes from `data`,
    /// adding it when the entity has none. The table's first row fixes its
    /// stride. Returns an error message, empty when the row was set: `size`
    /// differs from the stride, or is 0.
    std::string set(entt::entity entity, const std::string& name, const void* data, size_t size);

    /// set() for `count` entities, row i from `rows + i * stride`.
    std::string setMany(const std::string& name, const entt::entity* entities, size_t count, const void* rows,
                        size_t stride);

    /// Removes the row of `entity` from table `name`; false when it had none.
    bool remove(entt::entity entity, const std::string& name);

    /// Removes the rows of entities `registry` no longer holds, from every table.
    void dropInvalid(const entt::registry& registry);

    /// Table `name`, or null when nothing was ever set in it.
    const Table* find(const std::string& name) const;
    const std::unordered_map<std::string, Table>& tables() const { return m_tables; }

private:
    static void removeRow(Table& table, size_t row);
    std::unordered_map<std::string, Table> m_tables;
};

} // namespace Shoonyakasha::ECS
