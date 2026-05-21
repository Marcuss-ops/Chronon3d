#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace chronon3d::renderer {
    chronon3d::raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread = 0.0f);
}

namespace chronon3d::graph {

namespace detail {

template <size_t N>
[[nodiscard]] inline std::optional<raster::BBox> project_points_bbox(
    const renderer::ProjectionContext& projection,
    const std::array<Vec3, N>& points,
    i32 width,
    i32 height,
    f32 padding = 0.0f
) {
    if (!projection.ready) {
        return std::nullopt;
    }

    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();

    for (const auto& point : points) {
        const auto projected = projection.project_point(point);
        if (!projected.visible) {
            return std::nullopt;
        }
        min_x = std::min(min_x, projected.screen.x);
        min_y = std::min(min_y, projected.screen.y);
        max_x = std::max(max_x, projected.screen.x);
        max_y = std::max(max_y, projected.screen.y);
    }

    raster::BBox bbox{
        static_cast<i32>(std::floor(min_x - padding)),
        static_cast<i32>(std::floor(min_y - padding)),
        static_cast<i32>(std::ceil(max_x + padding)),
        static_cast<i32>(std::ceil(max_y + padding))
    };
    bbox.clip_to(width, height);
    if (bbox.is_empty()) {
        return std::nullopt;
    }
    return bbox;
}

[[nodiscard]] inline std::optional<raster::BBox> projected_native_3d_bbox(
    const RenderGraphContext& ctx,
    const ::chronon3d::RenderNode& node,
    const Mat4& world_matrix,
    f32 spread = 0.0f
) {
    if (!ctx.has_camera_2_5d || !ctx.projection_ctx.ready) {
        return std::nullopt;
    }

    const auto expand = [spread](f32 value) {
        return value + spread;
    };

    switch (node.shape.type) {
        case ShapeType::FakeBox3D: {
            const auto& s = node.shape.fake_box3d;
            const Vec3 center = s.world_pos;
            const f32 hw = expand(s.size.x * 0.5f);
            const f32 hh = expand(s.size.y * 0.5f);
            const f32 depth = expand(s.depth);
            const std::array<Vec3, 8> corners = {{
                {center.x - hw, center.y + hh, center.z},
                {center.x + hw, center.y + hh, center.z},
                {center.x + hw, center.y - hh, center.z},
                {center.x - hw, center.y - hh, center.z},
                {center.x - hw, center.y + hh, center.z + depth},
                {center.x + hw, center.y + hh, center.z + depth},
                {center.x + hw, center.y - hh, center.z + depth},
                {center.x - hw, center.y - hh, center.z + depth},
            }};

            std::array<Vec3, 8> transformed{};
            for (size_t i = 0; i < corners.size(); ++i) {
                const Vec4 w = world_matrix * Vec4(corners[i], 1.0f);
                transformed[i] = {w.x, w.y, w.z};
            }
            return project_points_bbox(ctx.projection_ctx, transformed, ctx.width, ctx.height, spread);
        }

        case ShapeType::GridPlane: {
            const auto& s = node.shape.grid_plane;
            const f32 extent = expand(s.extent);
            std::array<Vec3, 4> corners{};
            if (s.axis == PlaneAxis::XZ) {
                corners = {{
                    {s.world_pos.x - extent, s.world_pos.y, s.world_pos.z - extent},
                    {s.world_pos.x + extent, s.world_pos.y, s.world_pos.z - extent},
                    {s.world_pos.x + extent, s.world_pos.y, s.world_pos.z + extent},
                    {s.world_pos.x - extent, s.world_pos.y, s.world_pos.z + extent},
                }};
            } else {
                corners = {{
                    {s.world_pos.x - extent, s.world_pos.y - extent, s.world_pos.z},
                    {s.world_pos.x + extent, s.world_pos.y - extent, s.world_pos.z},
                    {s.world_pos.x + extent, s.world_pos.y + extent, s.world_pos.z},
                    {s.world_pos.x - extent, s.world_pos.y + extent, s.world_pos.z},
                }};
            }

            std::array<Vec3, 4> transformed{};
            for (size_t i = 0; i < 4; ++i) {
                const Vec4 w = world_matrix * Vec4(corners[i], 1.0f);
                transformed[i] = {w.x, w.y, w.z};
            }
            return project_points_bbox(ctx.projection_ctx, transformed, ctx.width, ctx.height, spread);
        }

        default:
            break;
    }

    return std::nullopt;
}

} // namespace detail

// ClearNode (Output/Start)
class ClearNode final : public RenderGraphNode {
public:
    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Output; }
    std::string name() const override { return "Clear"; }

