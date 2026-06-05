#pragma once

// ---------------------------------------------------------------------------
// text_effects.hpp
//
// Internal declarations for text glow and shadow rendering.
// Used by software_text_processor.cpp, implemented in text_glow.cpp and text_shadow.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>

namespace chronon3d::renderer {

void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                    const RenderState& state, const TextRasterization& raster,
                    float effective_size);

void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                      const RenderState& state, const TextRasterization& raster,
                      const TextShadow& shadow, size_t index, float effective_size);

} // namespace chronon3d::renderer
