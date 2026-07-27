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
