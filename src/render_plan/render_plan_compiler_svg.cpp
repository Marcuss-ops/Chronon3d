#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/text/text_path_composer.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
#include "render_plan_compiler_detail.hpp"
#include "../text/unicode/unicode_script.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::render_plan {
namespace {
template <typename Metrics>
std::optional<PlanDecodeError> append_merged_svg_text_layers(
    RenderPlan& plan,
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>>& pending_svg_text_layers,
    const std::vector<Metrics>& pending_svg_text_metrics,
    const std::vector<bool>& pending_svg_text_boundary_before) {
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>> shaped_text_layers;
    shaped_text_layers.reserve(pending_svg_text_layers.size());
    const auto compatible_paint_run = [](const auto& left, const auto& right) {
        const auto& a = left.first;
        const auto& b = right.first;
        const auto& al = left.second;
        const auto& bl = right.second;
        const auto source_layer = [](const std::string& id) {
            const auto marker = id.find("__svgtext_");
            return id.substr(0, marker);
        };
        return a.text_element_id == b.text_element_id &&
            source_layer(al.id) == source_layer(bl.id) &&
            a.group_ids == b.group_ids &&
            a.coordinate_scope_ids == b.coordinate_scope_ids &&
            a.text_anchor_scope_id == b.text_anchor_scope_id &&
            a.font_family == b.font_family && a.font_size == b.font_size &&
            a.font_weight == b.font_weight && a.font_style == b.font_style &&
            a.direction == b.direction &&
            a.text_anchor == b.text_anchor &&
            a.dominant_baseline == b.dominant_baseline &&
            a.baseline_shift == b.baseline_shift && a.opacity == b.opacity &&
            !a.fill_gradient && !b.fill_gradient && !a.stroke && !b.stroke &&
            !a.stroke_gradient && !b.stroke_gradient &&
            !a.fill_pattern && !b.fill_pattern &&
            !a.stroke_pattern && !b.stroke_pattern &&
            !a.text_path_id && !b.text_path_id &&
            !a.positioned_text_cluster && !b.positioned_text_cluster &&
            !b.x_explicit && !b.y_explicit &&
            a.x_coordinates.empty() && a.y_coordinates.empty() &&
            a.dx_coordinates.empty() && a.dy_coordinates.empty() &&
            a.transformed_position_coordinates.empty() &&
            a.transformed_delta_coordinates.empty() &&
            b.x_coordinates.empty() && b.y_coordinates.empty() &&
            b.dx_coordinates.empty() && b.dy_coordinates.empty() &&
            b.transformed_position_coordinates.empty() &&
            b.transformed_delta_coordinates.empty() &&
            !al.animation && !bl.animation && al.style && bl.style &&
            !al.style->fill_gradient && !bl.style->fill_gradient &&
            !al.style->stroke && !bl.style->stroke &&
            al.style->font_family == bl.style->font_family &&
            al.style->font_weight == bl.style->font_weight &&
            al.style->font_style == bl.style->font_style &&
            al.style->font_size == bl.style->font_size &&
            al.style->text_align == bl.style->text_align &&
            al.rotation == bl.rotation && al.scale == bl.scale &&
            al.enable_3d == bl.enable_3d &&
            al.position_dimensions == bl.position_dimensions &&
            al.opacity == bl.opacity &&
            std::abs(al.position[1] - bl.position[1]) <= 1.0f;
    };
    for (std::size_t begin = 0; begin < pending_svg_text_layers.size();) {
        std::size_t end = begin + 1;
        while (end < pending_svg_text_layers.size() &&
               pending_svg_text_boundary_before[end] &&
               compatible_paint_run(pending_svg_text_layers[end - 1],
                                    pending_svg_text_layers[end])) ++end;
        if (end - begin == 1) {
            shaped_text_layers.push_back(std::move(pending_svg_text_layers[begin]));
            begin = end;
            continue;
        }
        auto combined = pending_svg_text_layers[begin];
        combined.second.text_spans.clear();
        std::string text;
        float pen = 0.0f;
        float ink_right = 0.0f;
        struct PaintRange { std::size_t start; std::size_t end; Color color; };
        std::vector<PaintRange> paint_ranges;
        for (std::size_t index = begin; index < end; ++index) {
            const auto& run = pending_svg_text_layers[index].first;
            const std::size_t start = text.size();
            text += run.text;
            const auto& metrics = pending_svg_text_metrics[index];
            ink_right = std::max(ink_right, pen + metrics.ink_width);
            pen += metrics.advance;
            const auto color = parse_hex_color(run.fill);
            if (!color)
                return PlanDecodeError{"svg.text.fill",
                    "SVG text paint span has an invalid solid fill",
                    "svg_text_invalid", "render-plan-svg"};
            chronon3d::TextSpanOverride span;
            span.byte_start = start;
            span.byte_end = text.size();
            auto paint = *color;
            paint.a *= run.fill_opacity;
            paint_ranges.push_back({start, text.size(), paint});
        }
        combined.second.svg_glyph_paint_ranges.clear();
        combined.second.svg_glyph_paint_ranges.reserve(paint_ranges.size());
        for (const auto& range : paint_ranges) {
            combined.second.svg_glyph_paint_ranges.push_back(
                {range.start, range.end, range.color});
        }
        const auto grapheme_count = chronon3d::detail::grapheme_cluster_count(text);
        for (std::size_t grapheme = 0; grapheme < grapheme_count; ++grapheme) {
            const auto start = chronon3d::detail::grapheme_byte_offset_at(text, grapheme);
            const auto finish = chronon3d::detail::grapheme_byte_offset_at(text, grapheme + 1);
            const auto owner = std::find_if(paint_ranges.begin(), paint_ranges.end(),
                [start](const PaintRange& range) {
                    return start >= range.start && start < range.end;
                });
            if (owner == paint_ranges.end()) continue;
            chronon3d::TextSpanOverride span;
            span.byte_start = start;
            span.byte_end = finish;
            span.color = owner->color;
            if (!combined.second.text_spans.empty() &&
                combined.second.text_spans.back().color == span.color &&
                combined.second.text_spans.back().byte_end == span.byte_start) {
                combined.second.text_spans.back().byte_end = span.byte_end;
            } else {
                combined.second.text_spans.push_back(std::move(span));
            }
        }
        combined.first.text = text;
        combined.second.text = std::move(text);
        combined.second.size[0] = std::max(1.0f, std::max(pen, ink_right) + 1.0f);
        shaped_text_layers.push_back(std::move(combined));
        begin = end;
    }
    for (auto& pending : shaped_text_layers)
        plan.layers.push_back(std::move(pending.second));
    return std::nullopt;
}

text::unicode::Script first_strong_unicode_script(std::string_view value) {
    return text::unicode::first_strong_script(value);
}
} // namespace