    bool cacheable() const override { return false; }

    std::optional<raster::BBox> predicted_bbox(const RenderGraphContext& ctx) const override {
        if (ctx.clip_rect) {
            return *ctx.clip_rect;
        }
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

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
        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(ctx.backend);
        bool use_dirty_rects = sw_renderer && ctx.dirty_rect.has_value() && sw_renderer->m_prev_framebuffer;
        
        if (use_dirty_rects) {
            auto fb = sw_renderer->m_prev_framebuffer;
            if (ctx.counters) {
                ctx.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t area = ctx.clip_rect
                    ? static_cast<uint64_t>(std::max(0, ctx.clip_rect->x1 - ctx.clip_rect->x0)) *
                      static_cast<uint64_t>(std::max(0, ctx.clip_rect->y1 - ctx.clip_rect->y0))
                    : static_cast<uint64_t>(ctx.width) * static_cast<uint64_t>(ctx.height);
                ctx.counters->clear_pixels.fetch_add(area, std::memory_order_relaxed);
            }
            fb->clear(Color::transparent(), ctx.clip_rect);
            return fb;
        } else {
            auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height, true);
            return fb;
        }
    }
};

// SourceNode
class SourceNode final : public RenderGraphNode {
public:
    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key,
               bool centered = false, bool is_3d = false, std::optional<Mat4> matrix_override = std::nullopt,
               std::optional<f32> opacity_override = std::nullopt, bool cache_static = false)
        : m_name(std::move(name)), m_node(node), m_key(key), m_centered(centered), m_is_3d(is_3d),
          m_matrix_override(matrix_override), m_opacity_override(opacity_override), m_cache_static(cache_static) {}

    bool cacheable() const override { return m_cache_static; }

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        const std::vector<std::optional<raster::BBox>>& = {}
    ) const override {
        const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
        const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

        Mat4 matrix;
        if (m_centered) {
            matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
        } else {
            matrix = ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
        }

        f32 spread = 0.0f;
        if (m_node.shadow.enabled) {
            spread = std::max(spread, m_node.shadow.radius + std::max(std::abs(m_node.shadow.offset.x), std::abs(m_node.shadow.offset.y)));
        }
        if (m_node.glow.enabled) {
            spread = std::max(spread, m_node.glow.radius);
        }
        spread += 8.0f;

        if (ctx.has_camera_2_5d &&
            (m_node.shape.type == ShapeType::FakeBox3D || m_node.shape.type == ShapeType::GridPlane)) {
            const Mat4 world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
            if (auto bbox = detail::projected_native_3d_bbox(ctx, m_node, world_matrix, spread)) {
                return bbox;
            }
            return raster::BBox{0, 0, ctx.width, ctx.height};
        }

        auto bbox = renderer::compute_world_bbox(m_node.shape, matrix, spread);
        bbox.clip_to(ctx.width, ctx.height);
        if (bbox.is_empty()) {
            return std::nullopt;
        }
        return bbox;
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
        bool clear = true;
        if (m_node.shape.type == ShapeType::Image && !m_matrix_override.has_value() && !m_centered) {
            const auto& t = m_node.world_transform;
            const auto& img = m_node.shape.image;
            if (t.position == Vec3(0.0f) && t.rotation == Quat(1.0f, 0.0f, 0.0f, 0.0f) && t.scale == Vec3(1.0f) &&
                t.opacity >= 0.999f && img.opacity >= 0.999f &&
                std::abs(img.size.x - static_cast<f32>(ctx.width)) < 1e-3f &&
                std::abs(img.size.y - static_cast<f32>(ctx.height)) < 1e-3f) {
                clear = false;
            }
        }

        auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height, clear);
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
                    state.matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
                } else {
                    // Legacy path or absolute rendering
                    state.matrix = ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
                }
            }
            
            state.opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
            state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
            state.clip_rect = ctx.clip_rect;
            
            // Expose projection context to processors that use it directly (FakeBox3D etc.).
            if (ctx.has_camera_2_5d) {
                state.projection  = ctx.projection_ctx;
            }

            ctx.backend->draw_node(*fb, m_node, state, ctx.camera, ctx.width, ctx.height);
            fb->set_opaque(can_seed_full_frame(ctx));

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
                    "[source-debug] node='{}' shape={} nonzero_pixels={} opacity={:.3f} matrix_tx={:.3f} matrix_ty={:.3f} det2d={:.6f}",
                    m_name,
                    static_cast<int>(m_node.shape.type),
                    nonzero_pixels,
                    state.opacity,
                    state.matrix[3][0],
                    state.matrix[3][1],
                    glm::determinant(glm::mat3(
                        state.matrix[0][0], state.matrix[0][1], state.matrix[0][3],
                        state.matrix[1][0], state.matrix[1][1], state.matrix[1][3],
                        state.matrix[3][0], state.matrix[3][1], state.matrix[3][3]
                    ))
                );
            }
        }
        return fb;
    }

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const override {
        if (!m_cache_static || m_is_3d) {
            return false;
        }

        if (m_matrix_override && *m_matrix_override != Mat4(1.0f)) {
            return false;
        }

        if (m_node.shape.type != ShapeType::Image) {
            return false;
        }

        const auto& img = m_node.shape.image;
        const auto& tr = m_node.world_transform;
        constexpr f32 eps = 1e-3f;

        if (ctx.clip_rect) {
            const bool clip_is_full = ctx.clip_rect->x0 <= 0 && ctx.clip_rect->y0 <= 0 &&
                                      ctx.clip_rect->x1 >= ctx.width && ctx.clip_rect->y1 >= ctx.height;
            if (!clip_is_full) {
                return false;
            }
        }

        const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.width)) < eps &&
                               std::abs(img.size.y - static_cast<f32>(ctx.height)) < eps;
        const bool opaque = img.opacity >= 0.999f && tr.opacity >= 0.999f;
        const bool identity = tr.position == Vec3(0.0f) &&
                              tr.rotation == Quat(1.0f, 0.0f, 0.0f, 0.0f) &&
                              tr.scale == Vec3(1.0f);

        return full_size && opaque && identity;
    }

