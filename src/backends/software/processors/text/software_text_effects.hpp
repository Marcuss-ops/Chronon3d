#pragma once

#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::renderer {

void draw_text_glow(
    const SoftwareProcessorContext& rctx,
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const TextRasterization& raster,
    float effective_size
);

void draw_text_shadow(
    const SoftwareProcessorContext& rctx,
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const TextRasterization& raster,
    const TextShadow& shadow,
    size_t index,
    float effective_size
);

} // namespace chronon3d::renderer
