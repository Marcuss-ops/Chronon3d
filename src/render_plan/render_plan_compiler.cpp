#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/effects/cube_lut.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <cmath>
#include <string>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>

#include "render_plan_compiler_detail.hpp"
#include "effect_kind_coverage.hpp"
#include "audio_react_baker.hpp"
#include "audio_react_plan_baker.hpp"
#include "render_plan_effect_preparation.hpp"

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include "../scene/camera/camera_v1/camera_program_sources.hpp"
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_path_composer.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace chronon3d::render_plan {

namespace {
// ── media background card pass ─────────────────────────────────────────────
//
// A media layer (video/image) may declare the rounded, padded plate behind its
// box through style.background — the very field text layers already use for
// their card, declared by the same published style contract. This pass
// materialises it as a real Shape layer placed immediately BEFORE the media
// layer, so the plate is composited under the clip:
//
//   - the plate is a colour layer carrying the border colour and radius_px
//     (a flat colour plate honours the top-level `radius` corner contract), so
//     it composites through the proven centred-plate geometry;
//   - the plate's size is the layer's DISPLAYED box (its declared size, or the
//     canvas when it declares none) plus 2*padding: the padding IS the visible
//     frame thickness;
//   - its corner radius is style.background.radius (the frame's outer radius),
//     while the media layer keeps its own `radius` (the concentric inner one);
//   - the shadow moves to the plate. The plate IS the frame, so a shadow kept
//     on the clip underneath would be painted over by the opaque plate and
//     leak past it by exactly the frame thickness; the single shadow the
//     contract declares belongs to the card;
//   - the media layer keeps size/fit/opacity/radius untouched.
//   - the plate follows the media layer's resolved box placement; a full-canvas
//     source clip's centered scale and explicit positioned media both retain
//     their authored transform semantics.
//
// Runs before any per-layer index table (resolved media paths, prepared
// effects, content fingerprint) is built, so plan.layers stays the single
// index space for the rest of the compile.
void materialize_media_background_cards(RenderPlan& plan) {
    bool any_card = false;
    for (const auto& layer : plan.layers) {
        if ((layer.type == LayerType::Video || layer.type == LayerType::Image) &&
            layer.style && layer.style->background) {
            any_card = true;
            break;
        }
    }
    if (!any_card) return;

    std::vector<LayerPlan> expanded;
    expanded.reserve(plan.layers.size() * 2);
    for (auto& layer : plan.layers) {
        const bool media = layer.type == LayerType::Video || layer.type == LayerType::Image;
        if (!media || !layer.style || !layer.style->background) {
            expanded.push_back(std::move(layer));
            continue;
        }
        const auto& background = *layer.style->background;
        const auto parsed_color = parse_hex_color(background.color, 1.0f);
        if (!parsed_color)
            throw std::runtime_error("layer '" + layer.id +
                "' background colour '" + background.color + "' is not #RRGGBB");
        const float opacity = background.opacity.value_or(1.0f);
        if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
            throw std::runtime_error(
                "layer '" + layer.id + "' background opacity is outside [0,1]");
        const float padding_x = background.padding_dimensions == 2
            ? background.padding[0] : 0.0f;
        const float padding_y = background.padding_dimensions == 2
            ? background.padding[1] : 0.0f;
        if (!std::isfinite(padding_x) || !std::isfinite(padding_y) ||
            padding_x < 0.0f || padding_y < 0.0f)
            throw std::runtime_error("layer '" + layer.id +
                "' background padding must be finite and non-negative");

        const float scale_x = std::abs(layer.scale[0]) > 1e-6f
            ? std::abs(layer.scale[0]) : 1.0f;
        const float scale_y = std::abs(layer.scale[1]) > 1e-6f
            ? std::abs(layer.scale[1]) : 1.0f;
        const float box_w = (layer.size_dimensions == 2
            ? layer.size[0] : static_cast<float>(plan.canvas.width)) * scale_x;
        const float box_h = (layer.size_dimensions == 2
            ? layer.size[1] : static_cast<float>(plan.canvas.height)) * scale_y;
        const float card_w = box_w + 2.0f * padding_x;
        const float card_h = box_h + 2.0f * padding_y;
        if (!(card_w > 0.0f) || !(card_h > 0.0f) ||
            !std::isfinite(card_w) || !std::isfinite(card_h))
            throw std::runtime_error("layer '" + layer.id +
                "' background plate has a non-positive size");

        LayerPlan card;
        card.id = layer.id + "__frame";
        card.type = LayerType::Color;
        card.start_frame = layer.start_frame;
        card.duration_frames = layer.duration_frames;
        card.size = {card_w, card_h};
        card.size_dimensions = 2;
        // The plate geometry is centered around its transform, while a video
        // source's local surface starts at its top-left. Resolve the plate's
        // center from the same origin-to-center conversion used by the video
        // builder so scaled/positioned media and its frame remain aligned.
        // Its size has already incorporated scale, so the plate stays
        // unit-scaled.
        card.position = layer.position;
        card.position_dimensions = layer.position_dimensions;
        card.rotation = layer.rotation;
        card.rotation_dimensions = layer.rotation_dimensions;
        card.enable_3d = layer.enable_3d;
        card.opacity = layer.opacity;
        if (layer.type == LayerType::Video && layer.position_dimensions >= 2) {
            const float displayed_w = layer.size_dimensions == 2
                ? layer.size[0] : static_cast<float>(plan.canvas.width) * scale_x;
            const float displayed_h = layer.size_dimensions == 2
                ? layer.size[1] : static_cast<float>(plan.canvas.height) * scale_y;
            card.position[0] += displayed_w * 0.5f -
                static_cast<float>(plan.canvas.width) * 0.5f;
            card.position[1] += displayed_h * 0.5f -
                static_cast<float>(plan.canvas.height) * 0.5f;
        }
        // The frame plate is a sibling layer, so it must follow the media's
        // transform and visibility tracks explicitly. Its base size above
        // already includes the media's initial scale; normalize animated
        // scales around that base to avoid applying the initial scale twice.
        if (layer.animation) {
            AnimationPlan plate_animation;
            for (const auto& source_track : layer.animation->tracks) {
                if (source_track.property != "position" && source_track.property != "rotation" &&
                    source_track.property != "scale" && source_track.property != "opacity")
                    continue;
                auto track = source_track;
                if (track.property == "scale") {
                    for (auto& keyframe : track.keyframes) {
                        for (std::size_t axis = 0; axis < keyframe.value.size() && axis < 3; ++axis) {
                            const float base_scale = layer.scale[axis];
                            if (!std::isfinite(base_scale) || std::abs(base_scale) <= 1e-6f)
                                throw std::runtime_error("layer '" + layer.id +
                                    "' has an invalid base scale for its animated media frame");
                            keyframe.value[axis] /= base_scale;
                        }
                    }
                }
                plate_animation.tracks.push_back(std::move(track));
            }
            if (!plate_animation.tracks.empty())
                card.animation = std::move(plate_animation);
        }
        card.color = {parsed_color->r, parsed_color->g, parsed_color->b,
                      parsed_color->a * opacity};
        card.radius = std::min(background.radius.value_or(0.0f),
                               std::min(card_w, card_h) * 0.5f);
        if (layer.style->shadow || !layer.style->shadows.empty()) {
            LayerStylePlan card_style;
            card_style.shadow = layer.style->shadow;
            card_style.shadows = layer.style->shadows;
            card.style = std::move(card_style);
        }
        // The plate owns the frame and the shadow from here on: leaving the
        // declaration on the media layer would materialize a second plate on
        // any later pass over the same plan.
        layer.style->background.reset();
        layer.style->shadow.reset();
        layer.style->shadows.clear();
        expanded.push_back(std::move(card));
        expanded.push_back(std::move(layer));
    }
    plan.layers = std::move(expanded);
}

} // namespace

Result<PreparedRenderPlan, PlanDecodeError> compile_render_plan(
    const RenderPlan& input_plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options,
    const compiler::CompiledProgram* canonical_program) {
    try {
        auto expanded_plan = input_plan;
        auto& plan = expanded_plan;
        materialize_media_background_cards(plan);
        detail::bake_audio_react_plan(plan, resolver);
        for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
            auto& layer = plan.layers[layer_index];
            if (!layer.animation) continue;
            for (std::size_t track_index = 0; track_index < layer.animation->tracks.size(); ++track_index) {
                auto& track = layer.animation->tracks[track_index];
                if (!track.data_feed || track.data_feed->asset.empty()) continue;
                auto& feed = *track.data_feed;
                const auto path = "layers[" + std::to_string(layer_index) + "].animation.tracks[" +
                    std::to_string(track_index) + "].data_feed.asset";
                const auto resolved = resolver.resolve_logical(feed.asset);
                if (!resolved)
                    return PlanDecodeError{path, "data_feed asset could not be resolved within the mounted asset root",
                        "data_feed_asset_unresolved", "render-plan-data-feed"};
                std::error_code size_error;
                const auto byte_size = std::filesystem::file_size(*resolved, size_error);
                constexpr std::uintmax_t kMaxDataFeedAssetBytes = 32ULL * 1024ULL * 1024ULL;
                if (size_error || byte_size > kMaxDataFeedAssetBytes)
                    return PlanDecodeError{path, "data_feed asset must be readable and no larger than 32 MiB",
                        "data_feed_asset_size", "render-plan-data-feed"};
                const auto digest = chronon3d::assets::sha256_file(*resolved);
                if (!digest || digest->hex() != feed.sha256)
                    return PlanDecodeError{path, "data_feed asset SHA-256 does not match the declared digest",
                        "data_feed_digest_mismatch", "render-plan-data-feed"};
                try {
                    std::ifstream input(*resolved, std::ios::binary);
                    const auto data = nlohmann::json::parse(input);
                    if (!data.is_array() || data.size() != feed.item_count)
                        throw std::runtime_error("data_feed asset must be a numeric JSON array matching item_count");
                    feed.values.clear();
                    feed.values.reserve(data.size());
                    for (const auto& sample : data) {
                        if (!sample.is_number() || !std::isfinite(sample.get<double>()) ||
                            !std::isfinite(sample.get<float>()))
                            throw std::runtime_error("data_feed asset values must be finite float numbers");
                        feed.values.push_back(sample.get<float>());
                    }
                } catch (const std::exception& error) {
                    return PlanDecodeError{path, error.what(), "data_feed_asset_invalid", "render-plan-data-feed"};
                }
            }
        }
        for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
            auto& layer = plan.layers[layer_index];
            if (!layer.shape || !layer.shape->lottie_asset) continue;
            const auto logical = *layer.shape->lottie_asset;
            const auto resolved = resolver.resolve_logical(logical);
            if (!resolved)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.lottie",
                    "Lottie asset could not be resolved within the mounted asset root",
                    "lottie_asset_unresolved", "render-plan-lottie"};
            auto lowered = detail::expand_static_lottie(layer, *resolved);
            if (lowered)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.lottie",
                    *lowered, "lottie_unsupported_or_invalid", "render-plan-lottie"};
        }
