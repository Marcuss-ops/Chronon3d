#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run_driver.hpp>
#endif
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
    : RenderGraphNode(policy),
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
        ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(
        ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));

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
#ifdef CHRONON3D_ENABLE_TEXT
    auto bbox = renderer::compute_text_run_world_bbox(*m_shape, matrix, spread);
#else
    auto bbox = renderer::compute_world_bbox(m_render_ref.shape, matrix, spread);
#endif

    if (!ctx.policy.diagnostics_enabled) {
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
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
//   - `hash_text_run_shape(*m_shape, ctx.frame_input.sample_time)` — covers
//     immutable layout + per-glyph animated state + material/paint/
//     shadows, AND (PR 10) the AnimatedTextDocument state at the
//     current sample time (transition type + transition_text +
//     morph_map + mix).  This guarantees Scramble / Morph /
//     CrossfadeLayouts frames invalidate the executor's per-frame
//     node cache correctly — without the sample-time fold a
//     transitioning shape would share a stale entry across frames.
//   - placement hash from the RenderNode (name + position)
//   - matrix/opacity override bytes (modular-coordinates path)
//   - 2.5D camera transform (when projected) so a moving camera does not
//     serve a stale TextRun frame.

cache::NodeCacheKey TextRunNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;

    // Content: layout (cached) + animated glyph state + material +
    // PR 10 AnimatedTextDocument state at the current integral frame.
    //
    // The frame overload folds transition_type + active->utf8 +
    // active->defaults.font + transition_text bytes + morph_map bytes +
    // mix into the cache key.  This guarantees Scramble / Morph /
    // CrossfadeLayouts / font-swap Cut frames invalidate the
    // executor's per-frame node cache correctly.
#ifdef CHRONON3D_ENABLE_TEXT
    if (m_shape) {
        key.params_hash = hash_combine(
            key.params_hash,
            chronon3d::hash_text_run_shape(
                *m_shape,
                ctx.frame_input.sample_time.integral_frame()));
    } else {
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(0xdeadbeef));
    }
#else
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(0xdeadbeef));
#endif

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
        static_cast<u64>(ctx.policy.modular_coordinates));
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
    if (m_uses_2_5d_projection && ctx.frame_input.has_camera_2_5d) {
        const auto& cam = ctx.frame_input.camera_2_5d;
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

NodeExecResult TextRunNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> /*inputs*/,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) {
    CHRONON_ZONE_C("text_run_render", trace_category::kRasterize);

    if (!m_shape) {
        return NodeExecResult{ctx.acquire_owned_fb(
            ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true)};
    }

    // ── PR 8 wire-up ────────────────────────────────────────────────
    // Re-evaluate the AE-style animator stack per frame, writing per-glyph
    // state back into m_shape->glyphs.  Cheap (no re-shaping); the cache
    // key below already folds `hash_text_run_shape(*m_shape,
    // ctx.frame_input.sample_time)` so animated frames invalidate the stale
    // entry automatically.  No-op when `shape->animators` is empty
    // (static layout).
    //
    // PR 10 NOTE: cache-key/pre-mutation ordering is closed.  The
    // transition_text / morph_map fold inside `hash_text_run_shape`'s
    // sample-time overload ensures Scramble / Morph frames produce
    // distinct cache keys.  The wire-up is therefore safe to call before
    // the executor evaluates `cache_key()` — the order is intentionally
    // mutated-after-cache-key-fetch because the hash overload mirrors
    // the post-mutation layout contents.
#ifdef CHRONON3D_ENABLE_TEXT
    chronon3d::update_text_run_shape_per_frame(*m_shape, ctx.frame_input.sample_time);
#endif

    // Acquire full-canvas framebuffer (no clear-skip — text can't fill a frame).
    auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);

    // Resolve backend and dispatch through the virtual draw_text_run.
    auto* backend = ctx.services.backend;
    if (!backend) {
        spdlog::error(
            "[text-run] node='{}' cannot render: backend is null; "
            "returning cleared fb.", m_name);
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "backend is null"
        }};
    }

    // Build the world-space transform that mirrors SourceNode's state.matrix.
    // draw_text_run only consumes model[3][0..1] for translation today
    // (future: extend to full model-matrix transform); we still pass
    // the full matrix so the future path has the data.
    Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(
        ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(
        ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));

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
    //    startup-leak-style warning.
    if (!backend->capabilities().text_run) {
        spdlog::error(
            "[text-run] node='{}' backend does not support "
            "draw_text_run; returning cleared fb.", m_name);
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability,
            m_name,
            "backend does not support draw_text_run"
        }};
    }

    auto result = backend->draw_text_run(
        *fb, *m_shape, world_matrix, opacity);

    if (!result) {
        spdlog::error(
            "[text-run] node='{}' draw_text_run failed: [{}] {}",
            m_name,
            chronon3d::graph::render_backend_error_code_name(result.error().code),
            result.error().message);
        return NodeExecResult{NodeExecutionError{
            result.error().code,
            m_name,
            result.error().message
        }};
    }

    if (ctx.policy.diagnostics_enabled) {
        // DEBUG (not INFO): this fires every frame.  The diagnostic-mode
        // spammy output is much better at debug level — users who want it
        // explicitly opt in via Chronon log-level configuration.
        // PR 10: log the frame overload so the diagnostic value matches
        // the actual cache key the executor used.
#ifdef CHRONON3D_ENABLE_TEXT
        spdlog::debug(
            "[text-run] node='{}' shape_hash=0x{:016x} glyphs={} drew={} "
            "opacity={:.3f} tx={:.1f} ty={:.1f}",
            m_name,
            chronon3d::hash_text_run_shape(
                *m_shape,
                ctx.frame_input.sample_time.integral_frame()),
            m_shape->glyphs.size(),
            result ? result.value().items_drawn : 0u,
            opacity,
            world_matrix[3][0],
            world_matrix[3][1]
        );
#else
        spdlog::debug(
            "[text-run] node='{}' glyphs={} drew={} "
            "opacity={:.3f} tx={:.1f} ty={:.1f}",
            m_name,
            m_shape->glyphs.size(),
            result ? result.value().items_drawn : 0u,
            opacity,
            world_matrix[3][0],
            world_matrix[3][1]
        );
#endif
    }

    // NOTE: draw_text_run() already increments text_glyphs_rasterized
    // inside the processor.  Do NOT double-count here — the processor is
    // the single source of truth for telemetry.

    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph
