#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/layout/overlay_layout_resolver.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan {
namespace {

std::optional<chronon3d::Color> parse_hex_color(std::string_view value,
                                                 float alpha = 1.0f) {
    if (value.size() != 7 || value.front() != '#') return std::nullopt;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const int r1 = hex(value[1]), r2 = hex(value[2]);
    const int g1 = hex(value[3]), g2 = hex(value[4]);
    const int b1 = hex(value[5]), b2 = hex(value[6]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
        return std::nullopt;
    return chronon3d::Color{
        static_cast<float>(r1 * 16 + r2) / 255.0f,
        static_cast<float>(g1 * 16 + g2) / 255.0f,
        static_cast<float>(b1 * 16 + b2) / 255.0f,
        std::clamp(alpha, 0.0f, 1.0f)};
}

std::string visual_base_preset(std::string_view id) {
    if (id == "caption_card") return "caption_safe_area";
    if (id == "active_word_pop") return "kinetic_word";
    if (id == "subtitle_card") return "subtitle_bottom";
    if (id == "lower_third_safe" || id == "organization_card" ||
        id == "location_card") return "lower_third";
    return {};
}

chronon3d::TextPlacementKind placement_kind(std::string_view type) {
    using K = chronon3d::TextPlacementKind;
    if (type == "top_left") return K::TopLeft;
    if (type == "top_right") return K::TopRight;
    if (type == "bottom_left") return K::BottomLeft;
    if (type == "bottom_right" || type == "lower_third") return K::BottomLeft;
    if (type == "safe_area" || type == "safe_area_center") return K::SafeAreaCenter;
    if (type == "top") return K::SafeAreaTop;
    if (type == "bottom") return K::SafeAreaBottom;
    return K::CanvasCenter;
}

void apply_visual_style(chronon3d::TextDefinition& definition,
                        const chronon3d::registry::VisualStyle& style) {
    if (!style.font_family.empty()) definition.style.font.font_family = style.font_family;
    if (style.font_weight) definition.style.font.font_weight = *style.font_weight;
    if (style.font_size) definition.style.font.font_size = *style.font_size;
    if (const auto fill = parse_hex_color(style.fill)) definition.style.color = *fill;

    if (!style.stroke_color.empty() || style.stroke_width) {
        definition.style.paint.stroke_enabled = true;
        if (const auto color = parse_hex_color(style.stroke_color))
            definition.style.paint.stroke_color = *color;
        if (style.stroke_width) definition.style.paint.stroke_width = *style.stroke_width;
    }
    if (!style.shadow_color.empty() || style.shadow_blur || style.shadow_opacity) {
        chronon3d::TextShadow shadow;
        shadow.enabled = true;
        if (const auto color = parse_hex_color(style.shadow_color)) shadow.color = *color;
        if (style.shadow_blur) shadow.blur = *style.shadow_blur;
        if (style.shadow_opacity) shadow.opacity = *style.shadow_opacity;
        if (style.shadow_offset) {
            shadow.offset = {(*style.shadow_offset)[0], (*style.shadow_offset)[1]};
        }
        definition.style.shadows = {shadow};
    }
    if (!style.background_color.empty() || style.background_opacity ||
        style.radius || style.padding) {
        definition.style.box_style.enabled = true;
        const float opacity = style.background_opacity.value_or(1.0f);
        if (const auto color = parse_hex_color(style.background_color, opacity))
            definition.style.box_style.background = *color;
        if (style.radius) definition.style.box_style.radius = *style.radius;
        if (style.padding) {
            definition.style.box_style.padding = {
                (*style.padding)[0], (*style.padding)[1]};
        }
    }
}

void apply_visual_plan_overrides(chronon3d::TextDefinition& definition,
                                 const LayerPlan& layer) {
    if (layer.style) {
        const auto& style = *layer.style;
        if (!style.font_family.empty()) definition.style.font.font_family = style.font_family;
        if (style.font_weight) definition.style.font.font_weight = *style.font_weight;
        if (style.font_size) definition.style.font.font_size = *style.font_size;
        if (const auto fill = parse_hex_color(style.fill)) definition.style.color = *fill;
        if (style.stroke) {
            definition.style.paint.stroke_enabled = true;
            if (const auto color = parse_hex_color(style.stroke->color))
                definition.style.paint.stroke_color = *color;
            if (style.stroke->width) definition.style.paint.stroke_width = *style.stroke->width;
        }
        if (style.shadow) {
            chronon3d::TextShadow shadow;
            shadow.enabled = true;
            if (const auto color = parse_hex_color(style.shadow->color)) shadow.color = *color;
            shadow.opacity = style.shadow->opacity.value_or(1.0f);
            shadow.blur = style.shadow->blur.value_or(0.0f);
            if (style.shadow->offset_dimensions >= 2)
                shadow.offset = {style.shadow->offset[0], style.shadow->offset[1]};
            definition.style.shadows = {shadow};
        }
        if (style.background) {
            definition.style.box_style.enabled = true;
            const auto& background = *style.background;
            if (const auto color = parse_hex_color(
                    background.color, background.opacity.value_or(1.0f)))
                definition.style.box_style.background = *color;
            if (background.radius) definition.style.box_style.radius = *background.radius;
            if (background.padding_dimensions >= 2)
                definition.style.box_style.padding = {
                    background.padding[0], background.padding[1]};
        }
    }
    if (layer.anchor) {
        definition.frame.placement = chronon3d::TextPlacement{
            placement_kind(layer.anchor->type)};
        definition.frame.align = layer.anchor->alignment == "right"
            ? chronon3d::TextAlign::Right
            : layer.anchor->alignment == "center"
                ? chronon3d::TextAlign::Center : chronon3d::TextAlign::Left;
    }
    if (layer.font_asset) {
        definition.style.font.font_path = layer.font_asset->asset;
        if (!layer.font_asset->family.empty())
            definition.style.font.font_family = layer.font_asset->family;
        if (layer.font_asset->weight)
            definition.style.font.font_weight = *layer.font_asset->weight;
    }
    if (!layer.font.empty()) definition.style.font.font_path = layer.font;
    if (layer.font_size) definition.style.font.font_size = *layer.font_size;
}

void resolve_visual_layout(chronon3d::TextDefinition& definition,
                           const LayerPlan& layer,
                           const chronon3d::registry::VisualPresetDescriptor& visual,
                           const chronon3d::CanvasInfo& canvas) {
    chronon3d::layout::OverlayLayoutRequest request;
    request.id = layer.id;
    request.intent = visual.anchor.type;
    request.fallback_intents = visual.fallback_anchors;
    request.width = definition.frame.size.x;
    request.height = definition.frame.size.y;
    request.safe_margin = layer.anchor
        ? layer.anchor->safe_margin : visual.anchor.safe_margin;
    const auto resolved = chronon3d::layout::OverlayLayoutResolver{}.solve(
        canvas.width, canvas.height, {std::move(request)});
    if (resolved.empty() || !resolved.front().valid) {
        throw std::runtime_error(resolved.empty()
            ? "visual layout resolver returned no result"
            : resolved.front().warning);
    }
    // The layout resolver owns the top-left box coordinate.  Use an absolute
    // top-left text anchor so the renderer cannot apply a second implicit
    // center/lower-third offset.
    definition.frame.anchor = chronon3d::TextAnchor::TopLeft;
    definition.frame.placement = chronon3d::TextPlacement{
        chronon3d::TextPlacementKind::Absolute,
        {resolved.front().x, resolved.front().y}};
}

chronon3d::FitMode fit_mode(FitMode value) {
    switch (value) {
        case FitMode::Contain: return chronon3d::FitMode::Contain;
        case FitMode::Stretch: return chronon3d::FitMode::Stretch;
        case FitMode::None: return chronon3d::FitMode::None;
        case FitMode::Cover: return chronon3d::FitMode::Cover;
    }
    return chronon3d::FitMode::Cover;
}

chronon3d::TextDefinition text_definition(const LayerPlan& layer,
                                           const chronon3d::CanvasInfo& canvas) {
    chronon3d::TextDefinition definition;
    std::string base_preset = layer.preset;
    if (!layer.preset.empty()) {
        const auto& visual_registry = chronon3d::registry::builtin_visual_preset_registry();
        if (visual_registry.contains(layer.preset)) {
            const auto& visual = visual_registry.get(layer.preset);
            if (visual.supported_layer != chronon3d::registry::VisualLayerKind::Text)
                throw std::runtime_error("visual preset '" + layer.preset +
                                         "' cannot be used on a text layer");
            base_preset = visual_base_preset(layer.preset);
            if (base_preset.empty())
                throw std::runtime_error("visual preset '" + layer.preset +
                                         "' has no text materialization");
            if (layer.semantic_role.empty()) {
                // semantic_role is optional for compatibility; preset remains
                // the authoritative visual choice.
            }
            if (base_preset == "title_centered")
                definition = chronon3d::presets::text::title_centered(layer.text, canvas);
            else if (base_preset == "subtitle_bottom")
                definition = chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
            else if (base_preset == "caption_safe_area")
                definition = chronon3d::presets::text::caption_safe_area(layer.text, canvas);
            else if (base_preset == "kinetic_word")
                definition = chronon3d::presets::text::kinetic_word(layer.text, canvas);
            else
                definition = chronon3d::presets::text::lower_third(layer.text, canvas);
            apply_visual_style(definition, visual.style);
            definition.frame.placement = chronon3d::TextPlacement{
                placement_kind(visual.anchor.type)};
            definition.frame.align = visual.anchor.alignment == "right"
                ? chronon3d::TextAlign::Right
                : visual.anchor.alignment == "center"
                    ? chronon3d::TextAlign::Center : chronon3d::TextAlign::Left;
            apply_visual_plan_overrides(definition, layer);
            resolve_visual_layout(definition, layer, visual, canvas);
            return definition;
        }
    }
    if (base_preset == "title_centered")
        definition = chronon3d::presets::text::title_centered(layer.text, canvas);
    else if (base_preset == "subtitle_bottom")
        definition = chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
    else if (base_preset == "caption_safe_area")
        definition = chronon3d::presets::text::caption_safe_area(layer.text, canvas);
    else if (base_preset == "kinetic_word")
        definition = chronon3d::presets::text::kinetic_word(layer.text, canvas);
    else if (base_preset == "lower_third")
        definition = chronon3d::presets::text::lower_third(layer.text, canvas);
    else {
        definition.content.value = layer.text;
        definition.style.font.font_size = layer.font_size.value_or(48.0f);
        definition.style.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
        definition.frame.size = {
            layer.box_width.value_or(static_cast<float>(canvas.width)),
            layer.box_height.value_or(static_cast<float>(canvas.height))};
        definition.frame.align = chronon3d::TextAlign::Center;
        definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    }
    // Presets supply placement and styling defaults; an explicit plan font is
    // authoritative so asset materialization remains deterministic.
    if (!layer.font.empty())
        definition.style.font.font_path = layer.font;
    if (layer.font_size)
        definition.style.font.font_size = *layer.font_size;
    return definition;
}

std::uint64_t render_settings_fingerprint(
    const RenderPlanFingerprintSettings& settings) {
    return chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-settings.fingerprint.v1")
        .add(settings.width)
        .add(settings.height)
        .add(settings.antialiasing_samples)
        .add(settings.ssaa_factor)
        .add(settings.motion_blur)
        .add(settings.dirty_rects)
        .add(settings.deterministic)
        .add(settings.force_scalar_normal_blend)
        .add(settings.dirty_bitmask)
        .add(settings.dirty_tiles)
        .add(settings.parallel_tiles)
        .add(settings.tile_size)
        .add(settings.tile_dirty_ratio_threshold)
        .add(settings.optimize_compositing)
        .finish();
}

RenderJobFingerprint render_job_fingerprint(
    const RenderPlan& plan,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const RenderPlanFingerprintOptions& options) {
    // Job identifiers and destination paths are routing metadata. They are
    // deliberately excluded so absolute machine paths, temp directories, and
    // output renames cannot change the content identity.
    auto content_plan = plan;
    content_plan.job_id.clear();
    content_plan.output = {};
    auto request_plan = plan;
    request_plan.job_id.clear();
    // Destination paths are machine-local routing metadata, not identity.
    // Keep format/codec/bitrate/crf in the request fingerprint.
    request_plan.output.path.clear();

    const auto content_hash = compute_render_plan_content_fingerprint(content_plan);
    const auto request_hash = compute_render_plan_content_fingerprint(request_plan);
    const auto settings_hash = render_settings_fingerprint(options.render_settings);
    const auto material = [&](std::string_view domain, std::uint64_t plan_hash,
                              bool include_output_settings) {
        auto hash = chronon3d::core::hash::HashBuilder{}
            .add(domain)
            .add(options.schema_version)
            .add(options.engine_compatibility_version)
            .add(plan_hash)
            .add(settings_hash)
            .add_bytes(assets.manifest_digest().bytes.data(),
                       assets.manifest_digest().bytes.size());
        if (include_output_settings) {
            hash.add_enum(request_plan.output.format)
                .add_enum(request_plan.output.codec)
                .add(request_plan.output.bitrate)
                .add(request_plan.output.crf)
                .add(request_plan.output.profile_id);
        }
        return std::to_string(hash.finish());
    };
    return {
        chronon3d::assets::sha256_string(
            material("chronon.render-content.v2", content_hash, false)),
        chronon3d::assets::sha256_string(
            material("chronon.render-request.v2", request_hash, true))};
}

void apply_layer_timing(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (layer.position_dimensions >= 2)
        builder.position({layer.position[0], layer.position[1], layer.position[2]});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    if (layer.animation) {
        if (layer.animation->start_frame) builder.from(*layer.animation->start_frame);
        if (layer.animation->duration_frames)
            builder.duration(*layer.animation->duration_frames);
        if (!layer.animation->preset.empty()) builder.motion(layer.animation->preset);
    } else if (!layer.preset.empty()) {
        const auto& registry = chronon3d::registry::builtin_visual_preset_registry();
        if (registry.contains(layer.preset)) {
            const auto& animation = registry.get(layer.preset).animation;
            if (!animation.preset.empty()) builder.motion(animation.preset);
        }
    }
}

}  // namespace

Result<PreparedRenderPlan, PlanDecodeError>
compile_render_plan(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options) {
    try {
        if (const auto budget_error = validate_render_budget(plan)) {
            return *budget_error;
        }
        auto prepared_store = chronon3d::assets::prepare_asset_store(plan, resolver);
        if (!prepared_store) {
            return PlanDecodeError{
                "assets." + prepared_store.error().logical_path,
                prepared_store.error().message};
        }
        auto resources = std::move(prepared_store).value();
        auto assets = resources.manifest();
        const auto fingerprints = render_job_fingerprint(
            plan, assets, fingerprint_options);
        auto content_plan = plan;
        content_plan.job_id.clear();
        content_plan.output = {};
        const auto content_fingerprint =
            compute_render_plan_content_fingerprint(content_plan);
        const auto canvas = CanvasInfo::from_dimensions(
            static_cast<float>(plan.canvas.width),
            static_cast<float>(plan.canvas.height));
        std::vector<std::optional<presets::text::SubtitleTrack>> subtitles(
            plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::SubtitleTrack) continue;
            const auto subtitle = resources.find(
                std::filesystem::path(layer.source).lexically_normal().generic_string(),
                chronon3d::assets::PreparedAssetKind::Subtitle);
            if (!subtitle || subtitle->bytes.empty()) {
                throw std::runtime_error("subtitle asset was not prepared: " + layer.source);
            }
            const std::string raw(
                reinterpret_cast<const char*>(subtitle->bytes.data()),
                subtitle->bytes.size());
            if (layer.subtitle_format == SubtitleFormat::Vtt)
                subtitles[index] = presets::text::subtitle_from_vtt(raw);
            else if (layer.subtitle_format == SubtitleFormat::Json)
                subtitles[index] = presets::text::subtitle_from_json(raw);
            else
                subtitles[index] = presets::text::subtitle_from_srt(raw);
        }

        // Resolve video source logical paths to filesystem paths up front.
        // VideoNode's decoder opens the source path directly
        // (avformat_open_input), so it needs the resolver-mounted absolute
        // path — unlike image/font assets, which are re-resolved through the
        // runtime resolver during resource preparation. The content
        // fingerprint still derives from the plan's logical paths, so
        // machine-local roots never leak into identity.
        std::vector<std::string> resolved_video_paths(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Video) continue;
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(layer.source));
            if (!resolved) {
                throw std::runtime_error(
                    "video source was not prepared: " + layer.source);
            }
            resolved_video_paths[index] = resolved->string();
        }

        CompositionSpec spec;
        spec.name = plan.job_id;
        spec.width = plan.canvas.width;
        spec.height = plan.canvas.height;
        spec.frame_rate = {plan.canvas.fps, 1};
        spec.duration = plan.canvas.duration;

        CompositionDefinition definition;
        definition.composition = spec;
        definition.scene = [plan, canvas, subtitles, resolved_video_paths](const FrameContext& ctx) {
                SceneBuilder scene(ctx);
                for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                    const auto& layer = plan.layers[index];
                    scene.layer(layer.id, [&](LayerBuilder& builder) {
                        switch (layer.type) {
                            case LayerType::Image: {
                                ImageParams params;
                                params.asset_path = layer.asset;
                                params.size = {
                                    layer.box_width.value_or(static_cast<float>(plan.canvas.width)),
                                    layer.box_height.value_or(static_cast<float>(plan.canvas.height))};
                                params.fit = fit_mode(layer.fit.value_or(FitMode::Cover));
                                builder.image("image", std::move(params));
                                break;
                            }
                            case LayerType::Video: {
                                video::VideoSource source{
                                    .path = resolved_video_paths[index]};
                                if (layer.loop)
                                    source.loop_mode = video::VideoLoopMode::Loop;
                                builder.video(std::move(source));
                                break;
                            }
                            case LayerType::Color: {
                                RectParams params;
                                params.size = {static_cast<float>(plan.canvas.width),
                                               static_cast<float>(plan.canvas.height)};
                                params.color = {layer.color[0], layer.color[1],
                                                layer.color[2], layer.color[3]};
                                builder.rect("color", std::move(params));
                                break;
                            }
                            case LayerType::Text:
                                builder.kind(LayerKind::Text);
                                builder.text("text", text_definition(layer, canvas));
                                break;
                            case LayerType::SubtitleTrack: {
                                if (!subtitles[index])
                                    throw std::runtime_error("subtitle asset was not prepared");
                                authoring::Layer authoring_layer(builder, canvas);
                                auto track = authoring_layer.subtitles(*subtitles[index]);
                                track.preset(layer.preset.empty() ? "minimal_white" : layer.preset)
                                    .font(layer.font, layer.font_size.value_or(48.0f))
                                    .build();
                                break;
                            }
                        }
                        apply_layer_timing(builder, layer);
                    });
                }
                return scene.build();
            };
        definition.scene_content_fingerprint = content_fingerprint;

        // The explicit definition is compiled first and is the sole
        // canonical preparation path.  The legacy Composition below is only
        // a compatibility view whose callback delegates to that immutable
        // compiled value for consumers that have not yet migrated to the
        // direct CompiledComposition entry point.
        auto compiled = chronon3d::compile_composition(definition, {});
        if (!compiled) {
            return PlanDecodeError{"composition", compiled.error().message};
        }

        auto compiled_value = std::move(compiled).value();
        compiled_value.asset_manifest =
            std::make_shared<const chronon3d::assets::PreparedAssetManifest>(assets);
        compiled_value.render_budget = plan.budget;
        auto compiled_view = std::make_shared<const CompiledComposition>(
            compiled_value);
        auto composition = std::make_shared<Composition>(
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
        PreparedRenderPlan prepared;
        prepared.compiled_composition = std::move(compiled_value);
        prepared.composition = std::shared_ptr<const Composition>(std::move(composition));
        prepared.assets = std::move(assets);
        prepared.resources = std::move(resources);
        prepared.fingerprint = fingerprints;
        prepared.job_id = plan.job_id;
        prepared.canvas = plan.canvas;
        prepared.output = plan.output;
        prepared.audio_tracks = plan.audio_tracks;
        prepared.render_budget = plan.budget;
        return prepared;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