private:
    std::string m_name;
    ::chronon3d::RenderNode m_node;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
    bool m_cache_static{false};
};

// MaskNode
class MaskNode final : public RenderGraphNode {
public:
    MaskNode(Mask mask, Frame cache_frame = Frame{-1})
        : m_mask(std::move(mask)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Mask; }
    std::string name() const override { return "Mask"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        const std::vector<std::optional<raster::BBox>>& input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        return input_bboxes[0];
    }

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
        if (!m_mask.enabled()) {
            return result;
        }

        const auto cache_key = mask_alpha_cache_key(result->width(), result->height(), ctx.modular_coordinates);
        if (!m_alpha_cache || m_alpha_cache_key != cache_key) {
            m_alpha_cache = build_alpha_cache(result->width(), result->height(), ctx.modular_coordinates);
            m_alpha_cache_key = cache_key;
        }

        if (!m_alpha_cache) {
            return result;
        }

        const int W = result->width();
        const int H = result->height();
        for (i32 y = 0; y < H; ++y) {
            Color* dst_row = result->pixels_row(y);
            const Color* mask_row = m_alpha_cache->pixels_row(y);
            for (i32 x = 0; x < W; ++x) {
                dst_row[x].a *= mask_row[x].a;
            }
        }
        return result;
    }

