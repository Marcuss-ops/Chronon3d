#pragma once

#include <chronon3d/internal/render_graph/processor_registry_snapshot.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace chronon3d::renderer {

class SoftwareRegistry {
public:
    void register_shape(ShapeType type, std::unique_ptr<ShapeProcessor> processor) {
        register_shape(type, std::shared_ptr<ShapeProcessor>(std::move(processor)));
    }

    void register_shape(ShapeType type, std::shared_ptr<ShapeProcessor> processor) {
        m_shapes[type] = std::move(processor);
        ++m_generation;
    }

    void register_effect(std::type_index type, std::unique_ptr<EffectProcessor> processor) {
        register_effect(type, std::shared_ptr<EffectProcessor>(std::move(processor)));
    }

    void register_effect(std::type_index type, std::shared_ptr<EffectProcessor> processor) {
        m_effects[type] = std::move(processor);
        ++m_generation;
    }

    /// Register an effect processor for the given effect-params type T.
    /// Constrained to complete object types (avoids void/function nonsense).
    template<typename T>
        requires std::is_object_v<T>
    void register_effect_processor(std::unique_ptr<EffectProcessor> processor) {
        register_effect(std::type_index(typeid(T)), std::move(processor));
    }

    [[nodiscard]] std::shared_ptr<ShapeProcessor> get_shape_shared(
        ShapeType type) const {
        auto it = m_shapes.find(type);
        return it != m_shapes.end() ? it->second : nullptr;
    }

    [[nodiscard]] std::shared_ptr<EffectProcessor> get_effect_shared(
        std::type_index type) const {
        auto it = m_effects.find(type);
        return it != m_effects.end() ? it->second : nullptr;
    }



    /// Capture the current engine-local processor ownership and mapping.
    /// Entries are sorted so handle indices are deterministic despite the
    /// unordered-map storage used by the mutable registry. The returned
    /// snapshot owns shared references to every captured processor.
    [[nodiscard]] std::shared_ptr<const ProcessorRegistrySnapshot> snapshot() const {
        std::vector<ProcessorRegistrySnapshot::ShapeEntry> shapes;
        shapes.reserve(m_shapes.size());
        for (const auto& [type, processor] : m_shapes) {
            shapes.emplace_back(type, processor);
        }
        std::sort(shapes.begin(), shapes.end(), [](const auto& lhs, const auto& rhs) {
            return static_cast<int>(lhs.first) < static_cast<int>(rhs.first);
        });

        std::vector<ProcessorRegistrySnapshot::EffectEntry> effects;
        effects.reserve(m_effects.size());
        for (const auto& [type, processor] : m_effects) {
            effects.emplace_back(type, processor);
        }
        std::sort(effects.begin(), effects.end(), [](const auto& lhs, const auto& rhs) {
            const auto lhs_hash = lhs.first.hash_code();
            const auto rhs_hash = rhs.first.hash_code();
            if (lhs_hash != rhs_hash) return lhs_hash < rhs_hash;
            return lhs.first.name() < rhs.first.name();
        });

        return std::make_shared<const ProcessorRegistrySnapshot>(
            std::move(shapes), std::move(effects), m_generation);
    }

    /// Monotonic generation for processor registrations/replacements.
    /// Used to invalidate compiled topology reuse when a processor mapping
    /// changes without changing authored scene data.
    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }

    /// Lifetime token for consumers that may outlive this mutable registry.
    /// A backend can refresh while the registry exists and safely fall back to
    /// its last owning snapshot after registry/engine shutdown.
    [[nodiscard]] std::weak_ptr<const void> lifetime_token() const noexcept {
        return m_lifetime_token;
    }

private:
    std::shared_ptr<const void> m_lifetime_token{std::make_shared<int>(0)};
    std::unordered_map<ShapeType, std::shared_ptr<ShapeProcessor>> m_shapes;
    std::unordered_map<std::type_index, std::shared_ptr<EffectProcessor>> m_effects;
    std::uint64_t m_generation{1};
};

} // namespace chronon3d::renderer
