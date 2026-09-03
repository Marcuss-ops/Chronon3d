#pragma once

// ---------------------------------------------------------------------------
// internal/render_graph/processor_registry_snapshot.hpp
//
// Immutable, owning processor bindings captured from an engine-local software
// registry. The mapping and ownership are immutable after capture; processor
// implementations may retain their normal internal mutable state while they
// are used by the renderer. Compiled graphs retain a shared snapshot so
// processor lifetime is independent from the mutable registry that produced it.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/processor_handle.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chronon3d::renderer {

class ShapeProcessor;
class EffectProcessor;

class ProcessorRegistrySnapshot final {
public:
    using ShapeEntry = std::pair<ShapeType, std::shared_ptr<ShapeProcessor>>;
    using EffectEntry = std::pair<std::type_index, std::shared_ptr<EffectProcessor>>;

    ProcessorRegistrySnapshot(
        std::vector<ShapeEntry> shapes,
        std::vector<EffectEntry> effects,
        std::uint64_t generation)
        : m_shapes(std::move(shapes))
        , m_effects(std::move(effects))
        , m_generation(generation)
        , m_identity(compute_identity())
    {
        m_shape_indices.reserve(m_shapes.size());
        for (std::uint32_t i = 0; i < m_shapes.size(); ++i) {
            m_shape_indices.emplace(m_shapes[i].first, i);
        }

        m_effect_indices.reserve(m_effects.size());
        for (std::uint32_t i = 0; i < m_effects.size(); ++i) {
            m_effect_indices.emplace(m_effects[i].first, i);
        }
    }

    ProcessorRegistrySnapshot(const ProcessorRegistrySnapshot&) = delete;
    ProcessorRegistrySnapshot& operator=(const ProcessorRegistrySnapshot&) = delete;
    ProcessorRegistrySnapshot(ProcessorRegistrySnapshot&&) = delete;
    ProcessorRegistrySnapshot& operator=(ProcessorRegistrySnapshot&&) = delete;

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return m_generation;
    }

    /// Stable identity for the captured processor mapping. It distinguishes
    /// engine-local registries even when their generations happen to match,
    /// while remaining equal for equivalent snapshots of the same processor
    /// instances.
    [[nodiscard]] std::uint64_t identity() const noexcept {
        return m_identity;
    }

    [[nodiscard]] ShapeProcessorHandle shape_handle(ShapeType type) const noexcept {
        const auto it = m_shape_indices.find(type);
        return it == m_shape_indices.end()
            ? ShapeProcessorHandle{}
            : ShapeProcessorHandle{it->second};
    }

    [[nodiscard]] EffectProcessorHandle effect_handle(
        std::type_index type) const noexcept {
        const auto it = m_effect_indices.find(type);
        return it == m_effect_indices.end()
            ? EffectProcessorHandle{}
            : EffectProcessorHandle{it->second};
    }

    /// Owning access used by dispatch boundaries. The returned shared pointer
    /// keeps the processor alive for the entire operation, including when the
    /// originating registry or engine is shutting down.
    [[nodiscard]] std::shared_ptr<ShapeProcessor> shape_shared(
        ShapeProcessorHandle handle) const noexcept {
        if (!handle.valid() || handle.index >= m_shapes.size()) {
            return nullptr;
        }
        return m_shapes[handle.index].second;
    }

    [[nodiscard]] std::shared_ptr<EffectProcessor> effect_shared(
        EffectProcessorHandle handle) const noexcept {
        if (!handle.valid() || handle.index >= m_effects.size()) {
            return nullptr;
        }
        return m_effects[handle.index].second;
    }



private:
    [[nodiscard]] std::uint64_t compute_identity() const noexcept {
        constexpr std::uint64_t offset = 0xcbf29ce484222325ULL;
        constexpr std::uint64_t prime = 0x100000001b3ULL;
        std::uint64_t hash = offset;
        auto fold = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= prime;
        };
        fold(m_generation);
        for (const auto& [type, processor] : m_shapes) {
            fold(static_cast<std::uint64_t>(type));
            fold(reinterpret_cast<std::uintptr_t>(processor.get()));
        }
        for (const auto& [type, processor] : m_effects) {
            fold(static_cast<std::uint64_t>(type.hash_code()));
            fold(reinterpret_cast<std::uintptr_t>(processor.get()));
        }
        return hash == 0 ? 1 : hash;
    }

    std::vector<ShapeEntry> m_shapes;
    std::vector<EffectEntry> m_effects;
    std::unordered_map<ShapeType, std::uint32_t> m_shape_indices;
    std::unordered_map<std::type_index, std::uint32_t> m_effect_indices;
    std::uint64_t m_generation{0};
    std::uint64_t m_identity{1};
};

} // namespace chronon3d::renderer
