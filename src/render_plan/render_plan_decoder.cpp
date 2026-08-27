#include <chronon3d/render_plan/render_plan.hpp>

#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace chronon3d::render_plan {
namespace {

std::uint64_t fingerprint_render_plan_impl(const RenderPlan& plan,
                                          bool include_output_settings) {
    auto hash = chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-plan.fingerprint.v2")
        // job_id is runtime routing metadata, not content identity.
        .add("")
        .add(plan.style_profile)
        .add(plan.canvas.width)
        .add(plan.canvas.height)
        .add(plan.canvas.fps.num())
        .add(plan.canvas.fps.den())
        .add(plan.canvas.duration)
        // Temporal resource policy changes the effective render identity.
        .add(plan.budget.max_temporal_pixels)
        .add(plan.layers.size());

    for (const auto& layer : plan.layers) {
        hash.add(layer.id).add_enum(layer.type).add(layer.asset).add(layer.source)
            .add(layer.text).add(layer.font).add(layer.preset)
            .add(layer.semantic_role)
            .add(layer.font_size.has_value());
        if (layer.font_size) hash.add(*layer.font_size);
        hash.add(layer.font_asset.has_value());
        if (layer.font_asset) {
            hash.add(layer.font_asset->asset).add(layer.font_asset->family)
                .add(layer.font_asset->weight.has_value());
            if (layer.font_asset->weight) hash.add(*layer.font_asset->weight);
        }
        hash.add(layer.box_width.has_value());
        if (layer.box_width) hash.add(*layer.box_width);
        hash.add(layer.box_height.has_value());
        if (layer.box_height) hash.add(*layer.box_height);
        for (const auto value : layer.color) hash.add(value);
        for (const auto value : layer.position) hash.add(value);
        hash.add(layer.position_dimensions).add(layer.start_frame.has_value());
        if (layer.start_frame) hash.add(*layer.start_frame);
        hash.add(layer.duration_frames.has_value());
        if (layer.duration_frames) hash.add(*layer.duration_frames);
        hash.add(layer.fit.has_value());
        if (layer.fit) hash.add_enum(*layer.fit);
        hash.add(layer.subtitle_format.has_value());
        if (layer.subtitle_format) hash.add_enum(*layer.subtitle_format);
        hash.add(layer.animation.has_value());
        if (layer.animation) {
            hash.add(layer.animation->start_frame.has_value());
            if (layer.animation->start_frame) hash.add(*layer.animation->start_frame);
            hash.add(layer.animation->duration_frames.has_value());
            if (layer.animation->duration_frames) hash.add(*layer.animation->duration_frames);
            hash.add(layer.animation->preset).add(layer.animation->unit);
            hash.add(layer.animation->enter_duration_frames.has_value());
            if (layer.animation->enter_duration_frames)
                hash.add(*layer.animation->enter_duration_frames);
            hash.add(layer.animation->exit_duration_frames.has_value());
            if (layer.animation->exit_duration_frames)
                hash.add(*layer.animation->exit_duration_frames);
        }
        hash.add(layer.anchor.has_value());
        if (layer.anchor) {
            hash.add(layer.anchor->type).add(layer.anchor->safe_margin)
                .add(layer.anchor->alignment);
        }
        for (const auto value : layer.offset) hash.add(value);
        hash.add(layer.offset_dimensions);
        hash.add(layer.style.has_value());
        if (layer.style) {
            const auto& style = *layer.style;
            hash.add(style.font_family).add(style.font_weight.has_value());
            if (style.font_weight) hash.add(*style.font_weight);
            hash.add(style.font_size.has_value());
            if (style.font_size) hash.add(*style.font_size);
            hash.add(style.fill);
            hash.add(style.stroke.has_value());
            if (style.stroke) {
                hash.add(style.stroke->color).add(style.stroke->width.has_value());
                if (style.stroke->width) hash.add(*style.stroke->width);
            }
            hash.add(style.shadow.has_value());
            if (style.shadow) {
                hash.add(style.shadow->color).add(style.shadow->opacity.has_value());
                if (style.shadow->opacity) hash.add(*style.shadow->opacity);
                hash.add(style.shadow->blur.has_value());
                if (style.shadow->blur) hash.add(*style.shadow->blur);
                for (const auto value : style.shadow->offset) hash.add(value);
                hash.add(style.shadow->offset_dimensions);
            }
            hash.add(style.background.has_value());
            if (style.background) {
                hash.add(style.background->color)
                    .add(style.background->opacity.has_value());
                if (style.background->opacity) hash.add(*style.background->opacity);
                hash.add(style.background->radius.has_value());
                if (style.background->radius) hash.add(*style.background->radius);
                for (const auto value : style.background->padding) hash.add(value);
                hash.add(style.background->padding_dimensions);
            }
        }
        hash.add(layer.blend_mode.has_value());
        if (layer.blend_mode) hash.add_enum(*layer.blend_mode);
        hash.add(layer.opacity.has_value());
        if (layer.opacity) hash.add(*layer.opacity);
        hash.add(layer.loop);
        hash.add(layer.background.has_value());
        if (layer.background) {
            hash.add(layer.background->asset);
            hash.add(layer.background->fit.has_value());
            if (layer.background->fit) hash.add_enum(*layer.background->fit);
            hash.add(layer.background->opacity.has_value());
            if (layer.background->opacity) hash.add(*layer.background->opacity);
        }
    }

    // Output paths are always excluded because they identify a machine-local
    // destination. Request identity may retain the deterministic output
    // encoding settings; content identity must not.
    if (include_output_settings) {
        hash.add_enum(plan.output.format)
            .add_enum(plan.output.codec)
            .add(plan.output.bitrate)
            .add(plan.output.crf);
    }
    return hash.finish();
}

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

bool invalid_logical_path(const std::string& value) {
    if (value.empty()) return false;
    const std::filesystem::path path{value};
    if (path.is_absolute()) return true;
    for (const auto& component : path) {
        if (component == std::filesystem::path("..")) return true;
    }
    return false;
}

LayerType layer_type(const std::string& value) {
    if (value == "image") return LayerType::Image;
    if (value == "video") return LayerType::Video;
    if (value == "text") return LayerType::Text;
    if (value == "subtitle_track") return LayerType::SubtitleTrack;
    return LayerType::Color;
}

// Derive the layer primitive from a visual preset's supported_layer. This is
// the single place `type` is recovered when the plan omits it (ADR-029:
// PipelineGen stops transporting `type` for preset-driven layers).
LayerType visual_layer_type(chronon3d::registry::VisualLayerKind kind) {
    using chronon3d::registry::VisualLayerKind;
    switch (kind) {
        case VisualLayerKind::Image: return LayerType::Image;
        case VisualLayerKind::Video: return LayerType::Video;
        case VisualLayerKind::Text: return LayerType::Text;
        case VisualLayerKind::Color: return LayerType::Color;
    }
    return LayerType::Color;
}

FitMode fit_mode(const std::string& value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
}

std::optional<BlendMode> blend_mode(const std::string& value) {
    if (value == "normal") return BlendMode::Normal;
    if (value == "add") return BlendMode::Add;
    if (value == "multiply") return BlendMode::Multiply;
    if (value == "screen") return BlendMode::Screen;
    if (value == "overlay") return BlendMode::Overlay;
    if (value == "darken") return BlendMode::Darken;
    if (value == "lighten") return BlendMode::Lighten;
    if (value == "difference") return BlendMode::Difference;
    if (value == "exclusion") return BlendMode::Exclusion;
    if (value == "soft_light") return BlendMode::SoftLight;
    if (value == "hard_light") return BlendMode::HardLight;
    if (value == "color_dodge") return BlendMode::ColorDodge;
    if (value == "color_burn") return BlendMode::ColorBurn;
    return std::nullopt;
}

SubtitleFormat subtitle_format(const std::string& value) {
    if (value == "vtt") return SubtitleFormat::Vtt;
    if (value == "json") return SubtitleFormat::Json;
    if (value == "ass") return SubtitleFormat::Ass;
    return SubtitleFormat::Srt;
}

OutputFormat output_format(const std::string& value) {
    if (value == "mp4") return OutputFormat::Mp4;
    if (value == "mkv") return OutputFormat::Mkv;
    if (value == "webm") return OutputFormat::WebM;
    return OutputFormat::Png;
}

VideoCodec video_codec(const std::string& value) {
    if (value == "h264") return VideoCodec::H264;
    if (value == "h265") return VideoCodec::H265;
    if (value == "vp9") return VideoCodec::VP9;
    if (value == "av1") return VideoCodec::AV1;
    return VideoCodec::Auto;
}

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    // `type` is optional: preset-driven layers omit it and Chronon derives it
    // from the preset's supported_layer. A layer with neither an explicit type
    // nor a resolvable preset is invalid (fail-closed).
    if (value.contains("type")) {
        layer.type = layer_type(value.at("type").get<std::string>());
    } else {
        const std::string preset = value.value("preset", std::string{});
        if (preset.empty()) {
            throw std::runtime_error(
                "layer '" + layer.id + "' has no type and no preset to derive one from");
        }
        const auto& registry = chronon3d::registry::builtin_visual_preset_registry();
        if (!registry.contains(preset)) {
            throw std::runtime_error(
                "layer '" + layer.id + "' preset '" + preset +
                "' is unknown (cannot derive layer type)");
        }
        layer.type = visual_layer_type(registry.get(preset).supported_layer);
    }
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});
    layer.font = value.value("font", std::string{});
    if (value.contains("font_asset")) {
        const auto& font_asset = value.at("font_asset");
        FontAssetPlan asset_plan;
        asset_plan.asset = font_asset.value("asset", std::string{});
        asset_plan.family = font_asset.value("family", std::string{});
        asset_plan.weight = optional_value<int>(font_asset, "weight");
        layer.font_asset = std::move(asset_plan);
        // The canonical path field stays authoritative for the compiler; the
        // richer object form supplies family/weight metadata.
        if (layer.font.empty()) layer.font = layer.font_asset->asset;
    }
    layer.preset = value.value("preset", std::string{});
    layer.semantic_role = value.value("semantic_role", std::string{});
    layer.font_size = optional_value<float>(value, "font_size");
    layer.box_width = optional_value<float>(value, "box_width");
    layer.box_height = optional_value<float>(value, "box_height");
    if (value.contains("color")) {
        const auto& color = value.at("color");
        for (std::size_t index = 0; index < color.size() && index < layer.color.size(); ++index)
            layer.color[index] = color.at(index).get<float>();
    }
    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t index = 0; index < position.size(); ++index)
            layer.position[index] = position.at(index).get<float>();
    }
    if (value.contains("offset")) {
        const auto& offset = value.at("offset");
        layer.offset_dimensions = offset.size();
        for (std::size_t index = 0; index < offset.size() && index < layer.offset.size(); ++index)
            layer.offset[index] = offset.at(index).get<float>();
    }
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    if (value.contains("format"))
        layer.subtitle_format = subtitle_format(value.at("format").get<std::string>());
    if (value.contains("animation")) {
        const auto& animation = value.at("animation");
        AnimationTiming timing;
        timing.start_frame = optional_frame(animation, "start_frame");
        timing.duration_frames = optional_frame(animation, "duration_frames");
        timing.preset = animation.at("preset").get<std::string>();
        timing.unit = animation.value("unit", std::string{});
        if (animation.contains("enter"))
            timing.enter_duration_frames =
                optional_frame(animation.at("enter"), "duration_frames");
        if (animation.contains("exit"))
            timing.exit_duration_frames =
                optional_frame(animation.at("exit"), "duration_frames");
        layer.animation = std::move(timing);
    }
    if (value.contains("anchor")) {
        const auto& anchor = value.at("anchor");
        AnchorPlan anchor_plan;
        anchor_plan.type = anchor.value("type", std::string{});
        anchor_plan.safe_margin = anchor.value("safe_margin", 0.06f);
        anchor_plan.alignment = anchor.value("alignment", std::string{"left"});
        layer.anchor = std::move(anchor_plan);
    }
    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font_family = style.value("font_family", std::string{});
        style_plan.font_weight = optional_value<int>(style, "font_weight");
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{
                stroke.value("color", std::string{}),
                optional_value<float>(stroke, "width")};
        }
        if (style.contains("shadow")) {
            const auto& shadow = style.at("shadow");
            ShadowStyle shadow_plan;
            shadow_plan.color = shadow.value("color", std::string{});
            shadow_plan.opacity = optional_value<float>(shadow, "opacity");
            shadow_plan.blur = optional_value<float>(shadow, "blur");
            if (shadow.contains("offset")) {
                const auto& offset = shadow.at("offset");
                shadow_plan.offset_dimensions = offset.size();
                for (std::size_t i = 0; i < offset.size() && i < shadow_plan.offset.size(); ++i)
                    shadow_plan.offset[i] = offset.at(i).get<float>();
            }
            style_plan.shadow = std::move(shadow_plan);
        }
        if (style.contains("background")) {
            const auto& background = style.at("background");
            BackgroundStyle background_plan;
            background_plan.color = background.value("color", std::string{});
            background_plan.opacity = optional_value<float>(background, "opacity");
            background_plan.radius = optional_value<float>(background, "radius");
            if (background.contains("padding")) {
                const auto& padding = background.at("padding");
                background_plan.padding_dimensions = padding.size();
                for (std::size_t i = 0; i < padding.size() && i < background_plan.padding.size(); ++i)
                    background_plan.padding[i] = padding.at(i).get<float>();
            }
            style_plan.background = std::move(background_plan);
        }
        layer.style = std::move(style_plan);
    }
    if (value.contains("background")) {
        const auto& background = value.at("background");
        LayerBackgroundPlan background_plan;
        background_plan.asset = background.value("asset", std::string{});
        if (background.contains("fit"))
            background_plan.fit = fit_mode(background.at("fit").get<std::string>());
        background_plan.opacity = optional_value<float>(background, "opacity");
        layer.background = std::move(background_plan);
    }
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>())) {
            layer.blend_mode = mode;
        }
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    return layer;
}

}  // namespace

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
            if (layer.background->opacity &&
                !std::isfinite(*layer.background->opacity))
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

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    const auto validation = validate_render_plan(root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        RenderPlan plan;
        plan.job_id = root.value("job_id", std::string{"chronon_plan"});
        plan.style_profile = root.value("style_profile", std::string{"discovery"});
        if (plan.style_profile != "discovery" && plan.style_profile != "young" &&
            plan.style_profile != "crime") {
            return PlanDecodeError{"style_profile", "must be discovery, young, or crime"};
        }
        const auto& canvas = root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            FrameRate{canvas.at("fps_num").get<int>(), canvas.at("fps_den").get<int>()},
            Frame{canvas.at("duration_frames").get<std::int64_t>()}};
        for (const auto& layer : root.at("layers")) {
            auto decoded_layer = decode_layer(layer);
            if (invalid_logical_path(decoded_layer.asset)) {
                return PlanDecodeError{"layers[].asset",
                    "asset references must be relative logical paths"};
            }
            if (invalid_logical_path(decoded_layer.source)) {
                return PlanDecodeError{"layers[].source",
                    "source references must be relative logical paths"};
            }
            if (decoded_layer.background &&
                invalid_logical_path(decoded_layer.background->asset)) {
                return PlanDecodeError{"layers[].background.asset",
                    "background references must be relative logical paths"};
            }
            if (invalid_logical_path(decoded_layer.font)) {
                return PlanDecodeError{"layers[].font",
                    "font references must be relative logical paths"};
            }
            plan.layers.push_back(std::move(decoded_layer));
        }
        const auto& output = root.at("output");
        plan.output.path = output.at("path").get<std::string>();
        if (output.contains("format"))
            plan.output.format = output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            plan.output.codec = video_codec(output.at("codec").get<std::string>());
        plan.output.bitrate = output.value("bitrate", std::int64_t{0});
        plan.output.crf = output.value("crf", 0);
        plan.output.qp = output.value("qp", -1);
        const auto rate_control = output.value("rate_control", "crf");
        if (rate_control == "qp") plan.output.rate_control = render_plan::RateControlMode::ConstantQp;
        else if (rate_control == "bitrate") plan.output.rate_control = render_plan::RateControlMode::Bitrate;
        else if (rate_control == "crf") plan.output.rate_control = render_plan::RateControlMode::Crf;
        else return PlanDecodeError{"output.rate_control", "must be crf, qp, or bitrate"};
        plan.output.profile_id = output.value("profile_id", std::string{});
        if (plan.output.profile_id == "velox-h264-1080p30-v1" &&
            (plan.canvas.width != 1920 || plan.canvas.height != 1080 || plan.canvas.fps != FrameRate{30, 1})) {
            return PlanDecodeError{"canvas", "velox-h264-1080p30-v1 requires 1920x1080 at 30 fps"};
        }
        if (root.contains("budget")) {
            const auto& budget = root.at("budget");
            plan.budget.max_temporal_pixels = budget.value(
                "max_temporal_pixels", plan.budget.max_temporal_pixels);
        }
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;
        plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
