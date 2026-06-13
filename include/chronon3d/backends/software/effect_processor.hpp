#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>

namespace chronon3d::renderer {

class EffectProcessor {
public:
    virtual ~EffectProcessor() = default;

    // Apply the effect to the framebuffer.  params is the variant-stored
    // parameter struct (formerly std::any); extraction via std::get_if<T>
    // is O(1) with no type_info comparison.  context carries the per-frame
    // state: time, frame number, clip, quality, and diagnostics flag.
    virtual void apply(Framebuffer& fb, const EffectParams& params,
                       const effects::EffectExecutionContext& context) = 0;
};

} // namespace chronon3d::renderer