#if defined(CHRONON3D_ENABLE_SVG)
        std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>> pending_svg_text_layers;
        struct SvgTextMetrics {
            std::string font_asset;
            float advance{0.0f};
            float ascent{0.0f};
            float descent{0.0f};
            float x_height{0.0f};
            float cap_height{0.0f};
            float line_height{0.0f};
        };
        std::unique_ptr<chronon3d::FontEngine> svg_text_font_engine;
        for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
            auto& layer = plan.layers[layer_index];
            if (!layer.shape || !layer.shape->svg_asset) continue;
            const auto logical = *layer.shape->svg_asset;
            const auto resolved = resolver.resolve_logical(logical);
            if (!resolved)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG asset could not be resolved within the mounted asset root",
                    "svg_asset_unresolved", "render-plan-svg"};
            const auto imported = chronon3d::assets::load_svg_document_file(resolved->string());
            if (!imported.ok)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    imported.error, "svg_import_failed", "render-plan-svg"};
            if (imported.elements.empty() && imported.text_runs.empty())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG contains no path geometry or text", "svg_empty_geometry", "render-plan-svg"};
            if (imported.elements.size() + imported.text_runs.size() > 4096)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG styled path/text run count exceeds 4096", "svg_resource_limit", "render-plan-svg"};
            if (imported.root_gaussian_blur) {
                if (!std::isfinite(*imported.root_gaussian_blur) ||
                    *imported.root_gaussian_blur < 0.0f ||
                    *imported.root_gaussian_blur > 256.0f)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG root Gaussian blur is outside the supported radius bounds",
                        "svg_unsupported_filter", "render-plan-svg"};
                LayerPlan::EffectPlan native_blur;
                native_blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
                native_blur.radius = *imported.root_gaussian_blur;
                layer.effects.insert(layer.effects.begin(), std::move(native_blur));
            }
            auto& shape = *layer.shape;
            shape.imported_subpaths.clear();
            shape.imported_subpaths.reserve(imported.elements.size());
            std::size_t total_commands = 0;
            for (const auto& element : imported.elements) {
                if (!std::isfinite(element.opacity) || element.opacity < 0.0f ||
                    element.opacity > 1.0f ||
                    (element.fill && element.fill->enabled && element.fill->type != FillType::Solid &&
                     element.fill->type != FillType::LinearGradient && element.fill->type != FillType::RadialGradient) ||
                    element.stroke.gradient)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG opacity must be in [0,1]; only solid path paints are supported",
                        "svg_unsupported_style", "render-plan-svg"};
                ImportedSubpathPlan subpath;
                subpath.source_id = element.source_id;
                subpath.source_group_ids = element.group_ids;
                subpath.fill_rule = element.path.fill_rule == PathFillRule::NonZero
                    ? ShapeFillRulePlan::NonZero : ShapeFillRulePlan::EvenOdd;
                if (element.group) {
                    if (element.group->id == 0 ||
                        !std::isfinite(element.group->opacity) ||
                        element.group->opacity <= 0.0f || element.group->opacity > 1.0f)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG isolated group is invalid: id must be nonzero and opacity in (0,1]",
                            "svg_unsupported_style", "render-plan-svg"};
                    subpath.svg_group = ImportedSubpathPlan::SvgGroupIsolationPlan{
                        element.group->id, element.group->opacity};
                }
                if (element.fill && element.fill->enabled) {
                    if (element.fill->type != FillType::Solid &&
                        std::any_of(element.animations.begin(), element.animations.end(), [](const auto& animation) {
                            return animation.kind == assets::SvgAnimationKind::FillColor;
                        }))
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG fill color animation requires a solid fill",
                            "svg_unsupported_style", "render-plan-svg"};
                    if (element.fill->type == FillType::Solid) {
                        auto color = element.fill->solid;
                        color.a *= element.opacity;
                        subpath.fill_color = std::array<float, 4>{color.r, color.g, color.b, color.a};
                    } else {
                        subpath.fill_gradient = element.fill->gradient;
                        for (auto& stop : subpath.fill_gradient->color_stops)
                            stop.color.a *= element.opacity;
                    }
                }
                if (element.stroke.enabled) {
                    StrokeStyle stroke;
                    stroke.width = element.stroke.width;
                    stroke.miter_limit = element.stroke.miter_limit;
                    stroke.cap = element.stroke.cap == LineCap::Round ? "round"
                        : element.stroke.cap == LineCap::Square ? "square" : "butt";
                    stroke.join = element.stroke.join == LineJoin::Round ? "round"
                        : element.stroke.join == LineJoin::Bevel ? "bevel" : "miter";
                    stroke.dash_array = element.stroke.dash_array;
                    stroke.dash_offset = element.stroke.dash_offset;
                    auto color = element.stroke.color;
                    color.a *= element.opacity;
                    stroke.solid_color = std::array<float, 4>{color.r, color.g, color.b, color.a};
                    subpath.stroke = std::move(stroke);
                }
                const double svg_fps = plan.canvas.fps.fps();
                const auto frame_at_time = [&](float seconds) -> std::optional<std::int64_t> {
                    const double frame = static_cast<double>(seconds) * svg_fps;
                    const double rounded = std::round(frame);
                    if (!std::isfinite(frame) || std::abs(frame - rounded) > 1e-5 ||
                        rounded < 0.0 || rounded > 1000000.0) return std::nullopt;
                    return static_cast<std::int64_t>(rounded);
                };
                for (const auto& svg_animation : element.animations) {
                    const auto first_frame = frame_at_time(svg_animation.begin_seconds);
                    const auto last_frame = frame_at_time(
                        svg_animation.begin_seconds + svg_animation.duration_seconds);
                    if (!first_frame || !last_frame || *last_frame <= *first_frame)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG animation times must map exactly to integral composition frames",
                            "svg_animation_timing_unsupported", "render-plan-svg"};
                    AnimationTrackPlan track;
                    switch (svg_animation.kind) {
                        case assets::SvgAnimationKind::Opacity: track.property = "svg_opacity"; break;
                        case assets::SvgAnimationKind::Translate: track.property = "svg_translation"; break;
                        case assets::SvgAnimationKind::Scale: track.property = "svg_scale"; break;
                        case assets::SvgAnimationKind::Rotate: track.property = "svg_rotation"; break;
                        case assets::SvgAnimationKind::FillColor: track.property = "fill_color"; break;
                        case assets::SvgAnimationKind::StrokeColor: track.property = "stroke_color"; break;
                        case assets::SvgAnimationKind::StrokeWidth: track.property = "stroke_width"; break;
                        case assets::SvgAnimationKind::FillOpacity: track.property = "fill_opacity"; break;
                        case assets::SvgAnimationKind::StrokeOpacity: track.property = "stroke_opacity"; break;
                    }
                    AnimationKeyframePlan from;
                    from.frame = Frame{*first_frame};
                    from.value.assign(svg_animation.from.begin(),
                        svg_animation.from.begin() + svg_animation.components);
                    from.interpolation = svg_animation.discrete ? "hold" : "linear";
                    AnimationKeyframePlan to;
                    to.frame = Frame{*last_frame};
                    to.value.assign(svg_animation.to.begin(),
                        svg_animation.to.begin() + svg_animation.components);
                    track.keyframes = {std::move(from), std::move(to)};
                    std::optional<AnimationTrackPlan>* destination = nullptr;
                    switch (svg_animation.kind) {
                        case assets::SvgAnimationKind::Opacity: destination = &subpath.svg_opacity_animation; break;
                        case assets::SvgAnimationKind::Translate: destination = &subpath.svg_translation_animation; break;
                        case assets::SvgAnimationKind::Scale: destination = &subpath.svg_scale_animation; break;
                        case assets::SvgAnimationKind::Rotate: destination = &subpath.svg_rotation_animation; break;
                        case assets::SvgAnimationKind::FillColor: destination = &subpath.fill_color_animation; break;
                        case assets::SvgAnimationKind::StrokeColor: destination = &subpath.stroke_color_animation; break;
                        case assets::SvgAnimationKind::StrokeWidth: destination = &subpath.stroke_width_animation; break;
                        case assets::SvgAnimationKind::FillOpacity: destination = &subpath.fill_opacity_animation; break;
                        case assets::SvgAnimationKind::StrokeOpacity: destination = &subpath.stroke_opacity_animation; break;
                    }
                    if (*destination)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG path has multiple animations targeting the same property",
                            "svg_animation_conflict", "render-plan-svg"};
                    *destination = std::move(track);
                }
                if (!element.gradient_animation.empty()) {
                    if (!subpath.fill_gradient)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG gradient animation requires a gradient fill",
                            "svg_unsupported_style", "render-plan-svg"};
                    GradientAnimationPlan track;
                    track.easing = element.gradient_animation_discrete ? "hold" : "linear";
                    std::int64_t previous_frame = -1;
                    for (const auto& key : element.gradient_animation) {
                        const auto frame = frame_at_time(key.seconds);
                        if (!frame || *frame <= previous_frame)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                                "SVG gradient animation times must map to unique integral composition frames",
                                "svg_animation_timing_unsupported", "render-plan-svg"};
                        previous_frame = *frame;
                        track.keyframes.push_back({Frame{*frame}, key.gradient});
                    }
                    subpath.fill_gradient_animation = std::move(track);
                }
                if (element.morph_animation) {
                    const auto& morph = *element.morph_animation;
                    if (morph.paths.size() < 2 || morph.paths.size() != morph.key_times.size())
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG path morph has inconsistent keyframe geometry", "svg_morph_invalid", "render-plan-svg"};
                    AnimationTrackPlan progress;
                    progress.property = "morph_path_progress";
                    std::int64_t previous_frame = -1;
                    for (std::size_t morph_index = 0; morph_index < morph.paths.size(); ++morph_index) {
                        const auto frame = frame_at_time(morph.duration_seconds * morph.key_times[morph_index]);
                        if (!frame || *frame <= previous_frame)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                                "SVG path morph times must map to unique integral composition frames",
                                "svg_animation_timing_unsupported", "render-plan-svg"};
                        previous_frame = *frame;
                        AnimationKeyframePlan key;
                        key.frame = Frame{*frame};
                        key.value = {static_cast<float>(morph_index)};
                        key.interpolation = morph.discrete ? "hold" : "linear";
                        progress.keyframes.push_back(std::move(key));
                        if (morph_index == 0) continue;
                        std::vector<ShapePathCommandPlan> target;
                        target.reserve(morph.paths[morph_index].commands.size());
                        for (const auto& command : morph.paths[morph_index].commands) {
                            ShapePathCommandPlan lowered;
                            const auto point = [&](Vec2 value, std::array<float, 2>& out) {
                                out = {std::round(value.x * 10000.0f) / 10000.0f,
                                       std::round(value.y * 10000.0f) / 10000.0f};
                            };
                            switch (command.type) {
                                case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; point(command.p0, lowered.point); break;
                                case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; point(command.p0, lowered.point); break;
                                case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); break;
                                case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); point(command.p2, lowered.control2); break;
                                case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                            }
                            target.push_back(lowered);
                        }
                        subpath.morph_keyframe_targets.push_back(std::move(target));
                    }
                    subpath.morph_animation = std::move(progress);
                }
                for (const auto& command : element.path.commands) {
                    ShapePathCommandPlan lowered;
                    const auto quantize = [](float value) { return std::round(value * 10000.0f) / 10000.0f; };
                    const auto point = [&](Vec2 value, std::array<float, 2>& out) {
                        if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                            std::abs(value.x) > 100000.0f || std::abs(value.y) > 100000.0f)
                            throw std::runtime_error("SVG coordinate exceeds finite resource bounds");
                        out = {quantize(value.x), quantize(value.y)};
                    };
                    switch (command.type) {
                        case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; point(command.p0, lowered.point); break;
                        case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; point(command.p0, lowered.point); break;
                        case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); break;
                        case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); point(command.p2, lowered.control2); break;
                        case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                    }
                    if (total_commands >= 65536)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG total path command limit exceeded", "svg_resource_limit", "render-plan-svg"};
                    subpath.path.push_back(lowered);
                    ++total_commands;
                }
                shape.imported_subpaths.push_back(std::move(subpath));
            }
            std::vector<std::size_t> current_text_layers;
            current_text_layers.reserve(imported.text_runs.size());
            std::vector<SvgTextMetrics> text_metrics(imported.text_runs.size());
            std::unordered_map<std::string, float> text_anchor_advances;
            const auto group_key_for = [&](const chronon3d::assets::SvgTextRun& run,
                                           std::size_t index) {
                return layer.id + "#" + (run.text_element_id.empty()
                    ? (run.source_id.empty() ? "run_" + std::to_string(index) : run.source_id)
                    : run.text_element_id);
            };
            for (std::size_t text_index = 0; text_index < imported.text_runs.size(); ++text_index) {
                const auto& run = imported.text_runs[text_index];
                if (run.text.empty() || run.font_family.empty() ||
                    !std::isfinite(run.font_size) || run.font_size <= 0.0f ||
                    !std::isfinite(run.opacity) || run.opacity < 0.0f || run.opacity > 1.0f ||
                    !std::isfinite(run.fill_opacity) || run.fill_opacity < 0.0f || run.fill_opacity > 1.0f ||
                    !std::isfinite(run.stroke_opacity) || run.stroke_opacity < 0.0f || run.stroke_opacity > 1.0f ||
                    !std::isfinite(run.stroke_width) || run.stroke_width < 0.0f ||
                    !std::isfinite(run.dx) || !std::isfinite(run.dy) ||
                    !std::isfinite(run.baseline_shift))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG text run has invalid content, font size, position, or opacity",
                        "svg_text_invalid", "render-plan-svg"};
                std::string family_file = run.font_family;
                std::replace(family_file.begin(), family_file.end(), ' ', '-');
                if (family_file.empty() || family_file.find_first_of("/\\:") != std::string::npos)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG font-family is not a safe bundled font name",
                        "svg_font_unresolved", "render-plan-svg"};
                const std::string face = run.font_weight >= 600
                    ? (run.font_style == "normal" ? "Bold" : "BoldItalic")
                    : (run.font_style == "normal" ? "Regular" : "Italic");
                const std::array<std::string, 4> font_candidates{
                    "fonts/" + family_file + "-" + face + ".ttf",
                    "fonts/" + family_file + "-" + face + ".otf",
                    "fonts/" + family_file + "-Regular.ttf",
                    "fonts/" + family_file + ".ttf"};
                std::optional<std::filesystem::path> resolved_font;
                std::string font_asset;
                for (const auto& candidate : font_candidates) {
                    resolved_font = resolver.resolve_logical(std::filesystem::path(candidate));
                    if (resolved_font) { font_asset = candidate; break; }
                }
                if (!resolved_font)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG font-family has no matching font asset under fonts/: " + run.font_family,
                        "svg_font_unresolved", "render-plan-svg"};
                if (!svg_text_font_engine)
                    svg_text_font_engine = std::make_unique<chronon3d::FontEngine>(resolver);
                chronon3d::FontSpec font_spec;
                font_spec.font_path = resolved_font->string();
                font_spec.font_family = run.font_family;
                font_spec.font_weight = run.font_weight;
                font_spec.font_style = run.font_style;
                font_spec.font_size = run.font_size;
                const float advance = svg_text_font_engine->measure_text(
                    run.text, font_spec, run.font_size);
                const auto metrics = svg_text_font_engine->get_font_metrics(
                    font_spec, run.font_size);
                if (!std::isfinite(advance) || advance < 0.0f ||
                    !std::isfinite(metrics.ascent) || metrics.ascent <= 0.0f ||
                    !std::isfinite(metrics.descent) || metrics.descent < 0.0f ||
                    !std::isfinite(metrics.x_height) || metrics.x_height < 0.0f ||
                    !std::isfinite(metrics.cap_height) || metrics.cap_height < 0.0f ||
                    !std::isfinite(metrics.line_height) || metrics.line_height <= 0.0f)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG text could not be measured with its resolved font asset",
                        "svg_font_unresolved", "render-plan-svg"};
                text_metrics[text_index] = {font_asset, advance, metrics.ascent,
                    metrics.descent, metrics.x_height, metrics.cap_height,
                    metrics.line_height};
                const auto anchor_scope = run.text_anchor_scope_id.empty()
                    ? group_key_for(run, text_index) : layer.id + "#anchor#" + run.text_anchor_scope_id;
                text_anchor_advances[anchor_scope] += advance;
            }
            std::unordered_map<std::string, std::array<float, 2>> text_group_cursors;
            std::unordered_set<std::string> started_text_anchor_scopes;
            for (std::size_t text_index = 0; text_index < imported.text_runs.size(); ++text_index) {
                const auto& run = imported.text_runs[text_index];
                const auto group_key = group_key_for(run, text_index);
                const auto cursor_it = text_group_cursors.find(group_key);
                const bool first_run = cursor_it == text_group_cursors.end();
                float run_x = run.x_explicit ? run.x.value_or(0.0f)
                    : first_run ? 0.0f : cursor_it->second[0];
                float baseline_y = run.y_explicit ? run.y.value_or(0.0f)
                    : first_run ? 0.0f : cursor_it->second[1];
                run_x += run.dx;
                baseline_y += run.dy + run.baseline_shift;
                if (run.dominant_baseline == "central")
                    baseline_y += (text_metrics[text_index].ascent -
                                   text_metrics[text_index].descent) * 0.5f;
                else if (run.dominant_baseline == "middle")
                    baseline_y += text_metrics[text_index].x_height * 0.5f;
                else if (run.dominant_baseline == "hanging")
                    baseline_y += text_metrics[text_index].ascent * 0.8f;
                else if (run.dominant_baseline == "ideographic")
                    baseline_y += text_metrics[text_index].descent;
                else if (run.dominant_baseline == "mathematical")
                    baseline_y += text_metrics[text_index].cap_height * 0.5f;
                else if (run.dominant_baseline == "text-before-edge")
                    baseline_y += text_metrics[text_index].ascent;
                else if (run.dominant_baseline == "text-after-edge")
                    baseline_y -= text_metrics[text_index].descent;
                const auto anchor_scope = run.text_anchor_scope_id.empty()
                    ? group_key : layer.id + "#anchor#" + run.text_anchor_scope_id;
                const bool first_anchor_run = started_text_anchor_scopes.insert(anchor_scope).second;
                if (first_anchor_run || run.x_explicit) {
                    const float anchor_advance = run.x_explicit
                        ? text_metrics[text_index].advance : text_anchor_advances[anchor_scope];
                    if (run.text_anchor == "middle") run_x -= anchor_advance * 0.5f;
                    else if (run.text_anchor == "end") run_x -= anchor_advance;
                }
                text_group_cursors[group_key] = {
                    run_x + text_metrics[text_index].advance, baseline_y};
                LayerPlan text_layer;
                const std::string authored_id = run.source_id.empty()
                    ? "run_" + std::to_string(text_index) : run.source_id;
                text_layer.id = layer.id + "__svgtext_" + authored_id + "_" +
                    std::to_string(text_index);
                text_layer.type = LayerType::Text;
                text_layer.text = run.text;
                text_layer.position_dimensions = 3;
                text_layer.position[0] = layer.position[0] + run_x;
                text_layer.position[1] = layer.position[1] + baseline_y -
                    text_metrics[text_index].ascent;
                text_layer.position[2] = layer.position[2];
                text_layer.size_dimensions = 2;
                text_layer.size = {std::max(1.0f, text_metrics[text_index].advance + 1.0f),
                    text_metrics[text_index].line_height};
                text_layer.enable_3d = layer.enable_3d;
                text_layer.scale = layer.scale;
                text_layer.scale_dimensions = layer.scale_dimensions;
                text_layer.rotation = layer.rotation;
                text_layer.rotation_dimensions = layer.rotation_dimensions;
                text_layer.opacity = std::clamp(run.opacity * layer.opacity.value_or(1.0f), 0.0f, 1.0f);
                LayerStylePlan text_style;
                text_style.font_family = run.font_family;
                text_style.font_weight = run.font_weight;
                text_style.font_style = run.font_style;
                text_style.text_align = "left";
                text_style.font_size = run.font_size;
                text_style.fill = run.fill;
                text_style.fill_opacity = run.fill_opacity;
                if (run.stroke && run.stroke_width > 0.0f) {
                    text_style.stroke = StrokeStyle{.color = *run.stroke,
                        .width = run.stroke_width,
                        .dash_array = run.stroke_dash_array,
                        .dash_offset = run.stroke_dash_offset,
                        .cap = run.stroke_cap == LineCap::Round ? "round"
                            : run.stroke_cap == LineCap::Square ? "square" : "butt",
                        .join = run.stroke_join == LineJoin::Round ? "round"
                            : run.stroke_join == LineJoin::Bevel ? "bevel" : "miter",
                        .miter_limit = run.stroke_miter_limit,
                        .opacity = run.stroke_opacity};
                }
                text_style.text_fit_mode = "none";
                text_layer.font = text_metrics[text_index].font_asset;
                text_layer.style = std::move(text_style);
                if (!run.text_path_id) {
                    const bool has_coordinate_lists = !run.x_coordinates.empty() ||
                        !run.y_coordinates.empty() || !run.dx_coordinates.empty() ||
                        !run.dy_coordinates.empty();
                    if (has_coordinate_lists) {
                        chronon3d::FontSpec coordinate_font;
                        coordinate_font.font_path = text_metrics[text_index].font_asset;
                        if (const auto path = resolver.resolve_logical(coordinate_font.font_path))
                            coordinate_font.font_path = path->string();
                        coordinate_font.font_family = run.font_family;
                        coordinate_font.font_weight = run.font_weight;
                        coordinate_font.font_style = run.font_style;
                        coordinate_font.font_size = run.font_size;
                        const auto shaped = svg_text_font_engine->shape_text(
                            run.text, coordinate_font, run.font_size);
                        if (!shaped)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.asset", "SVG positioned text could not be shaped",
                                "svg_text_invalid", "render-plan-svg"};
                        const auto placed = chronon3d::resolve_placed_glyph_run(
                            *shaped, 0.0f, run.text);
                        float cursor_x = run_x - run.dx;
                        float cursor_y = baseline_y - run.dy;
                        std::size_t character_index = 0;
                        const auto utf8_count = [](std::string_view value) {
                            std::size_t count = 0;
                            for (const unsigned char byte : value)
                                if ((byte & 0xc0u) != 0x80u) ++count;
                            return count;
                        };
                        for (std::size_t cluster_index = 0;
                             cluster_index < placed.clusters.size(); ++cluster_index) {
                            const auto& cluster = placed.clusters[cluster_index];
                            if (cluster.byte_len == 0 ||
                                cluster.byte_offset + cluster.byte_len > run.text.size())
                                continue;
                            const std::size_t cluster_characters = utf8_count(run.text.substr(
                                cluster.byte_offset, cluster.byte_len));
                            if (cluster_characters == 0) continue;
                            const auto has_inner_coordinate = [&](const std::vector<float>& coordinates) {
                                const auto first_inner = character_index + 1;
                                const auto end = character_index + cluster_characters;
                                return std::any_of(coordinates.begin() + std::min(first_inner, coordinates.size()),
                                    coordinates.begin() + std::min(end, coordinates.size()),
                                    [](float) { return true; });
                            };
                            if (has_inner_coordinate(run.x_coordinates) ||
                                has_inner_coordinate(run.y_coordinates) ||
                                has_inner_coordinate(run.dx_coordinates) ||
                                has_inner_coordinate(run.dy_coordinates))
                                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                    "].shape.asset", "SVG coordinate list splits a shaped text cluster; split the run at that character",
                                    "svg_text_coordinate_cluster_unsupported", "render-plan-svg"};
                            if (character_index < run.x_coordinates.size())
                                cursor_x = run.x_coordinates[character_index];
                            if (character_index < run.y_coordinates.size())
                                cursor_y = run.y_coordinates[character_index];
                            if (character_index < run.dx_coordinates.size())
                                cursor_x += run.dx_coordinates[character_index];
                            if (character_index < run.dy_coordinates.size())
                                cursor_y += run.dy_coordinates[character_index];
                            const std::string cluster_text = run.text.substr(
                                cluster.byte_offset, cluster.byte_len);
                            auto cluster_run = run;
                            cluster_run.text = cluster_text;
                            cluster_run.positioned_text_cluster = true;
                            auto cluster_layer = text_layer;
                            cluster_layer.id += "_cluster_" + std::to_string(cluster_index);
                            cluster_layer.text = cluster_text;
                            float anchored_x = cursor_x;
                            const bool starts_positioned_chunk =
                                character_index < run.x_coordinates.size() ||
                                character_index < run.y_coordinates.size();
                            if (starts_positioned_chunk) {
                                float chunk_advance = cluster.advance;
                                std::size_t next_character = character_index + cluster_characters;
                                for (std::size_t next_cluster_index = cluster_index + 1;
                                     next_cluster_index < placed.clusters.size(); ++next_cluster_index) {
                                    const auto& next_cluster = placed.clusters[next_cluster_index];
                                    if (next_cluster.byte_len == 0 ||
                                        next_cluster.byte_offset + next_cluster.byte_len > run.text.size())
                                        continue;
                                    if (next_character < run.x_coordinates.size() ||
                                        next_character < run.y_coordinates.size())
                                        break;
                                    chunk_advance += next_cluster.advance;
                                    next_character += utf8_count(run.text.substr(
                                        next_cluster.byte_offset, next_cluster.byte_len));
                                }
                                if (run.text_anchor == "middle") anchored_x -= chunk_advance * 0.5f;
                                else if (run.text_anchor == "end") anchored_x -= chunk_advance;
                            }
                            cluster_layer.position[0] = layer.position[0] + anchored_x;
                            cluster_layer.position[1] = layer.position[1] + cursor_y -
                                text_metrics[text_index].ascent;
                            cluster_layer.size[0] = std::max(1.0f, cluster.advance + 1.0f);
                            current_text_layers.push_back(pending_svg_text_layers.size());
                            pending_svg_text_layers.emplace_back(std::move(cluster_run),
                                                                 std::move(cluster_layer));
                            cursor_x += cluster.advance;
                            character_index += cluster_characters;
                        }
                        continue;
                    }
                    current_text_layers.push_back(pending_svg_text_layers.size());
                    pending_svg_text_layers.emplace_back(run, std::move(text_layer));
                    continue;
                }
                if (!run.x_coordinates.empty() || !run.y_coordinates.empty() ||
                    !run.dx_coordinates.empty() || !run.dy_coordinates.empty())
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath coordinate lists are not supported; position the textPath with startOffset and text-anchor",
                        "svg_text_path_coordinate_list_unsupported", "render-plan-svg"};

                const chronon3d::assets::SvgStyledElement* target_path = nullptr;
                chronon3d::PathShape text_path_geometry;
                std::size_t target_path_matches = 0;
                for (const auto& element : imported.elements) {
                    if (element.source_id == *run.text_path_id) {
                        target_path = &element;
                        ++target_path_matches;
                    }
                }
                if (target_path_matches > 1)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath target is ambiguous after path expansion: " +
                        *run.text_path_id,
                        "svg_target_ambiguous", "render-plan-svg"};
                if (target_path_matches == 1 && target_path) {
                    text_path_geometry = target_path->path;
                    if (target_path->group_ids != run.coordinate_scope_ids)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.asset", "SVG textPath and target path must share the same group coordinate scope",
                            "svg_text_path_scope_invalid", "render-plan-svg"};
                    if (target_path->morph_animation || !target_path->animations.empty())
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.asset", "SVG textPath target paths must be static",
                            "svg_text_path_animated_target", "render-plan-svg"};
                } else if (run.text_path_geometry) {
                    text_path_geometry = *run.text_path_geometry;
                } else {
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath target must resolve to path geometry: " +
                        *run.text_path_id, "svg_target_missing", "render-plan-svg"};
                }
                if (text_path_geometry.commands.empty())
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath target path has empty geometry",
                        "svg_text_path_invalid", "render-plan-svg"};

                chronon3d::FontSpec path_font;
                path_font.font_path = text_metrics[text_index].font_asset;
                if (const auto path = resolver.resolve_logical(path_font.font_path))
                    path_font.font_path = path->string();
                path_font.font_family = run.font_family;
                path_font.font_weight = run.font_weight;
                path_font.font_style = run.font_style;
                path_font.font_size = run.font_size;
                const auto shaped = svg_text_font_engine->shape_text(
                    run.text, path_font, run.font_size);
                if (!shaped)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath text could not be shaped",
                        "svg_text_invalid", "render-plan-svg"};
                const auto placed = chronon3d::resolve_placed_glyph_run(
                    *shaped, 0.0f, run.text);
                const auto sampler = chronon3d::PathSampler::compile(text_path_geometry);
                chronon3d::TextPathSpec path_spec;
                const float start_offset = run.text_path_start_offset_percent
                    ? run.text_path_start_offset * sampler.total_length() * 0.01f
                    : run.text_path_start_offset;
                path_spec.first_margin = start_offset + run.dx;
                if (run.text_anchor == "middle")
                    path_spec.first_margin -= placed.total_width * 0.5f;
                else if (run.text_anchor == "end")
                    path_spec.first_margin -= placed.total_width;
                path_spec.baseline_offset = run.dy + run.baseline_shift;
                if (run.dominant_baseline == "central")
                    path_spec.baseline_offset += (text_metrics[text_index].ascent -
                        text_metrics[text_index].descent) * 0.5f;
                else if (run.dominant_baseline == "middle")
                    path_spec.baseline_offset += text_metrics[text_index].x_height * 0.5f;
                else if (run.dominant_baseline == "hanging")
                    path_spec.baseline_offset += text_metrics[text_index].ascent * 0.8f;
                path_spec.perpendicular_to_path = true;
                const auto path_placement = chronon3d::compose_text_on_path(
                    placed, sampler, path_spec, run.text);
                if (path_placement.clusters.size() != placed.clusters.size())
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath cluster placement failed",
                        "svg_text_invalid", "render-plan-svg"};
                for (std::size_t cluster_index = 0;
                     cluster_index < placed.clusters.size(); ++cluster_index) {
                    const auto& cluster = placed.clusters[cluster_index];
                    const auto& placement = path_placement.clusters[cluster_index];
                    if (cluster.byte_len == 0 || cluster.byte_offset + cluster.byte_len > run.text.size())
                        continue;
                    if (placement.path_distance > sampler.total_length()) continue;
                    const std::string cluster_text = run.text.substr(
                        cluster.byte_offset, cluster.byte_len);
                    if (cluster_text.empty()) continue;
                    auto cluster_run = run;
                    cluster_run.text = cluster_text;
                    auto cluster_layer = text_layer;
                    cluster_layer.id += "_cluster_" + std::to_string(cluster_index);
                    cluster_layer.text = cluster_text;
                    cluster_layer.position[0] = layer.position[0] + placement.position.x -
                        cluster.advance * 0.5f;
                    cluster_layer.position[1] = layer.position[1] + placement.position.y -
                        text_metrics[text_index].ascent;
                    cluster_layer.size[0] = std::max(1.0f, cluster.advance + 1.0f);
                    cluster_layer.rotation[2] += placement.rotation_deg;
                    cluster_layer.rotation_dimensions = 3;
                    current_text_layers.push_back(pending_svg_text_layers.size());
                    pending_svg_text_layers.emplace_back(std::move(cluster_run),
                                                         std::move(cluster_layer));
                }
            }
            for (const auto& operation : shape.operators) {
                if (operation.target_id.empty()) continue;
                std::size_t exact_matches = 0;
                std::size_t group_matches = 0;
                for (const auto& subpath : shape.imported_subpaths) {
                    exact_matches += subpath.source_id == operation.target_id;
                    group_matches += std::find(subpath.source_group_ids.begin(),
                        subpath.source_group_ids.end(), operation.target_id) !=
                        subpath.source_group_ids.end();
                }
                if (exact_matches > 1)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.operators", "SVG path operator target id is ambiguous after <use> expansion: " +
                        operation.target_id, "svg_target_ambiguous", "render-plan-svg"};
                if (exact_matches == 0 && group_matches == 0)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.operators", "SVG path operator target does not resolve: " +
                        operation.target_id, "svg_target_missing", "render-plan-svg"};
                if ((operation.kind == PathOperatorKindPlan::Merge ||
                     operation.kind == PathOperatorKindPlan::Morph) && exact_matches != 1)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.operators", "SVG Merge/Morph requires an exact path element id, not a group target",
                        "svg_target_property_invalid", "render-plan-svg"};
            }
            for (const auto& binding : shape.svg_element_animations) {
                std::vector<std::size_t> exact_text_matches;
                std::vector<std::size_t> group_text_matches;
                for (const auto pending_index : current_text_layers) {
                    const auto& run = pending_svg_text_layers[pending_index].first;
                    if (run.source_id == binding.target_id)
                        exact_text_matches.push_back(pending_index);
                    if (std::find(run.group_ids.begin(), run.group_ids.end(), binding.target_id) !=
                        run.group_ids.end())
                        group_text_matches.push_back(pending_index);
                }
                const bool one_clustered_text_run = exact_text_matches.size() > 1 &&
                    std::all_of(exact_text_matches.begin(), exact_text_matches.end(),
                        [&](std::size_t index) {
                            const auto& first = pending_svg_text_layers[exact_text_matches.front()].first;
                            const auto& current = pending_svg_text_layers[index].first;
                            const bool same_path = first.text_path_id &&
                                current.text_path_id == first.text_path_id;
                            const bool same_positioned_run = first.positioned_text_cluster &&
                                current.positioned_text_cluster;
                            return (same_path || same_positioned_run) &&
                                current.source_id == first.source_id &&
                                current.text_element_id == first.text_element_id;
                        });
                if (exact_text_matches.size() > 1 && !one_clustered_text_run)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.element_animations",
                        "SVG text target id is ambiguous: " + binding.target_id,
                        "svg_target_ambiguous", "render-plan-svg"};
                const auto& text_matches = exact_text_matches.empty()
                    ? group_text_matches : exact_text_matches;
                if (!text_matches.empty()) {
                    if (binding.property != "opacity")
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG text currently supports only opacity bindings",
                            "svg_target_property_invalid", "render-plan-svg"};
                    for (const auto pending_index : text_matches) {
                        auto& text_layer = pending_svg_text_layers[pending_index].second;
                        if (!text_layer.animation) text_layer.animation = AnimationPlan{};
                        auto track = binding.animation;
                        track.property = "opacity";
                        text_layer.animation->tracks.push_back(std::move(track));
                    }
                    continue;
                }
                std::vector<ImportedSubpathPlan*> exact_matches;
                std::vector<ImportedSubpathPlan*> group_matches;
                for (auto& subpath : shape.imported_subpaths) {
                    if (subpath.source_id == binding.target_id)
                        exact_matches.push_back(&subpath);
                    if (std::find(subpath.source_group_ids.begin(),
                                  subpath.source_group_ids.end(), binding.target_id) !=
                        subpath.source_group_ids.end())
                        group_matches.push_back(&subpath);
                }
                if (exact_matches.size() > 1) {
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.element_animations",
                        "SVG element target id is ambiguous after <use> expansion: " + binding.target_id,
                        "svg_target_ambiguous", "render-plan-svg"};
                }
                auto matches = exact_matches.empty() ? group_matches : exact_matches;
                if (matches.empty()) {
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.element_animations",
                        "SVG element animation target does not resolve: " + binding.target_id,
                        "svg_target_missing", "render-plan-svg"};
                }
                for (auto* subpath : matches) {
                    std::optional<AnimationTrackPlan>* destination = nullptr;
                    std::string canonical_property;
                    if (binding.property == "opacity") {
                        destination = &subpath->svg_opacity_animation;
                        canonical_property = "svg_opacity";
                    } else if (binding.property == "transform.translate") {
                        destination = &subpath->svg_translation_animation;
                        canonical_property = "svg_translation";
                    } else if (binding.property == "transform.scale") {
                        destination = &subpath->svg_scale_animation;
                        canonical_property = "svg_scale";
                    } else if (binding.property == "transform.rotation") {
                        destination = &subpath->svg_rotation_animation;
                        canonical_property = "svg_rotation";
                    } else if (binding.property == "fill.color") {
                        if (!subpath->fill_color)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.element_animations",
                                "SVG fill.color target requires a solid fill", "svg_target_property_invalid",
                                "render-plan-svg"};
                        destination = &subpath->fill_color_animation;
                        canonical_property = "fill_color";
                    } else if (binding.property == "fill.opacity") {
                        if (!subpath->fill_color && !subpath->fill_gradient)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.element_animations",
                                "SVG fill.opacity target requires a fill", "svg_target_property_invalid",
                                "render-plan-svg"};
                        destination = &subpath->fill_opacity_animation;
                        canonical_property = "fill_opacity";
                    } else if (binding.property == "stroke.color") {
                        if (!subpath->stroke)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.element_animations",
                                "SVG stroke.color target requires a stroke", "svg_target_property_invalid",
                                "render-plan-svg"};
                        destination = &subpath->stroke_color_animation;
                        canonical_property = "stroke_color";
                    } else if (binding.property == "stroke.opacity") {
                        if (!subpath->stroke)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.element_animations",
                                "SVG stroke.opacity target requires a stroke", "svg_target_property_invalid",
                                "render-plan-svg"};
                        destination = &subpath->stroke_opacity_animation;
                        canonical_property = "stroke_opacity";
                    } else if (binding.property == "stroke.width") {
                        if (!subpath->stroke)
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.element_animations",
                                "SVG stroke.width target requires a stroke", "svg_target_property_invalid",
                                "render-plan-svg"};
                        destination = &subpath->stroke_width_animation;
                        canonical_property = "stroke_width";
                    }
                    if (!destination)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "Unsupported SVG element animation property: " + binding.property,
                            "svg_target_property_invalid", "render-plan-svg"};
                    if (*destination)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                            "].shape.element_animations",
                            "SVG target property already has an animation: " + binding.property,
                            "svg_animation_conflict", "render-plan-svg"};
                    auto track = binding.animation;
                    track.property = canonical_property;
                    *destination = std::move(track);
                }
            }
            if (imported.vector_mask) {
                if (layer.path_mask || !layer.masks.empty())
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG vector clip/mask cannot be combined with a render-plan layer mask",
                        "svg_mask_conflict", "render-plan-svg"};
                LayerPlan::PathMaskPlan mask;
                mask.type = "path";
                mask.mode = "add";
                for (const auto& command : imported.vector_mask->commands) {
                    ShapePathCommandPlan lowered;
                    const auto lower_point = [&](Vec2 point, std::array<float, 2>& destination) {
                        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                            std::abs(point.x) > 100000.0f || std::abs(point.y) > 100000.0f)
                            throw std::runtime_error("SVG mask coordinate exceeds finite resource bounds");
                        destination = {std::round(point.x * 10000.0f) / 10000.0f,
                                       std::round(point.y * 10000.0f) / 10000.0f};
                    };
                    switch (command.type) {
                        case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; lower_point(command.p0, lowered.point); break;
                        case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; lower_point(command.p0, lowered.point); break;
                        case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; lower_point(command.p0, lowered.point); lower_point(command.p1, lowered.control1); break;
                        case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; lower_point(command.p0, lowered.point); lower_point(command.p1, lowered.control1); lower_point(command.p2, lowered.control2); break;
                        case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                    }
                    mask.path.push_back(lowered);
                }
                layer.path_mask = std::move(mask);
            }
        }
        for (auto& pending : pending_svg_text_layers)
            plan.layers.push_back(std::move(pending.second));
