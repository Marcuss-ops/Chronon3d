TextRunNode::TextRunNode(
    std::string name,
    std::string layer_id,
    std::shared_ptr<TextRunShape> shape,
    const ::chronon3d::RenderNode& render_ref,
    const cache::NodeCacheKey& key,
    TextRunPlacement placement,
    std::optional<f32> opacity_override,
    RenderNodeCachePolicy policy
)
    : RenderGraphNode(policy),
      m_name(std::move(name)),
      m_layer_id(std::move(layer_id)),
      m_shape(std::move(shape)),
      m_render_ref(render_ref),
      m_key(key),
      m_placement(placement),
      m_opacity_override(opacity_override)
{}

void TextRunNode::refresh_placement(
    const ::chronon3d::RenderNode& render_ref,
    TextRunPlacement placement,
    const cache::NodeCacheKey& key,
    std::optional<f32> opacity_override)
{
    m_render_ref = render_ref;
    m_placement = placement;
    m_key = key;
    m_opacity_override = opacity_override;
}

// =============================================================================
// predicted_bbox
// =============================================================================
//
// 2.5D-aware predicted bbox via renderer's `compute_text_run_world_bbox`.
// Matrix composition is delegated to `text_run::build_world_matrix()` so
// the bbox sampling and the rasterization dispatch agree on the canvas
// coordinate space by construction.

Mat4 TextRunNode::world_matrix(const RenderGraphContext& ctx) const {
    return text_run::build_world_matrix(ctx, m_placement);
}

