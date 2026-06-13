#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include "utils/render_effects_processor.hpp"

namespace chronon3d {

void SoftwareEffectRunner::apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                                             const renderer::SoftwareRegistry& registry,
                                             const effects::EffectExecutionContext& context) {
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;

        // Use param_type_index() — a fast switch on effect_type — instead of
        // std::any::type() which is no longer available now that params is a
        // variant (std::get_if is O(1) with no type_info comparison).
        if (auto* processor = registry.get_effect(effect.param_type_index())) {
            processor->apply(fb, effect.params, context);
            continue;
        }

        // Fallback for effects not in software registry or if resolution failed
        EffectStack single_effect{effect};
        renderer::apply_effect_stack(fb, single_effect, context);
    }
}

void SoftwareEffectRunner::apply_blur(Framebuffer& fb, f32 radius) {
    renderer::apply_blur(fb, radius);
}

} // namespace chronon3d
