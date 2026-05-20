#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

// ClearNode (Output/Start)
class ClearNode final : public RenderGraphNode {
public:
    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Output; }
    std::string name() const override { return "Clear"; }

    bool cacheable() const override { return false; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "clear",
            .frame = 0,
            .width = ctx.width,
            .height = ctx.height
        };
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>&,
        const std::vector<std::optional<raster::BBox>>&
    ) override {
        auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height);
        // acquire_framebuffer already returns a cleared buffer from the pool
        if (ctx.counters) {
            ctx.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
            ctx.counters->clear_pixels.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
        }
        return fb;
    }
};

// SourceNode
class SourceNode final : public RenderGraphNode {
public:
    bool cacheable() const override { return false; }

    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key,
               bool centered = false, bool is_3d = false, std::optional<Mat4> matrix_override = std::nullopt,
               std::optional<f32> opacity_override = std::nullopt)
        : m_name(std::move(name)), m_node(node), m_key(key), m_centered(centered), m_is_3d(is_3d), 
          m_matrix_override(matrix_override), m_opacity_override(opacity_override) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(const RenderGraphContext& ctx) const override {
        // For standard source nodes, we assume full frame for now.
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override { 
        auto key = m_key;
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.modular_coordinates));
        if (m_matrix_override) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
        }
        if (m_opacity_override) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
        }
        return key; 
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>&,
        const std::vector<std::optional<raster::BBox>>&
    ) override {
        auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height);
        // acquire_framebuffer already returns a cleared buffer from the pool
        if (ctx.counters) {
            ctx.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
            ctx.counters->clear_pixels.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
        }

        if (ctx.backend) {
            RenderState state;
            const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
            const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

            if (m_is_3d) {
                // Projected 3D layers: draw shapes at their local position relative to layer origin.
                // TransformNode handles layer-level projection (position, rotation, perspective).
                // Native-3D shapes (FakeBox3D etc.) ignore state.matrix and use the projector directly.
                state.matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
            } else {
                if (m_centered) {
                    // Modular 2D also uses center-origin logic for consistency.
                    // The relative transform is handled by TransformNode.
                    Mat4 center_adj = Mat4(1.0f);
                    if (m_node.shape.type == ShapeType::Image &&
                        std::abs(m_node.shape.image.size.x - ctx.width) < 1.0f &&
                        std::abs(m_node.shape.image.size.y - ctx.height) < 1.0f &&
                        std::abs(m_node.world_transform.position.x) < 1.0f &&
                        std::abs(m_node.world_transform.position.y) < 1.0f) {
                        center_adj = math::translate(Vec3(-ctx.width * 0.5f, -ctx.height * 0.5f, 0.0f));
                    }
                    state.matrix = canvas_center * ssaa_scale * center_adj * m_matrix_override.value_or(m_node.world_transform.to_mat4());
                } else {
                    // Legacy path or absolute rendering
                    state.matrix = ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
                }
            }
            
            state.opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
            state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
            
            // Expose projection context to processors that use it directly (FakeBox3D etc.).
            if (ctx.has_camera_2_5d) {
                state.projection  = ctx.projection_ctx;
            }

            ctx.backend->draw_node(*fb, m_node, state, ctx.camera, ctx.width, ctx.height);

            if (ctx.diagnostics_enabled) {
                int nonzero_pixels = 0;
                for (i32 y = 0; y < fb->height(); ++y) {
                    const Color* row = fb->pixels_row(y);
                    for (i32 x = 0; x < fb->width(); ++x) {
                        const Color& c = row[x];
                        if (c.a > 0.001f || c.r > 0.001f || c.g > 0.001f || c.b > 0.001f) {
                            ++nonzero_pixels;
                        }
                    }
                }

                spdlog::info(
                    "[source-debug] node='{}' shape={} nonzero_pixels={} opacity={:.3f} matrix_tx={:.3f} matrix_ty={:.3f}",
                    m_name,
                    static_cast<int>(m_node.shape.type),
                    nonzero_pixels,
                    state.opacity,
                    state.matrix[3][0],
                    state.matrix[3][1]
                );
            }
        }
        return fb;
    }

private:
    std::string m_name;
    ::chronon3d::RenderNode m_node;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
};

