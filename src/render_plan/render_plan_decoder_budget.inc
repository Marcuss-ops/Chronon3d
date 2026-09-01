std::optional<PlanDecodeError> validate_render_budget(
    const RenderPlan& plan, const RenderBudget& budget) {
    const auto fail = [](std::string path, std::string message) {
        return std::optional<PlanDecodeError>{
            PlanDecodeError{std::move(path), std::move(message)}};
    };
    const auto fail_if_non_finite = [&](double value, const char* path) {
        return !std::isfinite(value) ? fail(path, "value must be finite")
                                     : std::optional<PlanDecodeError>{};
    };
    if (plan.canvas.width <= 0 ||
        static_cast<std::uint64_t>(plan.canvas.width) > budget.max_width)
        return fail("canvas.width", "render budget resolution width exceeded or is non-positive");
    if (plan.canvas.height <= 0 ||
        static_cast<std::uint64_t>(plan.canvas.height) > budget.max_height)
        return fail("canvas.height", "render budget resolution height exceeded or is non-positive");
    if (plan.canvas.fps.num() <= 0 || plan.canvas.fps.den() <= 0)
        return fail("canvas.fps", "frame rate must be positive");
    if (plan.output.bitrate < 0)
        return fail("output.bitrate", "bitrate cannot be negative");
    if (plan.output.crf < 0 || plan.output.crf > 51)
        return fail("output.crf", "CRF must be between 0 and 51");
    if (plan.output.qp < -1 || plan.output.qp > 63)
        return fail("output.qp", "QP must be between 0 and 63");
    if (plan.output.rate_control == render_plan::RateControlMode::Crf &&
        (plan.output.qp >= 0 || plan.output.bitrate > 0))
        return fail("output.rate_control", "CRF mode cannot combine QP or bitrate");
    if (plan.output.rate_control == render_plan::RateControlMode::ConstantQp &&
        (plan.output.crf > 0 || plan.output.bitrate > 0 || plan.output.qp < 0))
        return fail("output.rate_control", "QP mode requires only a valid QP value");
    if (plan.output.rate_control == render_plan::RateControlMode::Bitrate &&
        (plan.output.crf > 0 || plan.output.qp >= 0 || plan.output.bitrate <= 0))
        return fail("output.rate_control", "bitrate mode requires only a positive bitrate");
    if (plan.layers.size() > budget.max_layers)
        return fail("layers", "render budget max_layers exceeded");
    const auto duration_value = plan.canvas.duration.integral();
    if (duration_value <= 0)
        return fail("canvas.duration_frames", "duration must be positive");
    const auto frames = static_cast<std::uint64_t>(duration_value);
    if (frames > budget.max_frames)
        return fail("canvas.duration_frames", "render budget max_frames exceeded");

    const auto width = static_cast<std::uint64_t>(plan.canvas.width);
    const auto height = static_cast<std::uint64_t>(plan.canvas.height);
    if (width > budget.max_total_pixels / height)
        return fail("canvas", "render budget max_total_pixels exceeded");
    const auto total_pixels = width * height;

    std::uint64_t text_bytes = 0;
    std::uint64_t asset_reference_bytes = 0;
    const auto add_bytes = [](std::uint64_t& total, std::size_t value,
                              std::uint64_t limit) {
        if (total > limit || static_cast<std::uint64_t>(value) > limit - total)
            return false;
        total += static_cast<std::uint64_t>(value);
        return true;
    };
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        if (layer.start_frame && layer.start_frame->integral() < 0)
            return fail("layers[" + std::to_string(index) + "].start_frame",
                        "layer start frame cannot be negative");
        if (layer.duration_frames && layer.duration_frames->integral() <= 0)
            return fail("layers[" + std::to_string(index) + "].duration_frames",
                        "layer duration must be positive");
        if (layer.start_frame && layer.start_frame->integral() >= duration_value)
            return fail("layers[" + std::to_string(index) + "].start_frame",
                        "layer starts outside the composition duration");
        if (layer.duration_frames &&
            layer.duration_frames->integral() >
                duration_value - (layer.start_frame
                    ? layer.start_frame->integral() : 0))
            return fail("layers[" + std::to_string(index) + "].duration_frames",
                        "layer duration exceeds composition duration");
        if (layer.animation) {
            const auto animation_start = layer.animation->start_frame
                ? layer.animation->start_frame->integral() : 0;
            if (animation_start < 0 || animation_start >= duration_value)
                return fail("layers[" + std::to_string(index) + "].animation.start_frame",
                            "animation starts outside the composition duration");
            if (layer.animation->duration_frames &&
                (layer.animation->duration_frames->integral() <= 0 ||
                 layer.animation->duration_frames->integral() >
                     duration_value - animation_start))
                return fail("layers[" + std::to_string(index) + "].animation.duration_frames",
                            "animation duration exceeds composition duration");
        }
        if (!add_bytes(text_bytes, layer.text.size(), budget.max_text_bytes))
            return fail("layers[].text", "render budget max_text_bytes exceeded");
        for (const auto* reference : {&layer.asset, &layer.source, &layer.font}) {
            if (!add_bytes(asset_reference_bytes, reference->size(),
                           budget.max_asset_reference_bytes))
                return fail("layers[]", "render budget max_asset_reference_bytes exceeded");
        }
        if (layer.background) {
            if (layer.background->asset.empty())
                return fail("layers[" + std::to_string(index) + "].background.asset",
                            "background asset must not be empty");
            if (!add_bytes(asset_reference_bytes, layer.background->asset.size(),
                           budget.max_asset_reference_bytes))
                return fail("layers[].background.asset",
                            "render budget max_asset_reference_bytes exceeded");
            if (layer.background->opacity && !std::isfinite(*layer.background->opacity))
                return fail("layers[].background.opacity", "value must be finite");
        }
        for (const auto* numeric : {&layer.font_size, &layer.box_width,
                                    &layer.box_height}) {
            if (numeric->has_value()) {
                if (const auto invalid = fail_if_non_finite(
                        static_cast<double>(numeric->value()), "layers[].numeric"))
                    return invalid;
            }
        }
    }
    if (total_pixels > budget.max_peak_memory_bytes / 16)
        return fail("canvas", "render budget max_peak_memory_bytes exceeded");
    if (total_pixels > std::numeric_limits<std::uint64_t>::max() / 4 / frames)
        return fail("canvas", "render budget output estimate overflow");
    const auto estimated_output = total_pixels * 4 * frames;
    if (estimated_output > budget.max_estimated_output_bytes)
        return fail("canvas", "render budget max_estimated_output_bytes exceeded");
    return std::nullopt;
}

std::optional<PlanDecodeError> validate_render_plan_budget(
    const RenderPlan& plan, const RenderBudget& budget) {
    return validate_render_budget(plan, budget);
}

std::uint64_t compute_render_plan_content_fingerprint(const RenderPlan& plan) {
    return fingerprint_render_plan_impl(plan, false);
}
