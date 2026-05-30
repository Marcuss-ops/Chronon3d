#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <span>

namespace chronon3d::graph {

/**
 * TransformNode applies a spatial transformation to an input framebuffer.
 * Used for layer-level transforms and precompositions.
 */
class TransformNode final : public RenderGraphNode {
public:
    explicit TransformNode(Transform transform, SamplingMode mode = SamplingMode::Bilinear) 
        : m_transform(std::move(transform)), m_mode(mode), m_use_matrix(false) {}

    explicit TransformNode(Mat4 matrix, f32 opacity = 1.0f, SamplingMode mode = SamplingMode::Bilinear)
        : m_matrix(matrix), m_opacity(opacity), m_mode(mode), m_use_matrix(true) {}

    explicit TransformNode(Transform transform, Frame cache_frame, SamplingMode mode = SamplingMode::Bilinear)
        : m_transform(std::move(transform)), m_mode(mode), m_use_matrix(false), m_cache_frame(cache_frame) {}

    explicit TransformNode(Mat4 matrix, f32 opacity, Frame cache_frame, SamplingMode mode = SamplingMode::Bilinear)
        : m_matrix(matrix), m_opacity(opacity), m_mode(mode), m_use_matrix(true), m_cache_frame(cache_frame) {}

    [[nodiscard]] RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Transform; }
    [[nodiscard]] std::string name() const override { return "Transform"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override;

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        u64 params_hash = hash_combine(
            hash_transform(m_transform),
            static_cast<u64>(m_mode)
        );

        if (m_use_matrix) {
            params_hash = hash_combine(params_hash, hash_bytes(&m_matrix, sizeof(m_matrix)));
        }

        return cache::NodeCacheKey{
            .scope = "transform",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = params_hash
        };
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

    // ── Accessors for graph optimization ────────────────────────────
    [[nodiscard]] Mat4 matrix()  const { return m_use_matrix ? m_matrix : m_transform.to_mat4(); }
    [[nodiscard]] f32        opacity() const { return m_use_matrix ? m_opacity : m_transform.opacity; }
    [[nodiscard]] bool       is_identity() const {
        const Mat4& m = matrix();
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                const float expected = (i == j) ? 1.0f : 0.0f;
                if (std::abs(m[i][j] - expected) > 1e-6f) return false;
            }
        }
        return std::abs(opacity() - 1.0f) < 1e-6f;
    }

    // ── Mutators for graph optimization ─────────────────────────────
    void set_matrix(const Mat4& m)  { m_matrix = m; m_use_matrix = true; }
    void set_opacity(f32 o)         { m_opacity = o; m_use_matrix = true; }

private:
    Transform m_transform;
    Mat4      m_matrix{1.0f};
    f32       m_opacity{1.0f};
    SamplingMode m_mode{SamplingMode::Bilinear};
    bool      m_use_matrix{false};
    Frame     m_cache_frame{-1};
};

} // namespace chronon3d::graph
