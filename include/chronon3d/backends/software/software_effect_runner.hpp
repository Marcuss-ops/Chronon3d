#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>

namespace chronon3d {

class SoftwareEffectRunner {
public:
    static void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                                  const renderer::SoftwareRegistry& registry,
                                  const effects::EffectExecutionContext& context);
    static void apply_blur(Framebuffer& fb, f32 radius);
};

} // namespace chronon3d
