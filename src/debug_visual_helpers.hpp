#pragma once

// ── Visual debug helpers ───────────────────────────────────────────
//
// All functions in this file are guarded by environment-variable checks
// and compile to minimal overhead when the vars are not set.
//
// Available env vars:
//   CHRONON_DEBUG_DISABLE_DIRTY  — disables dirty rects for full render
//   CHRONON_DEBUG_SOURCE_NO_CLIP — forces clip_rect = nullopt on SourceNodes
//   CHRONON_DEBUG_VISUAL         — logs [VDBG Source/Transform/Composite] lines
//   CHRONON_DEBUG_DUMP_FB        — saves intermediate framebuffers as PNG
//
// This file is NOT included by any production source file.
// To use during debugging, temporarily add:
//   #include "src/debug_visual_helpers.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <filesystem>
#include <optional>
#include <string>
#include <cstdlib>

namespace chronon3d::debug {

// ── Env var queries ───────────────────────────────────────────────

inline bool visual_debug_enabled() {
    return std::getenv("CHRONON_DEBUG_VISUAL") != nullptr;
}

inline bool dump_fb_enabled() {
    return std::getenv("CHRONON_DEBUG_DUMP_FB") != nullptr;
}

// ── Helpers ───────────────────────────────────────────────────────

inline std::string fmt_bbox(const std::optional<raster::BBox>& b) {
    if (!b) return "null";
    return fmt::format("[{},{},{},{}]", b->x0, b->y0, b->x1, b->y1);
}

/// Save a framebuffer to output/debug_fb/ as PNG.
inline void save_debug_fb(const Framebuffer& fb, const std::string& label, int frame) {
    if (!dump_fb_enabled()) return;
    std::filesystem::create_directories("output/debug_fb");
    const auto path = fmt::format("output/debug_fb/f{:04d}_{}.png", frame, label);
    chronon3d::save_png(fb, path);
    spdlog::info("[VDBG-dump] saved: {}", path);
}

// ── CHRONON_DEBUG_DISABLE_DIRTY ───────────────────────────────────

/// Override dirty rects for full-frame re-render. Call right after
/// compute_dirty_rect() in the render pipeline.
template <typename DirtyOut, typename Ctx>
inline void apply_disable_dirty(DirtyOut& dirty_out, Ctx& ctx) {
    if (!std::getenv("CHRONON_DEBUG_DISABLE_DIRTY")) return;
    dirty_out.dirty_rect = std::nullopt;
    dirty_out.use_dirty_rects = false;
    dirty_out.use_dirty_tiles = false;
    ctx.tile.dirty_rect = std::nullopt;
    ctx.options.reuse_prev_framebuffer = false;
    spdlog::warn("[VDBG] CHRONON_DEBUG_DISABLE_DIRTY active — dirty rects disabled");
}

// ── CHRONON_DEBUG_SOURCE_NO_CLIP ──────────────────────────────────

/// Override clip_rect for SourceNode debugging. Returns the clip rect
/// to use (nullopt when the env var is set, the original value otherwise).
inline std::optional<raster::BBox> source_debug_clip(
    const std::optional<raster::BBox>& original_clip,
    const std::string& node_name)
{
    if (!std::getenv("CHRONON_DEBUG_SOURCE_NO_CLIP")) return original_clip;
    spdlog::warn("[VDBG] CHRONON_DEBUG_SOURCE_NO_CLIP — clip_rect forced nullopt for node='{}'", node_name);
    return std::nullopt;
}

// ── CHRONON_DEBUG_VISUAL log helpers ──────────────────────────────

inline void log_source_node(int frame, const std::string& name, int shape_type,
                            const std::optional<raster::BBox>& clip,
                            const std::optional<raster::BBox>& predicted,
                            int w, int h)
{
    if (!visual_debug_enabled()) return;
    spdlog::warn("[VDBG Source] frame={} node='{}' shape={} clip_rect={} predicted={} fb_size={}x{}",
                 frame, name, shape_type, fmt_bbox(clip), fmt_bbox(predicted), w, h);
}

inline void log_transform_node(int frame,
                               const std::optional<raster::BBox>& input_bbox,
                               const std::optional<raster::BBox>& predicted,
                               const std::optional<raster::BBox>& clip)
{
    if (!visual_debug_enabled()) return;
    spdlog::warn("[VDBG Transform] frame={} input_bbox={} predicted={} clip={}",
                 frame, fmt_bbox(input_bbox), fmt_bbox(predicted), fmt_bbox(clip));
}

inline void log_composite_layer(const std::optional<raster::BBox>& clip,
                                i32 dst_ox, i32 dst_oy, i32 dst_w, i32 dst_h,
                                i32 src_ox, i32 src_oy, i32 src_w, i32 src_h,
                                int mode)
{
    if (!visual_debug_enabled()) return;
    spdlog::warn("[VDBG Composite] clip={} dst_origin=[{},{}] dst_size=[{},{}] "
                 "src_origin=[{},{}] src_size=[{},{}] mode={}",
                 fmt_bbox(clip),
                 dst_ox, dst_oy, dst_w, dst_h,
                 src_ox, src_oy, src_w, src_h,
                 mode);
}

} // namespace chronon3d::debug
