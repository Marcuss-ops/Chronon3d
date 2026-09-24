#pragma once

#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <optional>

namespace chronon3d {
class Framebuffer;
namespace raster { struct BBox; }
namespace graph {
struct RenderGraphContext;

namespace native_effects {

bool try_native_full_frame_blur(
    RenderGraphContext&, const EffectStack&, Framebuffer&,
    const std::optional<raster::BBox>&,
    runtime::RenderSurfaceHandle borrowed_source =
        runtime::kInvalidRenderSurfaceHandle);
bool try_native_full_frame_glow(
    RenderGraphContext&, const EffectStack&, Framebuffer&,
    const std::optional<raster::BBox>&,
    runtime::RenderSurfaceHandle = runtime::kInvalidRenderSurfaceHandle,
    const Framebuffer* existing_source_framebuffer = nullptr);
bool try_native_full_frame_tint(
    RenderGraphContext&, const EffectStack&, Framebuffer&,
    const std::optional<raster::BBox>&);

} // namespace native_effects
} // namespace graph
} // namespace chronon3d
