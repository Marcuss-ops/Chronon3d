#pragma once

#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>

#include <optional>
#include <string_view>

namespace chronon3d::graph::native_effects {
bool clip_covers_result(const std::optional<raster::BBox>& clip, const Framebuffer& result);
bool try_native_echo(RenderGraphContext& ctx, const EffectInstance& effect, Framebuffer& source, Framebuffer& result, std::string_view history_key, const std::optional<raster::BBox>& clip);
bool try_native_full_frame_blur(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source, const Framebuffer* borrowed_source_framebuffer);
bool try_native_full_frame_drop_shadow(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source, const Framebuffer* borrowed_source_framebuffer = nullptr);
bool try_native_full_frame_sharpen(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source);
bool try_native_full_frame_directional_blur(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source);
bool try_native_full_frame_radial_blur(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source);
bool try_native_full_frame_warp_bulk(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source, runtime::RenderSurfaceHandle map_source = runtime::kInvalidRenderSurfaceHandle, const Framebuffer* map_framebuffer = nullptr, const Framebuffer* src_framebuffer = nullptr);
bool try_native_full_frame_generate_bulk(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source);
bool try_native_full_frame_lut_bulk(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source);
bool try_native_full_frame_glow_or_bloom(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle existing_source, const Framebuffer* existing_source_framebuffer);
bool try_native_full_frame_tint(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source);
bool try_native_full_frame_color(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source, const Framebuffer* borrowed_source_framebuffer);
bool try_native_full_frame_chromatic(RenderGraphContext& ctx, const EffectStack& effect_stack, Framebuffer& result, const std::optional<raster::BBox>& clip, runtime::RenderSurfaceHandle borrowed_source, const Framebuffer* borrowed_source_framebuffer);
}
