#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

#include <spdlog/spdlog.h>

namespace chronon3d::graph {

class EffectStackNode final : public RenderGraphNode {
public:
    explicit EffectStackNode(EffectStack effects, Frame cache_frame = Frame{-1})
        : m_effects(std::move(effects)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "EffectStack"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "effect_stack",
            .frame = m_cache_frame >= 0 ? m_cache_frame : (frame_dependent() ? ctx.frame.frame : Frame{0}),
            .width = ctx.frame.width,
            .height = ctx.frame.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        auto bbox = *input_bboxes[0];
        if (bbox.is_empty()) {
            return bbox;
        }
        const f32 spread = compute_max_effect_spread();
        if (spread <= 0.0f) {
            return bbox;
        }
        bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
        bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
        bbox.x1 = std::min(ctx.frame.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
        bbox.y1 = std::min(ctx.frame.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));
        
        if (ctx.options.diagnostics_enabled) {
            spdlog::info(
                "[EffectStackNode] input_bbox=({}, {})-({}, {}) spread={} output_bbox=({}, {})-({}, {})",
                input_bboxes[0]->x0,
                input_bboxes[0]->y0,
                input_bboxes[0]->x1,
                input_bboxes[0]->y1,
                spread,
                bbox.x0,
                bbox.y0,
                bbox.x1,
                bbox.y1
            );
        }

        if (bbox.is_empty()) {
            return bbox;
        }
        return bbox;
    }

    OwnedFB execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>> input_bboxes) override {
        if (inputs.empty() || !inputs[0]) {
            auto empty = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
            empty->clear(Color::transparent());
            return empty;
        }

        const f32 spread = compute_max_effect_spread();
        auto pred_bbox = predicted_bbox(ctx, input_bboxes);

        OwnedFB result;
        if (spread > 0.0f && pred_bbox) {
            const raster::BBox out_bounds = *pred_bbox;
            const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
            const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);
            
            result = ctx.acquire_owned_fb(out_w, out_h, true, out_bounds);
            
            const i32 intersect_x0 = std::max(inputs[0]->origin_x(), out_bounds.x0);
            const i32 intersect_y0 = std::max(inputs[0]->origin_y(), out_bounds.y0);
            const i32 intersect_x1 = std::min(inputs[0]->origin_x() + inputs[0]->width(), out_bounds.x1);
            const i32 intersect_y1 = std::min(inputs[0]->origin_y() + inputs[0]->height(), out_bounds.y1);
            
            if (intersect_x1 > intersect_x0 && intersect_y1 > intersect_y0) {
                const i32 w_copy = intersect_x1 - intersect_x0;
                for (i32 y = intersect_y0; y < intersect_y1; ++y) {
                    const Color* src = inputs[0]->pixels_row(y - inputs[0]->origin_y()) + (intersect_x0 - inputs[0]->origin_x());
                    Color* dst = result->pixels_row(y - out_bounds.y0) + (intersect_x0 - out_bounds.x0);
                    std::copy_n(src, w_copy, dst);
                }
            }
            
            result->set_opaque(inputs[0]->is_opaque());
            result->set_key_digest(inputs[0]->key_digest());
        } else {
            result = ctx.acquire_owned_fb(*inputs[0]);
        }

        if (ctx.resources.backend) {
            std::optional<raster::BBox> local_clip = ctx.tile.clip_rect;
            if (pred_bbox) {
                if (local_clip) {
                    local_clip->x0 = std::max(local_clip->x0, pred_bbox->x0);
                    local_clip->y0 = std::max(local_clip->y0, pred_bbox->y0);
                    local_clip->x1 = std::min(local_clip->x1, pred_bbox->x1);
                    local_clip->y1 = std::min(local_clip->y1, pred_bbox->y1);
                } else {
                    local_clip = pred_bbox;
                }
            }
            ctx.resources.backend->apply_effect_stack(*result, m_effects, ctx.frame.time_seconds, local_clip);
            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = static_cast<uint64_t>(ctx.frame.width * ctx.frame.height);
                if (local_clip) {
                    raster::BBox clipped = *local_clip;
                    clipped.clip_to(ctx.frame.width, ctx.frame.height);
                    if (!clipped.is_empty()) {
                        area = static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
                    } else {
                        area = 0;
                    }
                }
                ctx.telemetry.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }
        return result;
    }

    // ── Accessors / mutators for graph optimization ────────────────────
    [[nodiscard]] const EffectStack& effects() const { return m_effects; }
    [[nodiscard]] EffectStack& effects() { return m_effects; }

    /// Returns true if the effect stack contains only color/tonal effects
    /// (no blur, glow, shadow, bloom, or other spatially-expanding effects).
    /// Used by the graph builder for skip-when-opaque analysis.
    [[nodiscard]] bool is_color_only() const { return compute_max_effect_spread() <= 0.0f; }

    /// Prepend `prefix` effects before this node's own effects.
    /// Used by the optimizer to merge an upstream EffectStackNode into this one.
    void prepend_effects(const EffectStack& prefix) {
        m_effects.insert(m_effects.begin(), prefix.begin(), prefix.end());
    }

private:
    [[nodiscard]] f32 compute_max_effect_spread() const {
        f32 max_spread = 0.0f;
        for (const auto& inst : m_effects) {
            if (!inst.enabled) continue;

            using enum effects::EffectType;
            switch (inst.effect_type) {
            case Blur: {
                if (auto* p = std::get_if<BlurParams>(&inst.params))
                    max_spread = std::max(max_spread, p->radius);
                break;
            }
            case DropShadow: {
                if (auto* p = std::get_if<DropShadowParams>(&inst.params))
                    max_spread = std::max(max_spread,
                        std::max(std::abs(p->offset.x), std::abs(p->offset.y)) + p->radius);
                break;
            }
            case Glow: {
                if (auto* p = std::get_if<GlowParams>(&inst.params))
                    max_spread = std::max(max_spread, glow_effect_extent(*p));
                break;
            }
            case Bloom: {
                if (auto* p = std::get_if<BloomParams>(&inst.params))
                    max_spread = std::max(max_spread, p->radius);
                break;
            }
            case Fake3DWave: {
                if (auto* p = std::get_if<Fake3DWaveParams>(&inst.params)) {
                    f32 s = p->depth_px + p->amplitude_px;
                    if (p->shadow_enabled) {
                        s += std::max(std::abs(p->shadow_offset.x),
                                      std::abs(p->shadow_offset.y)) + p->shadow_blur;
                    }
                    if (p->expand_bounds) {
                        max_spread = std::max(max_spread, s);
                    }
                }
                break;
            }
            default: break;
            }
        }
        // Add 2px safety margin for anti-aliasing fringes
        return max_spread > 0.0f ? max_spread + 2.0f : 0.0f;
    }

    EffectStack m_effects;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph
