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
    if (plan.output.rate_control == RateControlMode::Crf &&
        (plan.output.qp >= 0 || plan.output.bitrate > 0))
        return fail("output.rate_control", "CRF mode cannot combine QP or bitrate");
    if (plan.output.rate_control == RateControlMode::ConstantQp &&
        (plan.output.crf > 0 || plan.output.bitrate > 0 || plan.output.qp < 0))
        return fail("output.rate_control", "QP mode requires only a valid QP value");
    if (plan.output.rate_control == RateControlMode::Bitrate &&
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
        const auto prefix = "layers[" + std::to_string(index) + "]";
        if (layer.start_frame && layer.start_frame->integral() < 0)
            return fail(prefix + ".start_frame", "layer start frame cannot be negative");
        if (layer.duration_frames && layer.duration_frames->integral() <= 0)
            return fail(prefix + ".duration_frames", "layer duration must be positive");
        if (layer.start_frame && layer.start_frame->integral() >= duration_value)
            return fail(prefix + ".start_frame", "layer starts outside the composition duration");
        if (layer.duration_frames &&
            layer.duration_frames->integral() >
                duration_value - (layer.start_frame ? layer.start_frame->integral() : 0))
            return fail(prefix + ".duration_frames", "layer duration exceeds composition duration");

        if (!add_bytes(text_bytes, layer.text.size(), budget.max_text_bytes))
            return fail("layers[].text", "render budget max_text_bytes exceeded");
        for (const auto* reference : {&layer.asset, &layer.source, &layer.font}) {
            if (!add_bytes(asset_reference_bytes, reference->size(),
                           budget.max_asset_reference_bytes))
                return fail("layers[]", "render budget max_asset_reference_bytes exceeded");
        }

        for (const auto value : layer.size)
            if (const auto invalid = fail_if_non_finite(value, "layers[].size")) return invalid;
        for (const auto value : layer.position)
            if (const auto invalid = fail_if_non_finite(value, "layers[].position")) return invalid;
        for (const auto value : layer.scale)
            if (const auto invalid = fail_if_non_finite(value, "layers[].scale")) return invalid;
        for (const auto value : layer.rotation)
            if (const auto invalid = fail_if_non_finite(value, "layers[].rotation")) return invalid;
        if (layer.opacity)
            if (const auto invalid = fail_if_non_finite(*layer.opacity, "layers[].opacity")) return invalid;
        if (layer.style && layer.style->font_size)
            if (const auto invalid = fail_if_non_finite(*layer.style->font_size, "layers[].style.font_size")) return invalid;

        if (layer.animation) {
            for (const auto& track : layer.animation->tracks) {
                if (track.keyframes.empty())
                    return fail(prefix + ".animation.tracks", "tracks require at least one keyframe");
                Frame previous{-1};
                for (const auto& key : track.keyframes) {
                    if (key.frame.integral() < 0 || key.frame.integral() >= duration_value)
                        return fail(prefix + ".animation.tracks[].keyframes[].frame",
                                    "keyframe lies outside the composition duration");
                    if (previous.integral() >= 0 && key.frame < previous)
                        return fail(prefix + ".animation.tracks[].keyframes",
                                    "keyframes must be sorted by frame");
                    previous = key.frame;
                    if (key.value.empty() || key.value.size() > 4)
                        return fail(prefix + ".animation.tracks[].keyframes[].value",
                                    "keyframe value must contain 1 to 4 numeric components");
                    for (const auto component : key.value)
                        if (const auto invalid = fail_if_non_finite(component,
                                "layers[].animation.tracks[].keyframes[].value")) return invalid;
                }
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
