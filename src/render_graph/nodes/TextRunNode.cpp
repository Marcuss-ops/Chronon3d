#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

// =============================================================================
// Constructor
// =============================================================================

TextRunNode::TextRunNode(
    std::string name,
    std::shared_ptr<TextRunShape> shape,
    const ::chronon3d::RenderNode& render_ref,
    const cache::NodeCacheKey& key,
    bool centered,
    bool uses_2_5d_projection,
    std::optional<Mat4> matrix_override,
    std::optional<f32> opacity_override,
    RenderNodeCachePolicy policy
)
    : RenderGraphNode(policy)
      m_name(std::move(name)),
      m_shape(std::move(shape)),
      m_render_ref(render_ref),
      m_key(key),
      m_centered(centered),
      m_uses_2_5d_projection(uses_2_5d_projection),
      m_matrix_override(std::move(matrix_override)),
      m_opacity_override(std::move(opacity_override))
{}

// =============================================================================
// predicted_bbox
// =============================================================================
//
// Mirrors SourceNode::predicted_bbox exactly except the world bbox is computed
// by `renderer::compute_text_run_world_bbox` (2.5D-aware — per-glyph rotation.x/y
// shears and scale.z expansion).  Returns nullopt when the shape is
// missing (defensive; source-pass only emits a TextRunNode when shape is set).

std::optional<raster::BBox> TextRunNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) const {
    if (!m_shape) {
        return std::nullopt;
    }

    // SSAA + canvas-centre transforms — identical to SourceNode so the bbox
    // sits in the same canvas coordinate space downstream.
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(
        ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(
        ctx.frame.width * 0.5f, ctx.frame.height * 0.5f, 0.0f));

    Mat4 matrix;
    if (m_uses_2_5d_projection || m_centered) {
        matrix = canvas_center
               * ssaa_scale
               * m_matrix_override.value_or(m_render_ref.world_transform.to_mat4());
    } else {
        matrix = ssaa_scale
               * m_matrix_override.value_or(m_render_ref.world_transform.to_mat4());
    }

    // Spread is contributed by blur + stroke + 8px safety padding.
    // Plus any node-level shadow/glow plumbing carried on the RenderNode.
    f32 spread = 0.0f;
    if (m_render_ref.shadow.enabled) {
        spread = std::max(spread,
            m_render_ref.shadow.radius
            + std::max(std::abs(m_render_ref.shadow.offset.x),
                       std::abs(m_render_ref.shadow.offset.y)));
    }
    if (m_render_ref.glow.enabled) {
        spread = std::max(spread, m_render_ref.glow.radius);
    }
    spread += 8.0f;

    // 2.5D-aware bbox.  Inside `compute_text_run_world_bbox`:
    //   - glyph layout_position + position offsets
    //   - per-glyph rotation.y → horizontal shear expansion
    //   - per-glyph rotation.x → vertical shear expansion
    //   - per-glyph scale.z → uniform expansion
    //   - blur + stroke_width + spread padding
    auto bbox = renderer::compute_text_run_world_bbox(*m_shape, matrix, spread);

    if (!ctx.options.diagnostics_enabled) {
        bbox.clip_to(ctx.frame.width, ctx.frame.height);
    }
    if (bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};
    }
    return bbox;
}

// =============================================================================
// cache_key
// =============================================================================
//
// Combines:
//   - skeleton key from source pass (scope, frame, size)
//   - `hash_text_run_shape(*m_shape)` — covers immutable layout + per-glyph
//     animated state + material/paint/shadows.  Layout-hash component is
//     stable across frames of the same text (so the layout cache hits),
//     while the per-glyph state hash invalidates for animated frames.
//   - placement hash from the RenderNode (name + position)
//   - matrix/opacity override bytes (modular-coordinates path)
//   - 2.5D camera transform (when projected) so a moving camera does not
//     serve a stale TextRun frame.

