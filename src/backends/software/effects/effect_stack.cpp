// effect_stack.cpp — registry-backed software effect dispatch.

#include "render_effects_processor.hpp"
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_registry.hpp>

#include <stdexcept>

namespace chronon3d::renderer {

void apply_effect_stack(
    Framebuffer& fb,
    const EffectStack& stack,
    const effects::EffectExecutionContext& context,
    std::span<EffectProcessor* const> processors) {
    if (processors.size() != stack.size()) {
        throw std::runtime_error(
            "Compiled effect processor binding count does not match effect stack");
    }
    for (std::size_t index = 0; index < stack.size(); ++index) {
        const auto& effect = stack[index];
        if (!effect.enabled) continue;

        auto* processor = processors[index];
        if (!processor) {
            throw std::runtime_error(
                "Missing compiled software effect processor for effect type " +
                std::to_string(static_cast<int>(effect.effect_type)));
        }
        processor->apply(fb, effect.params, context);
    }
}

void apply_effect_stack(
    Framebuffer& fb,
    const EffectStack& stack,
    const effects::EffectExecutionContext& context,
    SoftwareRegistry& registry) {
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;

        auto* processor = registry.get_effect(effect.param_type_index());
        if (!processor) {
            throw std::runtime_error(
                "No software effect processor registered for effect type " +
                std::to_string(static_cast<int>(effect.effect_type)));
        }
        processor->apply(fb, effect.params, context);
    }
}

} // namespace chronon3d::renderer