namespace detail {
std::optional<PlanDecodeError> prepare_svg_layers(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
#if defined(CHRONON3D_ENABLE_SVG)
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>> pending_svg_text_layers;
    struct SvgTextMetrics {
        std::string font_asset;
        float advance{0.0f};
        float ink_width{0.0f};
        float ascent{0.0f};
        float descent{0.0f};
        float x_height{0.0f};
        float cap_height{0.0f};
        float line_height{0.0f};
        std::optional<float> script_baseline_offset;
        std::optional<float> central_baseline_offset;
        std::optional<float> hanging_baseline_offset;
        std::optional<float> ideographic_baseline_offset;
        std::optional<float> mathematical_baseline_offset;
        std::optional<float> before_edge_baseline_offset;
        std::optional<float> after_edge_baseline_offset;
    };
    std::vector<SvgTextMetrics> pending_svg_text_metrics;
    std::vector<bool> pending_svg_text_boundary_before;
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
            auto group_chain = element.groups;
            if (group_chain.empty() && element.group)
                group_chain.push_back(*element.group);
            if (group_chain.size() > 16)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG isolated group nesting exceeds the depth limit of 16",
                    "svg_resource_limit", "render-plan-svg"};
            const double svg_fps = plan.canvas.fps.fps();
            const auto frame_at_time = [&](float seconds) -> std::optional<std::int64_t> {
                const double frame = static_cast<double>(seconds) * svg_fps;
                const double rounded = std::round(frame);
                if (!std::isfinite(frame) || std::abs(frame - rounded) > 1e-5 ||
                    rounded < 0.0 || rounded > 1000000.0) return std::nullopt;
                return static_cast<std::int64_t>(rounded);
            };
            for (const auto& group : group_chain) {
                if (group.id == 0 || !std::isfinite(group.opacity) ||
                    group.opacity < 0.0f || group.opacity > 1.0f)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG isolated group is invalid: id must be nonzero and opacity in [0,1]",
                        "svg_unsupported_style", "render-plan-svg"};
                ImportedSubpathPlan::SvgGroupIsolationPlan planned_group{
                    group.id, group.opacity};
                if (group.opacity_animation) {
                    const auto& animation = *group.opacity_animation;
                    const auto first_frame = frame_at_time(animation.begin_seconds);
                    const auto last_frame = frame_at_time(
                        animation.begin_seconds + animation.duration_seconds);
                    if (animation.kind != assets::SvgAnimationKind::Opacity ||
                        animation.components != 1 || !first_frame || !last_frame ||
                        *last_frame <= *first_frame || animation.from[0] < 0.0f ||
                        animation.from[0] > 1.0f || animation.to[0] < 0.0f ||
                        animation.to[0] > 1.0f)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG group opacity animation is invalid or off the composition frame grid",
                            "svg_animation_timing_unsupported", "render-plan-svg"};
                    AnimationTrackPlan track;
                    track.property = "svg_group_opacity";
                    AnimationKeyframePlan from;
                    from.frame = Frame{*first_frame};
                    from.value = {animation.from[0]};
                    from.interpolation = animation.discrete ? easing_name::kHold : easing_name::kLinear;
                    AnimationKeyframePlan to;
                    to.frame = Frame{*last_frame};
                    to.value = {animation.to[0]};
                    track.keyframes = {std::move(from), std::move(to)};
                    planned_group.opacity_animation = std::move(track);
                }
                subpath.svg_groups.push_back(std::move(planned_group));
            }
            if (!subpath.svg_groups.empty())
                subpath.svg_group = subpath.svg_groups.back();
            for (const auto& geometry : element.vector_masks) {
                std::vector<ShapePathCommandPlan> commands;
                commands.reserve(geometry.commands.size());
                for (const auto& command : geometry.commands) {
                    ShapePathCommandPlan lowered;
                    const auto lower_point = [&](Vec2 point, std::array<float, 2>& destination) {
                        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                            std::abs(point.x) > 100000.0f || std::abs(point.y) > 100000.0f)
                            throw std::runtime_error("SVG element mask coordinate exceeds finite resource bounds");
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
                    commands.push_back(lowered);
                }
                subpath.svg_local_masks.push_back(std::move(commands));
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
                from.interpolation = svg_animation.discrete ? easing_name::kHold : easing_name::kLinear;
                AnimationKeyframePlan to;
                to.frame = Frame{*last_frame};
                to.value.assign(svg_animation.to.begin(),
                    svg_animation.to.begin() + svg_animation.components);
                track.keyframes = {std::move(from), std::move(to)};
                if (svg_animation.kind == assets::SvgAnimationKind::Translate ||
                    svg_animation.kind == assets::SvgAnimationKind::Scale ||
                    svg_animation.kind == assets::SvgAnimationKind::Rotate) {
                    const char* operation = svg_animation.kind == assets::SvgAnimationKind::Translate
                        ? "translate" : svg_animation.kind == assets::SvgAnimationKind::Scale
                        ? "scale" : "rotate";
                    subpath.svg_transform_animations.push_back(
                        ImportedSubpathPlan::SvgTransformAnimationPlan{
                            operation, std::move(track)});
                    continue;
                }
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
                track.easing = element.gradient_animation_discrete ? easing_name::kHold : easing_name::kLinear;
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
                    key.interpolation = morph.discrete ? easing_name::kHold : easing_name::kLinear;
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
        std::vector<bool> text_chunk_boundary_safe(imported.text_runs.size(), true);
        std::vector<text::unicode::Script> effective_text_scripts(
            imported.text_runs.size(), text::unicode::unknown_script);
        std::unordered_map<std::string, float> text_anchor_advances;
        const auto group_key_for = [&](const chronon3d::assets::SvgTextRun& run,
                                       std::size_t index) {
            return layer.id + "#" + (run.text_element_id.empty()
                ? (run.source_id.empty() ? "run_" + std::to_string(index) : run.source_id)
                : run.text_element_id);
        };
        for (std::size_t text_index = 0; text_index < imported.text_runs.size(); ++text_index) {
            const auto& run = imported.text_runs[text_index];
            auto script_code = first_strong_unicode_script(run.text);
            if (script_code == text::unicode::unknown_script && text_index > 0) {
                const auto& previous = imported.text_runs[text_index - 1];
                if (previous.text_element_id == run.text_element_id &&
                    previous.group_ids == run.group_ids &&
                    previous.coordinate_scope_ids == run.coordinate_scope_ids &&
                    previous.font_family == run.font_family &&
                    previous.font_size == run.font_size)
                    script_code = effective_text_scripts[text_index - 1];
            }
            effective_text_scripts[text_index] = script_code;
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
            if (family_file == "sans-serif" || family_file == "sans" || family_file == "system-ui" ||
                family_file == "Arial" || family_file == "Helvetica") {
                family_file = "Inter";
            } else if (family_file == "serif" || family_file == "Times" || family_file == "Times-New-Roman") {
                family_file = "FreeSerif";
            }
            if (family_file.empty() || family_file.find_first_of("/\\:") != std::string::npos)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG font-family is not a safe bundled font name",
                    "svg_font_unresolved", "render-plan-svg"};
            const std::string face = run.font_weight >= 600
                ? (run.font_style == "normal" ? "Bold" : "BoldItalic")
                : (run.font_style == "normal" ? "Regular" : "Italic");
            const std::array<std::string, 6> font_candidates{
                "fonts/" + family_file + "-" + face + ".ttf",
                "fonts/" + family_file + "-" + face + ".otf",
                "fonts/" + family_file + "-Regular.ttf",
                "fonts/" + family_file + ".ttf",
                "fonts/Inter-" + face + ".ttf",
                "fonts/Inter-Regular.ttf"};
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
            auto& resolved_metrics = text_metrics[text_index];
            resolved_metrics.font_asset = font_asset;
            resolved_metrics.advance = advance;
            resolved_metrics.ink_width = advance;
            resolved_metrics.ascent = metrics.ascent;
            resolved_metrics.descent = metrics.descent;
            resolved_metrics.x_height = metrics.x_height;
            resolved_metrics.cap_height = metrics.cap_height;
            resolved_metrics.line_height = metrics.line_height;
            if (script_code != text::unicode::unknown_script) {
                const char* script_name = text::unicode::short_name(script_code);
                if (script_name && std::strlen(script_name) == 4) {
                    const bool rtl_baseline = run.direction == "rtl" ||
                        (run.direction != "ltr" &&
                         text::unicode::is_right_to_left_script(script_code));
                    const auto baseline_offset = [&](std::string_view baseline) {
                        return svg_text_font_engine->get_open_type_baseline_offset(
                            font_spec, run.font_size, baseline, script_name,
                            rtl_baseline
                                ? chronon3d::TextDirection::RTL
                                : chronon3d::TextDirection::LTR);
                    };
                    resolved_metrics.script_baseline_offset = baseline_offset("auto");
                    resolved_metrics.central_baseline_offset = baseline_offset("central");
                    resolved_metrics.hanging_baseline_offset = baseline_offset("hanging");
                    resolved_metrics.ideographic_baseline_offset = baseline_offset("ideographic");
                    resolved_metrics.mathematical_baseline_offset = baseline_offset("mathematical");
                    resolved_metrics.before_edge_baseline_offset = baseline_offset("text-before-edge");
                    resolved_metrics.after_edge_baseline_offset = baseline_offset("text-after-edge");
                }
            }
        }
        const auto has_no_text_positioning = [](const auto& run) {
            return run.dx == 0.0f && run.dy == 0.0f && run.x_coordinates.empty() &&
                run.y_coordinates.empty() && run.dx_coordinates.empty() &&
                run.dy_coordinates.empty() &&
                run.transformed_position_coordinates.empty() &&
                run.transformed_delta_coordinates.empty() && run.rotate_degrees.empty() &&
                run.letter_spacing == 0.0f && run.word_spacing == 0.0f &&
                !run.text_length && !run.text_path_id;
        };
        const auto same_shaping_scope = [&](std::size_t left, std::size_t right) {
            const auto& a = imported.text_runs[left];
            const auto& b = imported.text_runs[right];
            return group_key_for(a, left) == group_key_for(b, right) &&
                a.coordinate_scope_ids == b.coordinate_scope_ids &&
                a.text_anchor_scope_id == b.text_anchor_scope_id &&
                !b.x_explicit && !b.y_explicit &&
                a.font_family == b.font_family && a.font_size == b.font_size &&
                a.font_weight == b.font_weight && a.font_style == b.font_style &&
                a.direction == b.direction &&
                a.text_anchor == b.text_anchor &&
                a.dominant_baseline == b.dominant_baseline &&
                a.baseline_shift == b.baseline_shift &&
                has_no_text_positioning(a) && has_no_text_positioning(b);
        };
        for (std::size_t begin = 0; begin < imported.text_runs.size();) {
            std::size_t end = begin + 1;
            while (end < imported.text_runs.size() &&
                   same_shaping_scope(end - 1, end)) ++end;
            if (has_no_text_positioning(imported.text_runs[begin])) {
                const auto& first = imported.text_runs[begin];
                chronon3d::FontSpec font_spec;
                font_spec.font_path = text_metrics[begin].font_asset;
                if (const auto path = resolver.resolve_logical(font_spec.font_path))
                    font_spec.font_path = path->string();
                font_spec.font_family = first.font_family;
                font_spec.font_weight = first.font_weight;
                font_spec.font_style = first.font_style;
                font_spec.font_size = first.font_size;
                std::string combined;
                std::vector<std::size_t> run_start_bytes(end - begin, 0);
                for (std::size_t index = begin; index < end; ++index) {
                    if (index > begin)
                        run_start_bytes[index - begin] = combined.size();
                    combined += imported.text_runs[index].text;
                }
                bool chunk_boundaries_safe = false;
                std::optional<text::unicode::Script> previous_script;
                bool mixed_scripts = false;
                for (std::size_t index = begin; index < end; ++index) {
                    const auto script = first_strong_unicode_script(
                        imported.text_runs[index].text);
                    if (script == text::unicode::unknown_script) continue;
                    if (previous_script && *previous_script != script)
                        mixed_scripts = true;
                    previous_script = script;
                }
                const auto primary_script = first_strong_unicode_script(combined);
                const bool right_to_left_script =
                    text::unicode::is_right_to_left_script(primary_script);
                if (mixed_scripts) {
                    // A single HarfBuzz buffer guesses one script from its
                    // first strong character. Measure the authored chunks
                    // independently across script boundaries; the canonical
                    // text shaper joins their glyphs with these byte ranges.
                    chunk_boundaries_safe = true;
                    for (std::size_t index = begin; index < end; ++index) {
                        const auto& run = imported.text_runs[index];
                        const auto shaped_run = svg_text_font_engine->shape_text(
                            run.text, font_spec, first.font_size);
                        if (!shaped_run) {
                            chunk_boundaries_safe = false;
                            break;
                        }
                        const auto placed = chronon3d::resolve_placed_glyph_run(
                            *shaped_run, 0.0f, run.text);
                        text_metrics[index].advance = placed.total_width;
                        float ink_right = 0.0f;
                        for (const auto& glyph : placed.glyphs)
                            ink_right = std::max(ink_right,
                                glyph.x + glyph.bbox_x1);
                        text_metrics[index].ink_width = std::max(
                            placed.total_width, ink_right);
                    }
                } else if (right_to_left_script) {
                    // RTL clusters are stored in logical byte order while
                    // their glyph positions run in visual order. Derive
                    // paint-chunk widths from logical cluster advances;
                    // x positions are not monotone and cannot be used as
                    // chunk boundaries as they are for LTR text.
                    const auto shaped = svg_text_font_engine->shape_text(
                        combined, font_spec, first.font_size);
                    if (shaped) {
                        const auto placed = chronon3d::resolve_placed_glyph_run(
                            *shaped, 0.0f, combined);
                        std::vector<std::size_t> byte_boundaries = run_start_bytes;
                        byte_boundaries.push_back(0);
                        byte_boundaries.push_back(combined.size());
                        std::sort(byte_boundaries.begin(), byte_boundaries.end());
                        byte_boundaries.erase(std::unique(byte_boundaries.begin(),
                            byte_boundaries.end()), byte_boundaries.end());
                        bool complete_boundaries = true;
                        for (std::size_t offset = 1; offset + 1 < byte_boundaries.size(); ++offset) {
                            if (std::none_of(placed.clusters.begin(), placed.clusters.end(),
                                [&](const auto& cluster) {
                                    return cluster.byte_offset == byte_boundaries[offset];
                                })) {
                                complete_boundaries = false;
                                break;
                            }
                        }
                        if (complete_boundaries) {
                            chunk_boundaries_safe = true;
                            for (std::size_t offset = 0; offset < run_start_bytes.size(); ++offset) {
                                const auto byte_start = run_start_bytes[offset];
                                const auto byte_end = offset + 1 < run_start_bytes.size()
                                    ? run_start_bytes[offset + 1] : combined.size();
                                float advance = 0.0f;
                                for (const auto& cluster : placed.clusters)
                                    if (cluster.byte_offset >= byte_start &&
                                        cluster.byte_offset < byte_end)
                                        advance += cluster.advance;
                                text_metrics[begin + offset].advance = advance;
                                text_metrics[begin + offset].ink_width = advance;
                            }
                        }
                    }
                } else {
                const auto shaped = svg_text_font_engine->shape_text(
                    combined, font_spec, first.font_size);
                if (shaped) {
                    const auto placed = chronon3d::resolve_placed_glyph_run(
                        *shaped, 0.0f, combined);
                    std::vector<std::optional<float>> run_starts(end - begin);
                    run_starts.front() = 0.0f;
                    bool complete_boundaries = true;
                    bool safe_grapheme_boundaries = true;
                    for (std::size_t offset = 1; offset < run_starts.size(); ++offset) {
                        const auto byte = run_start_bytes[offset];
                        const auto cluster = std::find_if(placed.clusters.begin(),
                            placed.clusters.end(), [byte](const auto& candidate) {
                                return candidate.byte_offset >= byte;
                            });
                        if (cluster == placed.clusters.end() ||
                            cluster->byte_offset != byte ||
                            cluster->start_glyph >= placed.glyphs.size()) {
                            complete_boundaries = false;
                            bool is_grapheme_edge = false;
                            const auto graphemes = chronon3d::detail::grapheme_cluster_count(combined);
                            for (std::size_t grapheme = 0; grapheme <= graphemes; ++grapheme) {
                                if (chronon3d::detail::grapheme_byte_offset_at(combined, grapheme) == byte) {
                                    is_grapheme_edge = true;
                                    break;
                                }
                            }
                            if (is_grapheme_edge) safe_grapheme_boundaries = false;
                            continue;
                        }
                        run_starts[offset] = placed.glyphs[cluster->start_glyph].x;
                    }
                    if (!complete_boundaries && safe_grapheme_boundaries) {
                        // The paint boundary falls inside one extended
                        // grapheme. Keep the authored paint spans together
                        // so shaping can retain the indivisible glyph
                        // cluster instead of reshaping its code points.
                        chunk_boundaries_safe = true;
                    } else if (complete_boundaries && std::isfinite(placed.total_width)) {
                        chunk_boundaries_safe = true;
                        for (std::size_t offset = 0; offset < run_starts.size(); ++offset) {
                            const float next = offset + 1 < run_starts.size()
                                ? *run_starts[offset + 1] : placed.total_width;
                            if (next >= *run_starts[offset]) {
                                text_metrics[begin + offset].advance =
                                    next - *run_starts[offset];
                                float ink_right = 0.0f;
                                const std::size_t byte_start = run_start_bytes[offset];
                                const std::size_t byte_end = offset + 1 < run_start_bytes.size()
                                    ? run_start_bytes[offset + 1] : combined.size();
                                for (const auto& glyph : placed.glyphs) {
                                    if (glyph.byte_offset < byte_start ||
                                        glyph.byte_offset >= byte_end) continue;
                                    ink_right = std::max(ink_right,
                                        glyph.x + glyph.bbox_x1 - *run_starts[offset]);
                                }
                                text_metrics[begin + offset].ink_width = std::max(
                                    text_metrics[begin + offset].advance, ink_right);
                            }
                        }
                    }
                }
                }
                if (!chunk_boundaries_safe)
                    for (std::size_t index = begin + 1; index < end; ++index)
                        text_chunk_boundary_safe[index] = false;
            }
            begin = end;
        }
        text_anchor_advances.clear();
        for (std::size_t text_index = 0; text_index < imported.text_runs.size(); ++text_index) {
            const auto& run = imported.text_runs[text_index];
            const auto anchor_scope = run.text_anchor_scope_id.empty()
                ? group_key_for(run, text_index) : layer.id + "#anchor#" + run.text_anchor_scope_id;
            text_anchor_advances[anchor_scope] += text_metrics[text_index].advance;
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
            if (run.dominant_baseline == "auto" ||
                run.dominant_baseline == "use-script")
                baseline_y += text_metrics[text_index].script_baseline_offset.value_or(0.0f);
            else if (run.dominant_baseline == "central")
                baseline_y += text_metrics[text_index].central_baseline_offset.value_or(
                    (text_metrics[text_index].ascent -
                     text_metrics[text_index].descent) * 0.5f);
            else if (run.dominant_baseline == "middle")
                baseline_y += text_metrics[text_index].x_height * 0.5f;
            else if (run.dominant_baseline == "hanging")
                baseline_y += text_metrics[text_index].hanging_baseline_offset.value_or(
                    text_metrics[text_index].ascent * 0.8f);
            else if (run.dominant_baseline == "ideographic")
                baseline_y += text_metrics[text_index].ideographic_baseline_offset.value_or(
                    text_metrics[text_index].descent);
            else if (run.dominant_baseline == "mathematical")
                baseline_y += text_metrics[text_index].mathematical_baseline_offset.value_or(
                    text_metrics[text_index].cap_height * 0.5f);
            else if (run.dominant_baseline == "text-before-edge")
                baseline_y += text_metrics[text_index].before_edge_baseline_offset.value_or(
                    text_metrics[text_index].ascent);
            else if (run.dominant_baseline == "text-after-edge")
                baseline_y += text_metrics[text_index].after_edge_baseline_offset.value_or(
                    -text_metrics[text_index].descent);
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
            text_layer.size = {std::max(1.0f,
                std::max(text_metrics[text_index].advance,
                         text_metrics[text_index].ink_width) + 1.0f),
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
            text_style.text_direction = run.direction;
            text_style.text_align = "left";
            text_style.font_size = run.font_size;
            text_style.fill = run.fill;
            text_style.fill_gradient = run.fill_gradient;
            text_style.fill_pattern = run.fill_pattern;
            text_style.fill_opacity = run.fill_opacity;
            if (run.stroke && run.stroke_width > 0.0f) {
                text_style.stroke = StrokeStyle{.color = *run.stroke,
                    .width = run.stroke_width,
                    .gradient = run.stroke_gradient,
                    .dash_array = run.stroke_dash_array,
                    .dash_offset = run.stroke_dash_offset,
                    .cap = run.stroke_cap == LineCap::Round ? "round"
                        : run.stroke_cap == LineCap::Square ? "square" : "butt",
                    .join = run.stroke_join == LineJoin::Round ? "round"
                        : run.stroke_join == LineJoin::Bevel ? "bevel" : "miter",
                    .miter_limit = run.stroke_miter_limit,
                    .opacity = run.stroke_opacity};
                text_style.stroke_pattern = run.stroke_pattern;
            }
            text_style.text_fit_mode = "none";
            text_layer.font = text_metrics[text_index].font_asset;
            text_layer.style = std::move(text_style);
            if (run.text_path_id && run.text_length)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textLength on textPath is unsupported by this bounded importer",
                    "svg_text_length_text_path_unsupported", "render-plan-svg"};
            if (!run.text_path_id) {
                const bool has_coordinate_lists = !run.x_coordinates.empty() ||
                    !run.y_coordinates.empty() || !run.dx_coordinates.empty() ||
                    !run.dy_coordinates.empty() ||
                    !run.transformed_position_coordinates.empty() ||
                    !run.transformed_delta_coordinates.empty() ||
                    !run.rotate_degrees.empty() || run.letter_spacing != 0.0f ||
                    run.word_spacing != 0.0f || run.text_length.has_value();
                if (run.text_length && (!run.x_coordinates.empty() ||
                    !run.y_coordinates.empty() || !run.dx_coordinates.empty() ||
                    !run.dy_coordinates.empty() ||
                    !run.transformed_position_coordinates.empty() ||
                    !run.transformed_delta_coordinates.empty()))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textLength cannot be combined with explicit coordinate lists in this bounded importer",
                        "svg_text_length_positioning_unsupported", "render-plan-svg"};
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
                    const float text_length_scale = run.text_length &&
                        run.length_adjust == "spacingAndGlyphs" && placed.total_width > 0.0f
                        ? *run.text_length / placed.total_width : 1.0f;
                    const std::size_t nonempty_cluster_count = static_cast<std::size_t>(
                        std::count_if(placed.clusters.begin(), placed.clusters.end(),
                            [](const auto& cluster) { return cluster.byte_len > 0; }));
                    const float text_length_gap = run.text_length &&
                        run.length_adjust == "spacing" && nonempty_cluster_count > 1
                        ? (*run.text_length - placed.total_width) /
                            static_cast<float>(nonempty_cluster_count - 1) : 0.0f;
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
                        const auto has_inner_transformed = [&](const auto& coordinates) {
                            const auto first_inner = character_index + 1;
                            const auto end = character_index + cluster_characters;
                            return std::any_of(coordinates.begin() + std::min(first_inner, coordinates.size()),
                                coordinates.begin() + std::min(end, coordinates.size()),
                                [](const auto&) { return true; });
                        };
                        if (has_inner_transformed(run.transformed_position_coordinates) ||
                            has_inner_transformed(run.transformed_delta_coordinates))
                            return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                                "].shape.asset", "SVG coordinate list splits a shaped text cluster; split the run at that character",
                                "svg_text_coordinate_cluster_unsupported", "render-plan-svg"};
                        if (character_index < run.transformed_position_coordinates.size()) {
                            cursor_x = run.transformed_position_coordinates[character_index][0];
                            cursor_y = run.transformed_position_coordinates[character_index][1];
                        } else {
                            if (character_index < run.x_coordinates.size())
                            cursor_x = run.x_coordinates[character_index];
                            if (character_index < run.y_coordinates.size())
                            cursor_y = run.y_coordinates[character_index];
                        }
                        if (character_index < run.dx_coordinates.size())
                            cursor_x += run.dx_coordinates[character_index];
                        if (character_index < run.dy_coordinates.size())
                            cursor_y += run.dy_coordinates[character_index];
                        if (character_index < run.transformed_delta_coordinates.size()) {
                            cursor_x += run.transformed_delta_coordinates[character_index][0];
                            cursor_y += run.transformed_delta_coordinates[character_index][1];
                        }
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
                            character_index < run.y_coordinates.size() ||
                            character_index < run.transformed_position_coordinates.size() ||
                            (run.text_length.has_value() && character_index == 0);
                        if (starts_positioned_chunk) {
                            const float cluster_advance = cluster.advance * text_length_scale;
                            float chunk_advance = cluster_advance;
                            std::size_t next_character = character_index + cluster_characters;
                            for (std::size_t next_cluster_index = cluster_index + 1;
                                 next_cluster_index < placed.clusters.size(); ++next_cluster_index) {
                                const auto& next_cluster = placed.clusters[next_cluster_index];
                                if (next_cluster.byte_len == 0 ||
                                    next_cluster.byte_offset + next_cluster.byte_len > run.text.size())
                                    continue;
                                if (next_character < run.x_coordinates.size() ||
                                    next_character < run.y_coordinates.size() ||
                                    next_character < run.transformed_position_coordinates.size())
                                    break;
                                chunk_advance += next_cluster.advance * text_length_scale;
                                next_character += utf8_count(run.text.substr(
                                    next_cluster.byte_offset, next_cluster.byte_len));
                            }
                            if (run.text_anchor == "middle") anchored_x -= chunk_advance * 0.5f;
                            else if (run.text_anchor == "end") anchored_x -= chunk_advance;
                        }
                        cluster_layer.position[0] = layer.position[0] + anchored_x;
                        cluster_layer.position[1] = layer.position[1] + cursor_y -
                            text_metrics[text_index].ascent;
                        const float cluster_advance = cluster.advance * text_length_scale;
                        cluster_layer.size[0] = std::max(1.0f, cluster_advance + 1.0f);
                        if (run.text_length && run.length_adjust == "spacingAndGlyphs") {
                            cluster_layer.scale[0] *= text_length_scale;
                            cluster_layer.scale_dimensions = std::max<std::size_t>(
                                cluster_layer.scale_dimensions, 2);
                        }
                        if (character_index < run.rotate_degrees.size()) {
                            cluster_layer.rotation[2] += run.rotate_degrees[character_index];
                            cluster_layer.rotation_dimensions = 3;
                        } else if (!run.rotate_degrees.empty()) {
                            cluster_layer.rotation[2] += run.rotate_degrees.back();
                            cluster_layer.rotation_dimensions = 3;
                        }
                        current_text_layers.push_back(pending_svg_text_layers.size());
                        pending_svg_text_layers.emplace_back(std::move(cluster_run),
                                                             std::move(cluster_layer));
                        pending_svg_text_metrics.push_back(text_metrics[text_index]);
                        pending_svg_text_boundary_before.push_back(
                            text_index == 0 || text_chunk_boundary_safe[text_index]);
                        float spacing = 0.0f;
                        if (cluster_index + 1 < placed.clusters.size()) {
                            spacing += run.letter_spacing + text_length_gap;
                            if (cluster_text == " " || cluster_text == "\t" ||
                                cluster_text == "\n" || cluster_text == "\r")
                                spacing += run.word_spacing;
                        }
                        cursor_x += cluster_advance + spacing;
                        character_index += cluster_characters;
                    }
                    continue;
                }
                current_text_layers.push_back(pending_svg_text_layers.size());
                pending_svg_text_layers.emplace_back(run, std::move(text_layer));
                pending_svg_text_metrics.push_back(text_metrics[text_index]);
                pending_svg_text_boundary_before.push_back(
                    text_index == 0 || text_chunk_boundary_safe[text_index]);
                continue;
            }
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
            const float initial_dx = run.dx_coordinates.empty() ? 0.0f : run.dx_coordinates.front();
            const float initial_dy = run.dy_coordinates.empty() ? 0.0f : run.dy_coordinates.front();
            path_spec.first_margin = start_offset + run.dx - initial_dx;
            if (run.text_anchor == "middle")
                path_spec.first_margin -= placed.total_width * 0.5f;
            else if (run.text_anchor == "end")
                path_spec.first_margin -= placed.total_width;
            path_spec.baseline_offset = run.dy - initial_dy + run.baseline_shift;
            if (run.dominant_baseline == "auto" ||
                run.dominant_baseline == "use-script")
                path_spec.baseline_offset += text_metrics[text_index].script_baseline_offset.value_or(0.0f);
            else if (run.dominant_baseline == "central")
                path_spec.baseline_offset += text_metrics[text_index].central_baseline_offset.value_or(
                    (text_metrics[text_index].ascent -
                     text_metrics[text_index].descent) * 0.5f);
            else if (run.dominant_baseline == "middle")
                path_spec.baseline_offset += text_metrics[text_index].x_height * 0.5f;
            else if (run.dominant_baseline == "hanging")
                path_spec.baseline_offset += text_metrics[text_index].hanging_baseline_offset.value_or(
                    text_metrics[text_index].ascent * 0.8f);
            else if (run.dominant_baseline == "ideographic")
                path_spec.baseline_offset += text_metrics[text_index].ideographic_baseline_offset.value_or(
                    text_metrics[text_index].descent);
            else if (run.dominant_baseline == "mathematical")
                path_spec.baseline_offset += text_metrics[text_index].mathematical_baseline_offset.value_or(
                    text_metrics[text_index].cap_height * 0.5f);
            else if (run.dominant_baseline == "text-before-edge")
                path_spec.baseline_offset += text_metrics[text_index].before_edge_baseline_offset.value_or(
                    text_metrics[text_index].ascent);
            else if (run.dominant_baseline == "text-after-edge")
                path_spec.baseline_offset += text_metrics[text_index].after_edge_baseline_offset.value_or(
                    -text_metrics[text_index].descent);
            path_spec.perpendicular_to_path = true;
            const auto path_placement = chronon3d::compose_text_on_path(
                placed, sampler, path_spec, run.text);
            if (path_placement.clusters.size() != placed.clusters.size())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.asset", "SVG textPath cluster placement failed",
                    "svg_text_invalid", "render-plan-svg"};
            std::size_t path_character_index = 0;
            float path_dx_accumulated = 0.0f;
            float path_dy_accumulated = 0.0f;
            const auto path_origin = sampler.sample_distance(0.0f);
            const auto absolute_path_coordinates = [&](std::size_t character_index)
                -> std::optional<std::array<float, 2>> {
                if (character_index < run.transformed_position_coordinates.size()) {
                    const auto& point = run.transformed_position_coordinates[character_index];
                    const float x = point[0] - path_origin.position.x;
                    const float y = point[1] - path_origin.position.y;
                    return std::array<float, 2>{
                        x * path_origin.tangent.x + y * path_origin.tangent.y,
                        x * path_origin.normal.x + y * path_origin.normal.y};
                }
                if (character_index < run.x_coordinates.size() ||
                    character_index < run.y_coordinates.size()) {
                    const float point_x = character_index < run.x_coordinates.size()
                        ? run.x_coordinates[character_index] : path_origin.position.x;
                    const float point_y = character_index < run.y_coordinates.size()
                        ? run.y_coordinates[character_index] : path_origin.position.y;
                    const float x = point_x - path_origin.position.x;
                    const float y = point_y - path_origin.position.y;
                    return std::array<float, 2>{
                        x * path_origin.tangent.x + y * path_origin.tangent.y,
                        x * path_origin.normal.x + y * path_origin.normal.y};
                }
                return std::nullopt;
            };
            const auto transformed_path_delta = [&](std::size_t character_index)
                -> std::optional<std::array<float, 2>> {
                if (character_index >= run.transformed_delta_coordinates.size())
                    return std::nullopt;
                const auto& delta = run.transformed_delta_coordinates[character_index];
                return std::array<float, 2>{
                    delta[0] * path_origin.tangent.x + delta[1] * path_origin.tangent.y,
                    delta[0] * path_origin.normal.x + delta[1] * path_origin.normal.y};
            };
            const auto path_utf8_count = [](std::string_view value) {
                std::size_t count = 0;
                for (const unsigned char byte : value)
                    if ((byte & 0xc0u) != 0x80u) ++count;
                return count;
            };
            for (std::size_t cluster_index = 0;
                 cluster_index < placed.clusters.size(); ++cluster_index) {
                const auto& cluster = placed.clusters[cluster_index];
                auto placement = path_placement.clusters[cluster_index];
                if (cluster.byte_len == 0 || cluster.byte_offset + cluster.byte_len > run.text.size())
                    continue;
                const std::string cluster_text = run.text.substr(
                    cluster.byte_offset, cluster.byte_len);
                if (cluster_text.empty()) continue;
                const auto cluster_characters = path_utf8_count(cluster_text);
                const auto has_inner_coordinate = [&](const auto& coordinates) {
                    const auto first_inner = path_character_index + 1;
                    const auto end = path_character_index + cluster_characters;
                    return std::any_of(coordinates.begin() + std::min(first_inner, coordinates.size()),
                        coordinates.begin() + std::min(end, coordinates.size()),
                        [](const auto&) { return true; });
                };
                if (has_inner_coordinate(run.x_coordinates) ||
                    has_inner_coordinate(run.y_coordinates) ||
                    has_inner_coordinate(run.transformed_position_coordinates) ||
                    has_inner_coordinate(run.dx_coordinates) ||
                    has_inner_coordinate(run.dy_coordinates) ||
                    has_inner_coordinate(run.transformed_delta_coordinates))
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                        "].shape.asset", "SVG textPath coordinate list splits a shaped cluster",
                        "svg_text_coordinate_cluster_unsupported", "render-plan-svg"};
                if (path_character_index > 0) {
                    if (path_character_index < run.dx_coordinates.size())
                        path_dx_accumulated += run.dx_coordinates[path_character_index];
                    if (path_character_index < run.dy_coordinates.size())
                        path_dy_accumulated += run.dy_coordinates[path_character_index];
                    if (const auto delta = transformed_path_delta(path_character_index)) {
                        path_dx_accumulated += delta->at(0);
                        path_dy_accumulated += delta->at(1);
                    }
                }
                const auto absolute_position = absolute_path_coordinates(path_character_index);
                const float adjusted_distance = absolute_position
                    ? absolute_position->at(0)
                    : placement.path_distance + path_dx_accumulated;
                if (adjusted_distance > sampler.total_length()) {
                    path_character_index += cluster_characters;
                    continue;
                }
                if (absolute_position || path_dx_accumulated != 0.0f ||
                    path_dy_accumulated != 0.0f) {
                    const auto sample = sampler.sample_distance(adjusted_distance);
                    placement.path_distance = adjusted_distance;
                    placement.position = sample.position + sample.normal *
                        (absolute_position ? absolute_position->at(1) :
                            path_spec.baseline_offset + path_dy_accumulated);
                    placement.rotation_deg = std::atan2(sample.tangent.y,
                        sample.tangent.x) * 57.29577951308232f;
                }
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
                if (path_character_index < run.rotate_degrees.size())
                    cluster_layer.rotation[2] += run.rotate_degrees[path_character_index];
                else if (!run.rotate_degrees.empty())
                    cluster_layer.rotation[2] += run.rotate_degrees.back();
                cluster_layer.rotation_dimensions = 3;
                current_text_layers.push_back(pending_svg_text_layers.size());
                pending_svg_text_layers.emplace_back(std::move(cluster_run),
                                                     std::move(cluster_layer));
                pending_svg_text_metrics.push_back(text_metrics[text_index]);
                pending_svg_text_boundary_before.push_back(
                    text_index == 0 || text_chunk_boundary_safe[text_index]);
                if (cluster_index + 1 < placed.clusters.size()) {
                    path_dx_accumulated += run.letter_spacing;
                    if (cluster_text == " " || cluster_text == "\t" ||
                        cluster_text == "\n" || cluster_text == "\r")
                        path_dx_accumulated += run.word_spacing;
                }
                path_character_index += cluster_characters;
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
                 operation.kind == PathOperatorKindPlan::BooleanUnion ||
                 operation.kind == PathOperatorKindPlan::Morph) && exact_matches != 1)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.operators", "SVG Merge/BooleanUnion/Morph requires an exact path element id, not a group target",
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
    if (auto error = append_merged_svg_text_layers(
            plan, pending_svg_text_layers, pending_svg_text_metrics,
            pending_svg_text_boundary_before))
        return error;
#else
    for (std::size_t index = 0; index < plan.layers.size(); ++index)
        if (plan.layers[index].shape && plan.layers[index].shape->svg_asset)
            return PlanDecodeError{"layers[" + std::to_string(index) + "].shape.asset",
                "Chronon was built without SVG import support", "svg_import_unavailable",
                "render-plan-svg"};
#endif
    return std::nullopt;
}
} // namespace detail
} // namespace chronon3d::render_plan
