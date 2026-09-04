std::optional<raster::BBox> EffectStackNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    if (input_bboxes.empty() || !input_bboxes[0]) {
        return std::nullopt;
    }
    auto bbox = *input_bboxes[0];
    if (bbox.is_empty()) {
        // When the input bbox is empty (e.g. a degenerate shape whose
        // corners all project to w≈0, producing an inverted {max,max,min,min}
        // bbox that collapses to {0,0,0,0} after SourceNode clipping),
        // returning it as a valid BBox causes execute() to intersect it
        // with the clip rect → inverted local_clip → negative ROI
        // dimensions → acquire_temp_framebuffer crash.  Return nullopt
        // so execute() falls back to the full input framebuffer + the
        // executor-provided clip_rect, which is always well-formed.
        return std::nullopt;
    }
    const f32 spread = compute_max_effect_spread();
    if (spread <= 0.0f) {
        return bbox;
    }
    bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
    bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
    bbox.x1 = std::min(ctx.frame_input.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
    bbox.y1 = std::min(ctx.frame_input.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));

    if (ctx.policy.diagnostics_enabled) {
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

    // After spread expansion + clamping, the bbox could still be empty
    // (e.g. input was inverted and expansion didn't fix it).  Return
    // nullopt so execute() falls back to the full input framebuffer.
    if (bbox.is_empty()) {
        return std::nullopt;
    }
    return bbox;
}
