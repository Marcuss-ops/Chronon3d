#pragma once

#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <unordered_map>
#include <memory>
#include <variant>
#include <type_traits>

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
        register_effect(effect_variant_index<T>(), std::move(processor));
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

    template<typename T, typename Variant>
    struct VariantIndex;

    template<typename T, typename... Ts>
    struct VariantIndex<T, std::variant<T, Ts...>> : std::integral_constant<size_t, 0> {};

    template<typename T, typename U, typename... Ts>
    struct VariantIndex<T, std::variant<U, Ts...>>
        : std::integral_constant<size_t, 1 + VariantIndex<T, std::variant<Ts...>>::value> {};

    template<typename T>
    static consteval size_t effect_variant_index() {
        return VariantIndex<T, EffectParams>::value;
    }
};

} // namespace chronon3d::renderer
