#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <any>

namespace chronon3d::renderer {

class EffectProcessor {
public:
    virtual ~EffectProcessor() = default;

    virtual void apply(Framebuffer& fb, const std::any& params, float time_seconds) = 0;
};

} // namespace chronon3d::renderer
