#pragma once

#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <cstdint>

namespace chronon3d::renderer {

class SoftwareRegistry {
public:
    void register_shape(ShapeType type, std::unique_ptr<ShapeProcessor> processor) {
        m_shapes[type] = std::move(processor);
        ++m_generation;
    }

    void register_effect(std::type_index type, std::unique_ptr<EffectProcessor> processor) {
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

    ShapeProcessor* get_shape(ShapeType type) const {
        auto it = m_shapes.find(type);
        return it != m_shapes.end() ? it->second.get() : nullptr;
    }

    EffectProcessor* get_effect(std::type_index type) const {
        auto it = m_effects.find(type);
        return it != m_effects.end() ? it->second.get() : nullptr;
    }

    /// Monotonic generation for processor registrations/replacements.
    /// Used to invalidate compiled topology reuse when a processor mapping
    /// changes without changing authored scene data.
    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }

private:
    std::unordered_map<ShapeType, std::unique_ptr<ShapeProcessor>> m_shapes;
    std::unordered_map<std::type_index, std::unique_ptr<EffectProcessor>> m_effects;
    std::uint64_t m_generation{1};
};

} // namespace chronon3d::renderer