std::optional<raster::BBox> TextRunNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> /*input_bboxes*/
) const {
    if (!m_shape) {
        return std::nullopt;
    }

    // A tight producer surface is itself the source-space contract consumed
    // by TransformNode. Returning the canvas-space ink bbox here would make
    // the transform interpret canvas coordinates as source pixels and would
    // reintroduce a full-frame/empty-input mismatch.
    if (m_placement.tight_surface &&
        m_placement.surface_size.x > 0.0f &&
        m_placement.surface_size.y > 0.0f) {
        return raster::BBox{
            0, 0,
            std::max(1, static_cast<i32>(std::ceil(m_placement.surface_size.x))),
            std::max(1, static_cast<i32>(std::ceil(m_placement.surface_size.y)))};
    }

    const Mat4 matrix = text_run::build_world_matrix(ctx, m_placement);

    f32 spread = 0.0f;
    spread += 8.0f;

#ifdef CHRONON3D_ENABLE_TEXT
    auto bbox = renderer::compute_text_run_world_bbox(*m_shape, matrix, spread);
#else
    auto bbox = renderer::compute_world_bbox(m_render_ref.shape, matrix, spread);
#endif

    // TICKET-TEXT-CLIP-PREDICTED-BBOX — contract violation guard (pre-clip).
    // The world matrix produced degenerate coordinates (e.g. the 403-px
    // residual offset documented in the ticket).  Conservative fallback
    // returns full canvas — same strategy as text_layout_debug below.
    // Each violation increments counters.text_bbox_contract_violations
    // (atomic, anti-false-share) so /api/runs diagnostics flags it.
    // NOTE: this guard runs *before* clip_to so a legitimately-off-canvas
    // layer (post-clip is_empty) hits the legacy BBox{0,0,0,0} cull path
    // below rather than the conservative fallback.
    const bool pre_clip_violation =
        !std::isfinite(bbox.x0) || !std::isfinite(bbox.y0) ||
        !std::isfinite(bbox.x1) || !std::isfinite(bbox.y1) ||
        bbox.is_empty();
    if (pre_clip_violation) {
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->text_bbox_contract_violations.fetch_add(
                1, std::memory_order_relaxed);
        }
        if (ctx.policy.diagnostics_enabled) {
            spdlog::debug("[text-bbox] CONTRACT_VIOLATION node={} bbox=({}, {}, {}, {})",
                          m_name, bbox.x0, bbox.y0, bbox.x1, bbox.y1);
        }
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    // TICKET-TEXT-CLIP-PREDICTED-BBOX — diagnostic logging.
    // Gated on diagnostics_enabled to avoid per-frame overhead in
    // production.  Logs the raw world bbox, the matrix translation,
    // and the placement matrix so the clipping root-cause can be
    // traced by comparing predicted_bbox vs actual alpha_bbox.
    if (ctx.policy.diagnostics_enabled) {
        const f32 mtx = matrix[3][0];
        const f32 mty = matrix[3][1];
        spdlog::debug(
            "[text-bbox] node={} "
            "world=({}, {}, {}, {}) "
            "w={} h={} "
            "matrix_tx={:.1f} matrix_ty={:.1f} "
            "spread={:.1f} "
            "canvas={}x{} "
            "placement[3][0]={:.1f} placement[3][1]={:.1f} "
            "op_override={}",
            m_name,
            bbox.x0, bbox.y0, bbox.x1, bbox.y1,
            bbox.x1 - bbox.x0, bbox.y1 - bbox.y0,
            mtx, mty,
            spread,
            ctx.frame_input.width, ctx.frame_input.height,
            m_placement.matrix[3][0], m_placement.matrix[3][1],
            m_opacity_override.has_value());
    }

    // When text_layout_debug is enabled, the overlay draws markers at
    // canvas center, layer origin, and alpha centroid — all outside the
    // tight text bbox.  Return full-canvas so the compositor doesn't clip
    // the overlay markers.
    if (ctx.policy.text_layout_debug) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    if (!ctx.policy.diagnostics_enabled) {
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
    }
    if (bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};  // legitimate post-clip cull
    }

    // TICKET-SIMPLICITY-CONSERVATIVE-BBOX — F1.C conservative fallback.
    // If predicted_bbox is suspiciously thin (height < 30% of font_size),
    // the world bbox computation likely under-estimated the glyph ink
    // extent.  This catches the class of bugs exemplified by the 19px-
    // sliver regression (TICKET-TEXT-CLIP-ASCENT) where a tight
    // predicted_bbox causes tile pruning to skip tiles that actually
    // contain visible text.  The font-size-proportional threshold avoids
    // false positives for small text on large canvases (e.g. 36pt caption
    // on 3840×2160).
    //
    // The primary defense is the post-render alpha_bbox expansion in
    // node_runner.cpp — this pre-render check is a safety net for cases
    // where the bbox is degenerate-thin before rendering even starts.
    if (m_shape) {
        const int bbox_h = std::max(0, bbox.y1 - bbox.y0);
        const int bbox_w = std::max(0, bbox.x1 - bbox.x0);
        // Read the canonical font_size from the shared layout (per
        // include/chronon3d/text/text_run_layout.hpp:104). The layout is
        // the SSOT for the per-run font_size; using it here makes the
        // conservative-bbox threshold proportional to the actual text
        // size (e.g. 88pt typewriters get 26px min-h, 16pt captions get
        // 5px min-h) instead of the previous constant 32.0f fallback that
        // mis-thresholded large and small text. Falls back to 32.0f for
        // degenerate shapes where layout is null or font_size is unset
        // (e.g. test fixtures, empty placeholders).
        const float font_size =
            (m_shape->layout && m_shape->layout->font_size > 0.0f)
                ? m_shape->layout->font_size
                : 32.0f;
        const int min_h = static_cast<int>(font_size * 0.3f);
        const int min_w = static_cast<int>(font_size * 0.5f);
        const bool suspiciously_thin =
            (bbox_h < min_h) || (bbox_w < min_w);
        if (suspiciously_thin) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->text_bbox_contract_violations.fetch_add(
                    1, std::memory_order_relaxed);
            }
            // TICKET-FIX-ALPHA-SCANNER-DUP-V1 — per-session warn-once
            // delegated to the canonical TU-local helper
            // `text_bbox_warn_once(...)` (declared in the anonymous
            // namespace at the top of this TU; Cat-3 anti-dup
            // consolidated: dedup logic declared ONCE).  No
            // `static bool` rot; no data race on parallel render; no
            // first-error-mask later invocations.  No string
            // duplication: the message format string is declared ONCE
            // at THIS call site (the grep-discoverable anchor); only
            // the dedup logic is shared via the helper.
            text_bbox_warn_once(
                ctx.node_exec.text_bbox_reporter,
                "[text-bbox] CONSERVATIVE_EXPAND node={} predicted=({}, {}, {}, {}) "
                "w={} h={} min=({}, {}) font_size={:.0f}",
                m_name, bbox.x0, bbox.y0, bbox.x1, bbox.y1,
                bbox_w, bbox_h, min_w, min_h, font_size);
            const int canvas_w = ctx.frame_input.width;
            const int canvas_h = ctx.frame_input.height;
            return raster::BBox{0, 0, canvas_w, canvas_h};
        }
    }

    // TICKET-SIMPLICITY-VISIBILITY-CONTRACT — §9 FU04 pre-render audit.
    // Calls `audit_text_visibility()` with the current tight bbox (no
    // framebuffer yet — alpha-bbox invariants deferred). If the audit
    // detects a `predicted_contains_world` violation (the math-side
    // contract fault that the FU01/FU03 guards don't catch), apply the
    // §9 violation response: substitute the `expanded_predicted_bbox`
    // (world_ink_bbox padded by `effect_padding` on all 4 sides) for
    // the original tight bbox. Logged via spdlog::warn + counter bump
    // so the telemetry surface surfaces the FU04 catch.
    //
    // Gated on CHRONON3D_BUILD_DIAGNOSTICS — in production builds the
    // tight bbox is returned unchanged (zero overhead). The expansion
    // is only computed in debug/diagnostic/inspect builds where the
    // audit is active.
    //
    // For TextRun nodes tile_pruning is already off by design (see
    // tile_pruning::compute_dirty_clip), so the "should_disable_tile_pruning"
    // flag is realized via the bbox expansion that guarantees
    // rasterization of all visible ink (the conceptual "disable" is the
    // guarantee that the tight bbox no longer under-estimates ink).