cache::NodeCacheKey TextRunNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;

    // Content: layout (cached) + animated glyph state + material
    if (m_shape) {
        key.params_hash = hash_combine(
            key.params_hash,
            chronon3d::hash_text_run_shape(*m_shape));
    } else {
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(0xdeadbeef));
    }

    // Placement: node name + position (mirrors SourceNode).
    key.source_hash = hash_combine(
        key.source_hash,
        hash_combine(
            hash_string(m_name),
            hash_bytes(&m_render_ref.world_transform.position,
                       sizeof(Vec3))));

    // Modular-coordinate overrides
    key.params_hash = hash_combine(
        key.params_hash,
        static_cast<u64>(ctx.options.modular_coordinates));
    if (m_matrix_override) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
    }
    if (m_opacity_override) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }

    // 2.5D camera transform — invalidate when the camera moves so the bg
    // matrix inside compute_text_run_world_bbox is up-to-date.
    if (m_uses_2_5d_projection && ctx.camera.has_camera_2_5d) {
        const auto& cam = ctx.camera.camera_2_5d;
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.position, sizeof(Vec3)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.rotation, sizeof(Vec3)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.zoom, sizeof(f32)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.fov_deg, sizeof(f32)));
        if (cam.point_of_interest_enabled) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.point_of_interest, sizeof(Vec3)));
        }
    }

    return key;
}

// =============================================================================
// execute
// =============================================================================
//
// Route through the SoftwareTextRunProcessor (`renderer::draw_text_run`).
// We resolve the SoftwareRenderer via dynamic_cast on the RenderBackend
// (matches the matte sub-pipeline pattern from
// graph_builder_layer_pipeline_pass.cpp).

OwnedFB TextRunNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> /*inputs*/,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) {
    CHRONON_ZONE_C("text_run_render", trace_category::kRasterize);

    if (!m_shape) {
        // Defensive: source pass only emits a TextRunNode when shape is set,
        // but if a future caller constructs an empty one, return a black frame.
        return ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height, /*clear=*/true);
    }

    // Acquire full-canvas framebuffer (no clear-skip — text can't fill a frame).
    auto fb = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height, /*clear=*/true);

    // Resolve backend and dispatch through the virtual draw_text_run.
    auto* backend = ctx.resources.backend;
    if (!backend) {
        if (!m_backend_warned) {
            spdlog::error(
                "[text-run] node='{}' cannot render: backend is null; "
                "returning cleared fb.", m_name);
            m_backend_warned = true;
        }
        return fb;
    }

    // Build the world-space transform that mirrors SourceNode's state.matrix.
    // draw_text_run only consumes model[3][0..1] for translation today
    // (future: extend to full model-matrix transform); we still pass
    // the full matrix so the future path has the data.
    Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(
        ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
    Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(
        ctx.frame.width * 0.5f, ctx.frame.height * 0.5f, 0.0f));

    Mat4 world_matrix;
    if (m_uses_2_5d_projection || m_centered) {
        world_matrix = canvas_center
                     * ssaa_scale
                     * m_matrix_override.value_or(m_render_ref.world_transform.to_mat4());
    } else {
        world_matrix = ssaa_scale
                     * m_matrix_override.value_or(m_render_ref.world_transform.to_mat4());
    }

    const f32 opacity = m_opacity_override.value_or(m_render_ref.world_transform.opacity);

    // PR2 — gate on capabilities first so the fast path (text features
    // present) is a single bool check and only the slow path logs a
    //    startup-leak-style warning via m_backend_warned.
    if (!backend->capabilities().text_run) {
        if (!m_backend_warned) {
            spdlog::error(
                "[text-run] node='{}' backend does not support "
                "draw_text_run; returning cleared fb.", m_name);
            m_backend_warned = true;
        }
        return fb;
    }

    auto result = backend->draw_text_run(
        *fb, *m_shape, world_matrix, opacity);

    if (!result) {
        spdlog::error(
            "[text-run] node='{}' draw_text_run failed: [{}] {}",
            m_name,
            chronon3d::graph::render_backend_error_code_name(result.error().code),
            result.error().message);
    }

    if (ctx.options.diagnostics_enabled) {
        // DEBUG (not INFO): this fires every frame.  The diagnostic-mode
        // spammy output is much better at debug level — users who want it
        // explicitly opt in via Chronon log-level configuration.
        spdlog::debug(
            "[text-run] node='{}' shape_hash=0x{:016x} glyphs={} drew={} "
            "opacity={:.3f} tx={:.1f} ty={:.1f}",
            m_name,
            chronon3d::hash_text_run_shape(*m_shape),
            m_shape->glyphs.size(),
            result ? result.value().items_drawn : 0u,
            opacity,
            world_matrix[3][0],
            world_matrix[3][1]
        );
    }

    // NOTE: draw_text_run() already increments text_glyphs_rasterized
    // inside the processor.  Do NOT double-count here — the processor is
    // the single source of truth for telemetry.

    return fb;
}

} // namespace chronon3d::graph
