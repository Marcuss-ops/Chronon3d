#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <glm/glm.hpp>
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
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame.frame,
            .width = ctx.frame.width,
            .height = ctx.frame.height,
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
        return get_transform_type() == TransformType::Identity;
    }

    // ── Mutators for graph optimization ─────────────────────────────
    void set_matrix(const Mat4& m)  { m_matrix = m; m_use_matrix = true; invalidate_cache(); }
    void set_opacity(f32 o)         { m_opacity = o; m_use_matrix = true; invalidate_cache(); }

private:
    // ── Cached matrix analysis (avoids redundant 4×4 float comparisons ──
    //     on every execute() call — saves ~16×4×44 compares per composition)
    enum class TransformType : uint8_t {
        Unknown,
        Identity,
        PureTranslation,
        Affine,      // no perspective component
        Projective
    };

    mutable TransformType cached_type_{TransformType::Unknown};
    mutable i32            cached_tx_{0}, cached_ty_{0};  // cached integer translation (PureTranslation only)

    void invalidate_cache() const {
        cached_type_ = TransformType::Unknown;
    }

    TransformType analyze_matrix() const {
        const Mat4& m = m_use_matrix ? m_matrix : m_transform.to_mat4();
        
        // Check identity first (most common)
        bool id = true;
        for (int i = 0; i < 4 && id; ++i) {
            for (int j = 0; j < 4 && id; ++j) {
                const float expected = (i == j) ? 1.0f : 0.0f;
                if (std::abs(m[i][j] - expected) > 1e-6f) id = false;
            }
        }
        if (id && std::abs(opacity() - 1.0f) < 1e-6f)
            return TransformType::Identity;

        // Extract 3×3 homography (local_z = 0 plane)
        glm::mat3 H;
        H[0][0] = m[0][0]; H[0][1] = m[0][1]; H[0][2] = m[0][3];
        H[1][0] = m[1][0]; H[1][1] = m[1][1]; H[1][2] = m[1][3];
        H[2][0] = m[3][0]; H[2][1] = m[3][1]; H[2][2] = m[3][3];

        // Check pure translation (identity 2×2 + no perspective)
        const bool pure_trans =
            std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[0][1]) < 1e-6f && std::abs(H[0][2]) < 1e-6f &&
            std::abs(H[1][0]) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f && std::abs(H[1][2]) < 1e-6f &&
            std::abs(H[2][2] - 1.0f) < 1e-6f;

        if (pure_trans) {
            const f32 tx = H[2][0];
            const f32 ty = H[2][1];
            if (std::abs(tx - std::round(tx)) < 1e-4f && std::abs(ty - std::round(ty)) < 1e-4f) {
                cached_tx_ = static_cast<i32>(std::round(tx));
                cached_ty_ = static_cast<i32>(std::round(ty));
                return TransformType::PureTranslation;
            }
        }

        // Check affine (no perspective)
        if (std::abs(H[2][0]) < 1e-9f && std::abs(H[2][1]) < 1e-9f)
            return TransformType::Affine;

        return TransformType::Projective;
    }

    TransformType get_transform_type() const {
        if (cached_type_ == TransformType::Unknown)
            cached_type_ = analyze_matrix();
        return cached_type_;
    }

    Transform m_transform;
    Mat4      m_matrix{1.0f};
    f32       m_opacity{1.0f};
    SamplingMode m_mode{SamplingMode::Bilinear};
    bool      m_use_matrix{false};
    Frame     m_cache_frame{-1};
};

} // namespace chronon3d::graph