#if defined(CHRONON3D_BUILD_DIAGNOSTICS) && defined(CHRONON3D_ENABLE_TEXT)
    if (m_shape) {
        const Rect predicted_r{
            {static_cast<float>(bbox.x0), static_cast<float>(bbox.y0)},
            {static_cast<float>(bbox.x1 - bbox.x0),
             static_cast<float>(bbox.y1 - bbox.y0)}
        };
        const Rect local_ink_bbox = local_bounds_to_rect(
            renderer::compute_text_run_visual_bounds(*m_shape));
        (void)local_ink_bbox;  // post Step 2 fix (d), audit populates
                                // local_ink_bbox internally; the unused
                                // local is kept as belt-and-braces for
                                // future FU04 / FU11 work that re-reads
                                // the canonical accumulator result.
        const auto audit = audit_text_visibility(
            *m_shape, matrix, predicted_r, predicted_r,
            nullptr, spread);
        if (audit.should_disable_tile_pruning) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->text_bbox_contract_violations.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const auto& exp = audit.expanded_predicted_bbox;
            // TICKET-FIX-ALPHA-SCANNER-DUP-V1 — per-session warn-once
            // delegated to the canonical TU-local helper
            // `text_bbox_warn_once(...)` (declared in the anonymous
            // namespace at the top of this TU; one `warned` flag per
            // session is correct semantics since CONSERVATIVE_EXPAND
            // and FU04_EXPAND are mutually exclusive per-frame).
            // Cat-3 anti-dup consolidated: dedup logic shared via the
            // helper, message format string declared ONCE at THIS
            // call site (the grep-discoverable anchor).
            text_bbox_warn_once(
                ctx.node_exec.text_bbox_reporter,
                "[text-bbox] FU04_EXPAND node={} tight=({}, {}, {}, {}) "
                "expanded=({}, {}, {}, {}) effect_padding={:.1f}",
                m_name, bbox.x0, bbox.y0, bbox.x1, bbox.y1,
                exp.origin.x, exp.origin.y,
                exp.origin.x + exp.size.x, exp.origin.y + exp.size.y,
                spread);
            return raster::BBox{
                static_cast<int>(exp.origin.x),
                static_cast<int>(exp.origin.y),
                static_cast<int>(exp.origin.x + exp.size.x),
                static_cast<int>(exp.origin.y + exp.size.y)
            };
        }
    }
#endif

    return bbox;
}

// =============================================================================
// cache_key
// =============================================================================

cache::NodeCacheKey TextRunNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // Static text is reusable across frames; animated text is not.  The
    // cache policy is the semantic boundary, so do not bake the current
    // frame into a static TextRun key (that would create one full surface
    // entry per frame and defeat raster-cache reuse).  Frame-dependent
    // inputs such as an evaluated 2.5D camera are folded below separately.
    key.frame = cache_policy().frame_dependent()
        ? ctx.frame_input.frame
        : Frame{0};

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

    key.source_hash = hash_combine(
        key.source_hash,
        hash_combine(
            hash_string(m_name),
            hash_bytes(&m_render_ref.world_transform.position,
                       sizeof(Vec3))));

    key.params_hash = hash_combine(
        key.params_hash,
        static_cast<u64>(ctx.policy.modular_coordinates));
    // Tight projected text is rasterized in a local surface. The producer
    // intentionally discards the projected matrix and lets TransformNode
    // own camera projection, so matrix/camera state must not fragment the
    // semantic text-raster cache. Non-tight text still rasterizes in canvas
    // space and therefore retains the matrix in its key.
    if (!m_placement.tight_surface) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&m_placement.matrix[0][0], sizeof(Mat4)));
    }
    key.params_hash = hash_combine(
        key.params_hash,
        hash_bytes(&m_placement.surface_origin[0], sizeof(Vec2)));
    key.params_hash = hash_combine(
        key.params_hash,
        hash_bytes(&m_placement.surface_size[0], sizeof(Vec2)));
    key.params_hash = hash_combine(
        key.params_hash,
        static_cast<u64>(m_placement.tight_surface));
    if (m_opacity_override) {
        key.params_hash = hash_combine(
            key.params_hash,
            hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }

    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B):
    // canonical conditional fold. Tight-surface text is owned downstream by
    // TransformNode, so camera state must not fragment the raster cache.
    cache::fold_camera_if(
        key,
        ctx.frame_input.has_camera_2_5d && !m_placement.tight_surface,
        ctx.frame_input.camera_2_5d);

    return key;
}