#else
        for (std::size_t index = 0; index < plan.layers.size(); ++index)
            if (plan.layers[index].shape && plan.layers[index].shape->svg_asset)
                return PlanDecodeError{"layers[" + std::to_string(index) + "].shape.asset",
                    "Chronon was built without SVG import support", "svg_import_unavailable",
                    "render-plan-svg"};
#endif
        const auto compile_total_t0 = chronon3d::profiling::now();
        double asset_prepare_ms = 0.0;
        double fingerprint_ms = 0.0;
        double media_resolution_ms = 0.0;
        double materialize_layout_ms = 0.0;
        double composition_compile_ms = 0.0;
        double compatibility_view_ms = 0.0;
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;

        const auto asset_t0 = chronon3d::profiling::now();
        auto prepared_store = chronon3d::assets::prepare_asset_store(plan, resolver);
        if (!prepared_store) {
            return PlanDecodeError{
                "assets." + prepared_store.error().logical_path,
                prepared_store.error().message,
                prepared_store.error().code ==
                        chronon3d::assets::AssetPreflightErrorCode::MissingAsset
                    ? "MissingAsset" : "",
                "asset_resolver"};
        }
        auto resources = std::move(prepared_store).value();
        asset_prepare_ms = chronon3d::profiling::duration_ms(
            asset_t0, chronon3d::profiling::now());
        auto assets = resources.manifest();

        const auto fingerprint_t0 = chronon3d::profiling::now();
        // Audit FP-1: the canonical job-identity authority, shared with the
        // compiler path so both produce the same digest for the same job.
        const auto fingerprints = compute_render_job_fingerprint(
            plan, assets, fingerprint_options);
        auto content_plan = plan;
        content_plan.job_id.clear();
        content_plan.output = {};
        const auto content_fingerprint =
            compute_render_plan_content_fingerprint(content_plan);
        fingerprint_ms = chronon3d::profiling::duration_ms(
            fingerprint_t0, chronon3d::profiling::now());

        const auto canvas = CanvasInfo::from_dimensions(
            static_cast<float>(plan.canvas.width),
            static_cast<float>(plan.canvas.height));

        const auto media_t0 = chronon3d::profiling::now();
        std::vector<std::string> resolved_video_paths(plan.layers.size());
        std::vector<std::string> resolved_particle_sprite_paths(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Video) continue;
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(layer.source));
            if (!resolved)
                throw std::runtime_error(
                    "video source was not prepared: " + layer.source);
            resolved_video_paths[index] = resolved->string();
        }
        for (std::size_t index=0; index<plan.layers.size(); ++index) {
            const auto& layer=plan.layers[index];
            if (!layer.particle_emitter || layer.particle_emitter->sprite_asset.empty()) continue;
            const auto resolved=resolver.resolve_logical(std::filesystem::path(layer.particle_emitter->sprite_asset));
            if(!resolved) throw std::runtime_error("particle sprite asset was not prepared: "+layer.particle_emitter->sprite_asset);
            resolved_particle_sprite_paths[index]=resolved->string();
        }
        media_resolution_ms = chronon3d::profiling::duration_ms(
            media_t0, chronon3d::profiling::now());

        CompositionSpec spec;
        spec.name = plan.job_id;
        spec.width = plan.canvas.width;
        spec.height = plan.canvas.height;
        spec.frame_rate = plan.canvas.fps;
        spec.duration = plan.canvas.duration;

        auto font_resolver = std::make_shared<chronon3d::assets::AssetResolver>();
        if (const auto root = resolver.mount_root(); !root.empty())
            font_resolver->mount(root);
        auto font_engine = std::make_shared<chronon3d::FontEngine>(*font_resolver);

        const auto materialize_t0 = chronon3d::profiling::now();
        // Lower each text definition once at the render-plan boundary. The
        // scene callback is evaluated once per frame; keeping PreparedText
        // here avoids rebuilding TextDocument/spans/shaping options on every
        // frame while preserving the existing layout-cache and animator
        // semantics in LayerBuilder::build().
        std::vector<std::optional<chronon3d::PreparedText>> prepared_texts(
            plan.layers.size());
        std::vector<std::shared_ptr<const chronon3d::AnimatedTextDocument>> prepared_text_counters(
            plan.layers.size());
        std::vector<std::optional<chronon3d::Vec2>> prepared_text_bounds(
            plan.layers.size());
        const auto assets_root = resolver.mount_root();
        chronon3d::TextCompileServices text_compile_services{
            font_engine.get(), nullptr,
            assets_root.empty() ? std::filesystem::path{}
                                : assets_root / "fonts"};
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Text) continue;
            auto definition = detail::materialize_text(layer, canvas);
            if (!definition.style.font.font_path.empty()) {
                const auto resolved = resolver.resolve_logical(
                    std::filesystem::path(definition.style.font.font_path));
                if (!resolved) {
                    throw std::runtime_error(
                        "font asset was not prepared: " +
                        definition.style.font.font_path);
                }
                definition.style.font.font_path = resolved->string();
            }
            if (layer.text_counter) {
                const auto& counter = layer.text_counter->counter;
                AnimationTrackPlan value_track;
                value_track.property = "text_counter";
                value_track.easing = counter.easing;
                value_track.keyframes = {
                    {counter.start_frame, {counter.from}},
                    {counter.start_frame + counter.duration_frames, {counter.to}}};
                chronon3d::AnimatedTextDocument animated;
                const auto replace_token = [&](float value) {
                    double magnitude = value;
                    std::string compact_suffix;
                    if (counter.format == "percent") magnitude *= 100.0;
                    if (counter.format == "compact") {
                        const double absolute = std::abs(magnitude);
                        if (absolute >= 1.0e9) { magnitude /= 1.0e9; compact_suffix = "B"; }
                        else if (absolute >= 1.0e6) { magnitude /= 1.0e6; compact_suffix = "M"; }
                        else if (absolute >= 1.0e3) { magnitude /= 1.0e3; compact_suffix = "K"; }
                    }
                    std::ostringstream formatted;
                    formatted.imbue(std::locale::classic());
                    formatted << std::fixed << std::setprecision(counter.decimals) << magnitude;
                    std::string number = formatted.str();
                    if (counter.decimals == 0)
                        number = std::to_string(static_cast<std::int64_t>(std::llround(magnitude)));
                    if (counter.format == "percent") number += "%";
                    else if (counter.format == "currency") {
                        const std::string symbol = counter.currency == "USD" ? "$" :
                            counter.currency == "EUR" ? "€" : counter.currency == "GBP" ? "£" : counter.currency + " ";
                        number = symbol + number;
                    } else if (counter.format == "compact") number += compact_suffix;
                    return counter.prefix + number + counter.suffix;
                };
                const auto fill_value = [&](float value) {
                    auto target = definition;
                    const auto found = target.content.value.find(layer.text_counter->token);
                    target.content.value.replace(found, layer.text_counter->token.size(), replace_token(value));
                    const auto token_start = found;
                    const auto token_end = found + layer.text_counter->token.size();
                    const auto replacement_size = target.content.value.size() -
                        (definition.content.value.size() - layer.text_counter->token.size());
                    const auto delta = static_cast<std::int64_t>(replacement_size) -
                        static_cast<std::int64_t>(layer.text_counter->token.size());
                    for (auto& span : target.spans) {
                        if (span.byte_end <= token_start) continue;
                        if (span.byte_start < token_end)
                            throw std::runtime_error("text_counter token cannot overlap styled text spans");
                        const auto shifted_start = static_cast<std::int64_t>(span.byte_start) + delta;
                        const auto shifted_end = static_cast<std::int64_t>(span.byte_end) + delta;
                        if (shifted_start < 0 || shifted_end < shifted_start)
                            throw std::runtime_error("text_counter span shift overflow");
                        span.byte_start = static_cast<std::size_t>(shifted_start);
                        span.byte_end = static_cast<std::size_t>(shifted_end);
                    }
                    return target;
                };
                for (std::int64_t offset = 0;
                     offset <= counter.duration_frames.integral(); ++offset) {
                    const Frame frame = counter.start_frame + Frame{offset};
                    const float value = detail::evaluate_scalar_track(
                        value_track, static_cast<double>(frame.integral()),
                        counter.from, plan.canvas.fps);
                    const auto definition_at_frame = fill_value(value);
                    chronon3d::SourceTextKeyframe key;
                    key.frame = frame;
                    key.document = chronon3d::to_text_document(definition_at_frame);
                    animated.add_keyframe(std::move(key));
                }
                const auto to_definition = fill_value(counter.to);
                prepared_text_counters[index] =
                    std::make_shared<const chronon3d::AnimatedTextDocument>(std::move(animated));
                definition = to_definition;
            }
            prepared_texts[index] = chronon3d::prepare_text(definition);
            if ((layer.style && layer.style->background) || layer.safe_insets) {
                auto measured = chronon3d::compile_text_layout(
                    *prepared_texts[index], text_compile_services);
                if (!measured)
                    throw std::runtime_error(
                        "text card layout failed for layer '" + layer.id + "': " +
                        measured.error().message);
                prepared_text_bounds[index] = measured.value()->bounds;
            }
        }
        auto prepared_effects = detail::prepare_layer_effects(plan, resolver);
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());

        CompositionDefinition definition;
        definition.composition = spec;
        const bool has_3d_layer = std::any_of(
            plan.layers.begin(), plan.layers.end(),
            [](const LayerPlan& layer) { return layer.enable_3d; });
        const auto plan_camera = detail::make_camera_descriptor(plan);
        std::string resolved_ocio_config_path;
        if (plan.color_management) {
            const auto config_path = resolver.resolve_lexical(
                plan.color_management->ocio_config);
            if (!config_path)
                throw std::runtime_error("OCIO config is outside the mounted asset root: " +
                    plan.color_management->ocio_config);
            resolved_ocio_config_path = config_path->string();
        }
        std::vector<detail::FollowPathEvaluators> follow_path_evaluators(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (!layer.follow_path) continue;
            Vec3 base_position{layer.position[0], layer.position[1], layer.position[2]};
            if ((layer.type == LayerType::Text || layer.type == LayerType::Shape ||
                 layer.type == LayerType::Particles || layer.type == LayerType::Light) &&
                layer.position_dimensions >= 2) {
                base_position.x -= static_cast<float>(plan.canvas.width) * 0.5f;
                base_position.y -= static_cast<float>(plan.canvas.height) * 0.5f;
            }
            const Vec3 base_rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};
            follow_path_evaluators[index] = detail::make_follow_path_evaluators(
                *layer.follow_path, base_position, base_rotation,
                plan.canvas.width, plan.canvas.height, plan.canvas.fps);
        }
        definition.scene = detail::make_scene_evaluator(detail::SceneMaterializationInputs{
            .plan = plan,
            .resolved_video_paths = resolved_video_paths,
            .resolved_particle_sprite_paths = resolved_particle_sprite_paths,
            .prepared_texts = prepared_texts,
            .prepared_text_counters = prepared_text_counters,
            .prepared_text_bounds = prepared_text_bounds,
            .prepared_effects = prepared_effects,
            .font_engine = font_engine,
            .has_3d_layer = has_3d_layer,
            .plan_camera = plan_camera,
            .follow_path_evaluators = follow_path_evaluators,
            .resolved_ocio_config_path = resolved_ocio_config_path});

        definition.camera = detail::make_camera_descriptor(plan);
        definition.scene_content_fingerprint = content_fingerprint;

        const auto composition_t0 = chronon3d::profiling::now();
        CompiledComposition compiled_value;
        std::shared_ptr<const Composition> composition;
        if (canonical_program != nullptr) {
            // The CLI already owns the canonical CompiledProgram. The old
            // video executor only needs a scene-evaluation compatibility
            // object, so do not invoke compile_composition again here.
            composition = std::make_shared<const Composition>(
                definition.composition, definition.scene,
                definition.scene_content_fingerprint);
            compiled_value.composition = composition;
            compiled_value.fingerprint = canonical_program->fingerprint();
        } else {
            // Standalone callers of the historical prepared-plan API retain
            // its original composition compiler behavior.
            Composition definition_composition(
                definition.composition,
                definition.scene,
                definition.scene_content_fingerprint);
            if (definition.camera.has_value()) {
                definition_composition.default_camera_descriptor(
                    *definition.camera);
            }
            auto compiled =
                chronon3d::compile_composition(definition_composition, {});
            if (!compiled)
                return PlanDecodeError{"composition", compiled.error().message};

            compiled_value = std::move(compiled).value();
            auto compiled_view =
                std::make_shared<const CompiledComposition>(compiled_value);
            composition = std::make_shared<Composition>(
                definition.composition,
                [compiled_view](const FrameContext& context) {
                    auto evaluated = chronon3d::evaluate(
                        *compiled_view,
                        CompositionEvaluateContext{.frame_context = context},
                        context.local_time());
                    if (!evaluated) {
                        throw std::runtime_error(
                            "compiled render-plan evaluation failed: " +
                            evaluated.error().message);
                    }
                    const auto camera = evaluated.value().camera;
                    Scene scene = std::move(evaluated.value().scene);
                    if (camera.has_value()) scene.set_camera_2_5d(*camera);
                    return scene;
                },
                content_fingerprint);
        }
        const double composition_stage_ms = chronon3d::profiling::duration_ms(
            composition_t0, chronon3d::profiling::now());
        if (canonical_program != nullptr) {
            compatibility_view_ms = composition_stage_ms;
        } else {
            composition_compile_ms = composition_stage_ms;
        }
        compiled_value.asset_manifest =
            std::make_shared<const chronon3d::assets::PreparedAssetManifest>(assets);
        compiled_value.render_budget = plan.budget;

        PreparedRenderPlan prepared;
        prepared.compiled_composition = std::move(compiled_value);
        prepared.composition =
            std::shared_ptr<const Composition>(std::move(composition));
        prepared.assets = std::move(assets);
        prepared.resources = std::move(resources);
        prepared.fingerprint = fingerprints;
        prepared.job_id = plan.job_id;
        prepared.canvas = plan.canvas;
        prepared.output = plan.output;
        prepared.render_budget = plan.budget;
        spdlog::info(
            "[plan-compile-profile] assets={:.2f}ms fingerprint={:.2f}ms "
            "media_resolution={:.2f}ms materialize={:.2f}ms "
            "composition_compile={:.2f}ms compatibility_view={:.2f}ms "
            "total={:.2f}ms",
            asset_prepare_ms, fingerprint_ms, media_resolution_ms,
            materialize_layout_ms, composition_compile_ms, compatibility_view_ms,
            chronon3d::profiling::duration_ms(
                compile_total_t0, chronon3d::profiling::now()));
        return prepared;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

Result<CompiledComposition, PlanDecodeError> make_video_compatibility_view(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const compiler::CompiledProgram& program,
    const RenderPlanFingerprintOptions& fingerprint_options) {
    // Reuse the canonical prepared-plan machinery with an explicit program
    // owner. The implementation's canonical_program branch performs asset
    // and text materialization but skips the legacy composition compiler.
    auto prepared = compile_render_plan(
        plan, resolver, fingerprint_options, &program);
    if (!prepared) return std::move(prepared).error();
    auto result = std::move(prepared).value().compiled_composition;
    // The canonical compiler's manifest is authoritative. `assets` is kept
    // as an explicit parameter to make the ownership contract visible at the
    // boundary and to support callers that already preflighted assets.
    if (!result.asset_manifest) {
        result.asset_manifest = std::make_shared<const chronon3d::assets::PreparedAssetManifest>(assets);
    }
    return result;
}

}  // namespace chronon3d::render_plan
