#pragma once

#include <chronon3d/renderer/software/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/renderer/software/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/renderer/assets/font_cache.hpp>

namespace chronon3d {

class TextRenderer {
public:
    // state is optional — when non-null, mask clipping is applied per pixel.
    bool draw_text(const TextShape& text, const Transform& tr, Framebuffer& fb,
                   const RenderState* state = nullptr);

    void clear_cache() { m_cache.clear(); }

private:
    FontCache m_cache;
};

} // namespace chronon3d
