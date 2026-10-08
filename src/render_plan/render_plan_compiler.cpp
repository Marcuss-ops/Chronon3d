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

// A light's projected CPU shadow is lowered onto every earlier layer that
// accepts shadows. The existing drop-shadow effect owns alpha extraction,
// blur and premultiplied compositing, so this pass only derives its projected
// offset and parameters from the light/caster pair.
void materialize_light_shadows(RenderPlan& plan) {
    for (std::size_t light_index = 0; light_index < plan.layers.size(); ++light_index) {
        const auto& light_layer = plan.layers[light_index];
        if (light_layer.type != LayerType::Light || !light_layer.light ||
            !light_layer.light->casts_shadow)
            continue;
        const auto& light = *light_layer.light;
        for (std::size_t caster_index = 0; caster_index < light_index; ++caster_index) {
            auto& caster = plan.layers[caster_index];
            if (caster.type == LayerType::Light || caster.type == LayerType::Adjustment ||
                (caster.casts_shadows && !*caster.casts_shadows) ||
                (caster.accepts_shadows && !*caster.accepts_shadows))
                continue;
            const float dx = light.kind == LightLayerPlan::Kind::Directional
                ? light.direction[0] : caster.position[0] - light_layer.position[0];
            const float dy = light.kind == LightLayerPlan::Kind::Directional
                ? light.direction[1] : caster.position[1] - light_layer.position[1];
            const float distance = std::hypot(dx, dy);
            const float depth = std::abs(caster.position[2] - light_layer.position[2]);
            const float planar_projection = light.kind == LightLayerPlan::Kind::Directional
                ? light.radius * 0.15f
                : std::min(light.range > 0.0f ? light.range : light.radius, distance) * 0.25f;
            const float projection = std::max(depth, planar_projection) + light.shadow_bias;
            const float nx = distance > 1e-5f ? dx / distance : 0.0f;
            const float ny = distance > 1e-5f ? dy / distance :
                (light.kind == LightLayerPlan::Kind::Directional ? 0.0f : 1.0f);
            LayerPlan::EffectPlan shadow;
            shadow.kind = LayerPlan::EffectKindPlan::DropShadow;
            shadow.radius = light.shadow_blur;
            shadow.offset = {nx * projection, ny * projection};
            shadow.effect_color = {0.0f, 0.0f, 0.0f,
                std::clamp(light.intensity * 0.45f, 0.0f, 1.0f)};
            caster.effects.push_back(std::move(shadow));
        }
    }
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
        materialize_light_shadows(plan);
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
            if (imported.elements.empty())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG contains no path geometry", "svg_empty_geometry", "render-plan-svg"};
            if (imported.elements.size() > 4096)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG styled path count exceeds 4096", "svg_resource_limit", "render-plan-svg"};
            auto& shape = *layer.shape;
            if (shape.operators.size() != 0)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.operators",
                    "SVG asset paths cannot be combined with path operators",
                    "svg_operator_conflict", "render-plan-svg"};
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
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(definition.style.font.font_path));
            if (!resolved) {
                throw std::runtime_error(
                    "font asset was not prepared: " +
                    definition.style.font.font_path);
            }
            definition.style.font.font_path = resolved->string();
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
