#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>

namespace chronon3d::renderer {

class EffectProcessor {
public:
    virtual ~EffectProcessor() = default;

    virtual void apply(Framebuffer& fb, const EffectParams& params) = 0;
};

} // namespace chronon3d::renderer
