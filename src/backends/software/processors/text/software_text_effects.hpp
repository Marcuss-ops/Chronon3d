#pragma once

namespace chronon3d::renderer {

    /// Release cached glow BLImages from the software-text pipeline.
    /// Called by SoftwareRenderer::clear_caches() on text builds.
    void clear_text_glow_cache();

    /// Release cached shadow BLImages from the software-text pipeline.
    void clear_text_shadow_cache();

}

#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/backends/software/software_processor_context.hpp>
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