private:
    [[nodiscard]] std::shared_ptr<Framebuffer> build_alpha_cache(i32 width, i32 height, bool modular_coordinates) const {
        if (!modular_coordinates) {
            return rasterize_mask_alpha(m_mask, Mat4{1.0f}, width, height);
        }

        auto fb = std::make_shared<Framebuffer>(width, height);
        fb->clear(Color::transparent());

        const f32 cx = static_cast<f32>(width) * 0.5f;
        const f32 cy = static_cast<f32>(height) * 0.5f;
        for (i32 y = 0; y < height; ++y) {
            Color* row = fb->pixels_row(y);
            for (i32 x = 0; x < width; ++x) {
                if (mask_contains_local_point(m_mask, Vec2{static_cast<f32>(x) - cx, static_cast<f32>(y) - cy})) {
                    row[x] = {1.0f, 1.0f, 1.0f, 1.0f};
                }
            }
        }
        return fb;
    }

    [[nodiscard]] std::uint64_t mask_alpha_cache_key(i32 width, i32 height, bool modular_coordinates) const {
        auto mix = [](std::uint64_t seed, std::uint64_t value) {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        };

        std::uint64_t seed = hash_mask(m_mask);
        seed = mix(seed, static_cast<std::uint64_t>(width));
        seed = mix(seed, static_cast<std::uint64_t>(height));
        seed = mix(seed, static_cast<std::uint64_t>(modular_coordinates));
        seed = mix(seed, static_cast<std::uint64_t>(m_cache_frame >= 0 ? m_cache_frame : -1));
        return seed;
    }

    Mask m_mask;
    Frame m_cache_frame{-1};
    mutable std::shared_ptr<Framebuffer> m_alpha_cache;
    mutable std::uint64_t m_alpha_cache_key{0};
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
    bool cacheable() const override { return m_cache_frame >= 0; }

    CompositeNode(::chronon3d::BlendMode mode, Frame cache_frame = Frame{-1}) : m_mode(mode), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Composite; }
    std::string name() const override { return "Composite"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        const std::vector<std::optional<raster::BBox>>& input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        if (input_bboxes.size() == 1) return input_bboxes[0];

        auto bottom = input_bboxes[0];
        auto top = input_bboxes[1];
        if (!bottom) return top;
        if (!top) return bottom;
        // Both inputs have valid (known) but empty bounding boxes.
        // Return a valid empty bbox (not nullopt) so the dirty-rect system
        // knows the output is empty rather than falling back to a full-frame render.
        if (bottom->is_empty() && top->is_empty()) {
            const i32 x = std::min(bottom->x0, top->x0);
            const i32 y = std::min(bottom->y0, top->y0);
            return raster::BBox{x, y, x, y};
        }

        raster::BBox union_box;
        union_box.x0 = std::min(bottom->x0, top->x0);
        union_box.y0 = std::min(bottom->y0, top->y0);
        union_box.x1 = std::max(bottom->x1, top->x1);
        union_box.y1 = std::max(bottom->y1, top->y1);
        return union_box;
    }

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
            std::optional<raster::BBox> clip = (input_bboxes.size() >= 2) ? input_bboxes[1] : std::nullopt;
            if (ctx.clip_rect) {
                if (clip) {
                    clip = raster::BBox{
                        .x0 = std::max(clip->x0, ctx.clip_rect->x0),
                        .y0 = std::max(clip->y0, ctx.clip_rect->y0),
                        .x1 = std::min(clip->x1, ctx.clip_rect->x1),
                        .y1 = std::min(clip->y1, ctx.clip_rect->y1)
                    };
                    if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                        clip = raster::BBox{0, 0, 0, 0};
                    }
                } else {
                    clip = ctx.clip_rect;
                }
            }
            ctx.backend->composite_layer(*result, *top, m_mode, clip);
            if (ctx.counters) {
                ctx.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = clip ? (static_cast<uint64_t>(std::max(0, clip->x1 - clip->x0)) * std::max(0, clip->y1 - clip->y0))
                                     : static_cast<uint64_t>(ctx.width * ctx.height);
                if (m_mode == BlendMode::Normal && top->is_opaque()) {
                    ctx.counters->clear_copy_pixels.fetch_add(area, std::memory_order_relaxed);
                } else {
                    ctx.counters->composite_pixels.fetch_add(area, std::memory_order_relaxed);
                }
            }
        }
        return result;
    }

private:
    BlendMode m_mode;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph
