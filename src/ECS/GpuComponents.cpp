//
// GpuComponents.cpp - per-entity rows for storage buffers (include/ECS/GpuComponents.h)
//

#include "ECS/GpuComponents.h"

#include <cstring>

namespace Shoonyakasha::ECS {

std::string GpuComponentTables::set(entt::entity entity, const std::string& name, const void* data, size_t size) {
    return setMany(name, &entity, 1, data, size);
}

std::string GpuComponentTables::setMany(const std::string& name, const entt::entity* entities, size_t count,
                                        const void* rows, size_t stride) {
    if (stride == 0) return "component '" + name + "': rows need at least one byte";
    Table& table = m_tables[name];
    if (table.stride == 0) table.stride = stride;
    if (stride != table.stride) {
        return "component '" + name + "' has rows of " + std::to_string(table.stride) + " bytes, " +
               std::to_string(stride) + " were given";
    }
    const auto* source = static_cast<const uint8_t*>(rows);
    for (size_t i = 0; i < count; ++i) {
        auto [it, added] = table.index.try_emplace(entities[i], table.entities.size());
        if (added) {
            table.entities.push_back(entities[i]);
            table.rows.resize(table.rows.size() + stride);
        }
        std::memcpy(table.rows.data() + it->second * stride, source + i * stride, stride);
    }
    return {};
}

void GpuComponentTables::removeRow(Table& table, size_t row) {
    const size_t last = table.entities.size() - 1;
    table.index.erase(table.entities[row]);
    if (row != last) {
        std::memcpy(table.rows.data() + row * table.stride, table.rows.data() + last * table.stride, table.stride);
        table.entities[row] = table.entities[last];
        table.index[table.entities[row]] = row;
    }
    table.entities.pop_back();
    table.rows.resize(last * table.stride);
}

bool GpuComponentTables::remove(entt::entity entity, const std::string& name) {
    const auto table = m_tables.find(name);
    if (table == m_tables.end()) return false;
    const auto row = table->second.index.find(entity);
    if (row == table->second.index.end()) return false;
    removeRow(table->second, row->second);
    return true;
}

void GpuComponentTables::dropInvalid(const entt::registry& registry) {
    for (auto& [name, table] : m_tables) {
        for (size_t row = table.entities.size(); row-- > 0;) {
            if (!registry.valid(table.entities[row])) removeRow(table, row);
        }
    }
}

const GpuComponentTables::Table* GpuComponentTables::find(const std::string& name) const {
    const auto it = m_tables.find(name);
    return it == m_tables.end() ? nullptr : &it->second;
}

} // namespace Shoonyakasha::ECS
