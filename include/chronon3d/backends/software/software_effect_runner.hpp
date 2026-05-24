#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/backends/software/software_registry.hpp>

namespace chronon3d {

class SoftwareEffectRunner {
public:
    static void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                                  const renderer::SoftwareRegistry& registry,
                                  float time_seconds);
    static void apply_blur(Framebuffer& fb, f32 radius);
};

} // namespace chronon3d
