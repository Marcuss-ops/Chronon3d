#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include "utils/render_effects_processor.hpp"

namespace chronon3d {

void SoftwareEffectRunner::apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                                             const renderer::SoftwareRegistry& registry) {
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;
        if (auto* processor = registry.get_effect(effect.params.index())) {
            processor->apply(fb, effect.params);
        } else {
            // Fallback for effects not in software registry if needed
            // But usually the software registry should cover all built-ins.
            EffectStack single_effect{effect};
            renderer::apply_effect_stack(fb, single_effect);
        }
    }
}

void SoftwareEffectRunner::apply_blur(Framebuffer& fb, f32 radius) {
    renderer::apply_blur(fb, radius);
}

} // namespace chronon3d
