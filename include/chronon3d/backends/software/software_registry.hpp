#pragma once

#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <unordered_map>
#include <memory>
#include <typeindex>

namespace chronon3d::renderer {

class SoftwareRegistry {
public:
    void register_shape(ShapeType type, std::unique_ptr<ShapeProcessor> processor) {
        m_shapes[type] = std::move(processor);
    }

    void register_effect(size_t variant_index, std::unique_ptr<EffectProcessor> processor) {
        m_effects[variant_index] = std::move(processor);
    }

    template<typename T>
    void register_effect_processor(std::unique_ptr<EffectProcessor> processor) {
        register_effect(typeid(T).hash_code(), std::move(processor));
    }

    ShapeProcessor* get_shape(ShapeType type) const {
        auto it = m_shapes.find(type);
        return it != m_shapes.end() ? it->second.get() : nullptr;
    }

    EffectProcessor* get_effect(size_t variant_index) const {
        auto it = m_effects.find(variant_index);
        return it != m_effects.end() ? it->second.get() : nullptr;
    }

private:
    std::unordered_map<ShapeType, std::unique_ptr<ShapeProcessor>> m_shapes;
    std::unordered_map<size_t, std::unique_ptr<EffectProcessor>> m_effects;
};

} // namespace chronon3d::renderer
