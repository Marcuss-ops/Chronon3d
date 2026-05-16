#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <optional>
#include "utils/render_effects_processor.hpp"

namespace chronon3d {

void SoftwareEffectRunner::apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                                             const renderer::SoftwareRegistry& registry) {
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;
        
        std::optional<EffectParams> resolved_params;
        if (auto* v = std::any_cast<EffectParams>(&effect.params)) {
            resolved_params = *v;
        } else if (auto* p = std::any_cast<BlurParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        } else if (auto* p = std::any_cast<TintParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        } else if (auto* p = std::any_cast<BrightnessParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        } else if (auto* p = std::any_cast<ContrastParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        } else if (auto* p = std::any_cast<BloomParams>(&effect.params)) {
            resolved_params = EffectParams{*p};
        }

        if (resolved_params) {
            if (auto* processor = registry.get_effect(resolved_params->index())) {
                processor->apply(fb, *resolved_params);
                continue;
            }
        }

        // Fallback for effects not in software registry or if resolution failed
        EffectStack single_effect{effect};
        renderer::apply_effect_stack(fb, single_effect);
    }
}

void SoftwareEffectRunner::apply_blur(Framebuffer& fb, f32 radius) {
    renderer::apply_blur(fb, radius);
}

} // namespace chronon3d