// MaskNode
class MaskNode final : public RenderGraphNode {
public:
    MaskNode(Mask mask, Frame cache_frame = Frame{-1})
        : m_mask(std::move(mask)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Mask; }
    std::string name() const override { return "Mask"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mask",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_mask(m_mask)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs, const std::vector<std::optional<raster::BBox>>&) override {
        if (inputs.empty()) return ctx.acquire_framebuffer(ctx.width, ctx.height);

        auto result = ctx.acquire_framebuffer(*inputs[0]);

        f32 cx = 0.0f;
        f32 cy = 0.0f;
        if (ctx.modular_coordinates) {
            cx = static_cast<f32>(result->width()) * 0.5f;
            cy = static_cast<f32>(result->height()) * 0.5f;
        }

        for (i32 y = 0; y < result->height(); ++y) {
            Color* row = result->pixels_row(y);
            for (i32 x = 0; x < result->width(); ++x) {
                Vec2 local{static_cast<f32>(x) - cx, static_cast<f32>(y) - cy};
                if (!mask_contains_local_point(m_mask, local))
                    row[x].a = 0.0f;
            }
        }
        return result;
    }

private:
    Mask m_mask;
    Frame m_cache_frame{-1};
};

// EffectStackNode
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
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs, const std::vector<std::optional<raster::BBox>>&) override {
        if (inputs.empty()) return ctx.acquire_framebuffer(ctx.width, ctx.height);
        
        auto result = ctx.acquire_framebuffer(*inputs[0]);
        if (ctx.backend) {
            ctx.backend->apply_effect_stack(*result, m_effects, ctx.time_seconds, ctx.clip_rect);
            if (ctx.counters) {
                ctx.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.counters->effect_pixels.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
            }
        }
        return result;
    }

private:
    EffectStack m_effects;
    Frame m_cache_frame{-1};
};

// AdjustmentNode
class AdjustmentNode final : public RenderGraphNode {
public:
    explicit AdjustmentNode(EffectStack effects) : m_effects(std::move(effects)) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Adjustment; }
    std::string name() const override { return "Adjustment"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "adjustment",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs, const std::vector<std::optional<raster::BBox>>&) override {
        if (inputs.empty()) return ctx.acquire_framebuffer(ctx.width, ctx.height);
        
        auto result = ctx.acquire_framebuffer(*inputs[0]);
        if (ctx.backend) {
            ctx.backend->apply_effect_stack(*result, m_effects, ctx.time_seconds, ctx.clip_rect);
            if (ctx.counters) {
                ctx.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.counters->effect_pixels.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
            }
        }
        return result;
    }

private:
    EffectStack m_effects;
};

// CompositeNode
class CompositeNode final : public RenderGraphNode {
public:
    bool cacheable() const override { return false; }

    CompositeNode(::chronon3d::BlendMode mode, Frame cache_frame = Frame{-1}) : m_mode(mode), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Composite; }
    std::string name() const override { return "Composite"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "composite",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = static_cast<u64>(m_mode)
        };
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>& inputs,
        const std::vector<std::optional<raster::BBox>>& input_bboxes
    ) override {
        if (inputs.size() < 2) return inputs.empty() ? ctx.acquire_framebuffer(ctx.width, ctx.height) : inputs[0];
        
        auto bottom = inputs[0];
        auto top = inputs[1];
        
        std::shared_ptr<Framebuffer> result;
        // Optimization: In-place composition if we are the unique owner of the bottom buffer
        if (ctx.optimize_compositing && bottom.use_count() == 1 && bottom->width() == ctx.width && bottom->height() == ctx.height) {
            result = bottom;
        } else {
            result = ctx.acquire_framebuffer(*bottom);
        }

        if (ctx.backend) {
            // Optimization: Only composite the area where the top node actually drew something
            const auto& clip = (input_bboxes.size() >= 2) ? input_bboxes[1] : std::nullopt;
            ctx.backend->composite_layer(*result, *top, m_mode, clip);
            if (ctx.counters) {
                ctx.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = clip ? (static_cast<uint64_t>(clip->x1 - clip->x0) * (clip->y1 - clip->y0))
                                     : static_cast<uint64_t>(ctx.width * ctx.height);
                ctx.counters->composite_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }
        return result;
    }

private:
    BlendMode m_mode;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph
