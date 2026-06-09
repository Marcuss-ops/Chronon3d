#pragma once

// ---------------------------------------------------------------------------
// text_effects.hpp
//
// Internal declarations for text glow and shadow rendering.
// Used by software_text_processor.cpp, implemented in text_glow.cpp and text_shadow.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <blend2d.h>

namespace chronon3d::renderer {

void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                    const RenderState& state, const TextRasterization& raster,
                    float effective_size);

void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                      const RenderState& state, const TextRasterization& raster,
                      const TextShadow& shadow, size_t index, float effective_size);

// Separable box blur directly on BLImage PRGB32 pixel data.
// Eliminates the intermediate Framebuffer allocation+copy in the
// old glow/shadow pipeline (BLImage → Framebuffer → blur → Framebuffer).
void blur_bl_image_inplace(BLImage& img, float radius);

} // namespace chronon3d::renderer
