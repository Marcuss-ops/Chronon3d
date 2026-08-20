#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <chronon3d/render_plan/animation_intent.hpp>
#include <chronon3d/render_plan/visual_preset_materializer.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/prepared_text.hpp>
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

// Text overlay that has been materialized but NOT yet placed.  `layout` is
// set only when the overlay must go through the scene-wide layout resolver
// (entity cards, explicit job anchors, image presets, …); caption/word
// presets without an explicit anchor keep their native safe-area box and
// skip the resolver entirely.
struct MaterializedText {
    chronon3d::TextDefinition definition;
    std::optional<ResolvedLayoutIntent> layout;
};

// Build one OverlayLayoutRequest from a materializer-resolved layout intent.
// The intent (anchor + fallback + content bounds) is already concrete; this
// only fills the scene-wide temporal framing.  `priority` is intentionally
// left at the resolver default (stable original order) until a plan-level
// priority contract exists.
chronon3d::layout::OverlayLayoutRequest layout_request(
    const LayerPlan& layer,
    const ResolvedLayoutIntent& layout,
    std::int64_t composition_frames) {
    chronon3d::layout::OverlayLayoutRequest request;
    request.id = layer.id;
    request.intent = layout.intent;
    request.fallback_intents = layout.fallback_intents;
    request.width = layout.width;
    request.height = layout.height;
    request.safe_margin = layout.safe_margin;
    request.start_frame = layer.start_frame.value_or(Frame{0}).value;
    request.end_frame = request.start_frame +
        layer.duration_frames.value_or(Frame{composition_frames}).value;
    return request;
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

MaterializedText materialize_text(const LayerPlan& layer,
                                  const chronon3d::CanvasInfo& canvas,
                                  std::string_view style_profile,
                                  std::int64_t composition_frames,
                                  chronon3d::FontEngine& font_engine) {
    MaterializedText out;
    std::string base_preset = layer.preset;
    if (!layer.preset.empty()) {
        const auto& visual_registry = chronon3d::registry::builtin_visual_preset_registry();
        if (!visual_registry.contains(layer.preset)) {
            throw std::runtime_error("unknown visual preset '" + layer.preset +
                                     "' for text layer '" + layer.id + "'");
        }
        // Single materializer consumes the existing registry and resolves
            // style, font, animation and layout intent — the compiler no
            // longer re-maps preset ids or knows profile/role specifics.
            auto resolved = VisualPresetMaterializer{}.materialize(
                layer, canvas, style_profile, visual_registry,
                Frame{composition_frames});
            // The canonical text presets already own their safe-area box and
            // anchor. Replacing that pin with an absolute top-left coordinate
            // loses the preset's box anchor at large canvases and can clip
            // long captions. Resolve entity-card placement and explicit job
            // anchors for every modern overlay preset.
            if (layer.anchor || !layer.preset.empty()) {
                // Real content bounds (shaped width + font metrics + padding
                // + stroke/shadow) drive the resolver, NOT the canvas-fraction
                // layout box.  Falls back to the preset box when the font is
                // unavailable (measure_visual_bounds handles that case).
                const auto bounds = measure_visual_bounds(resolved, font_engine);
                resolved.layout.width = bounds.width;
                resolved.layout.height = bounds.height;
                out.layout = std::move(resolved.layout);
            }
            out.definition = std::move(resolved.text);
            return out;
    }
    if (base_preset == "title_centered")
        out.definition = chronon3d::presets::text::title_centered(layer.text, canvas);
    else if (base_preset == "subtitle_bottom")
        out.definition = chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
    else if (base_preset == "caption_safe_area")
        out.definition = chronon3d::presets::text::caption_safe_area(layer.text, canvas);
    else if (base_preset == "kinetic_word")
        out.definition = chronon3d::presets::text::kinetic_word(layer.text, canvas);
    else if (base_preset == "lower_third")
        out.definition = chronon3d::presets::text::lower_third(layer.text, canvas);
    else {
        out.definition.content.value = layer.text;
        out.definition.style.font.font_size = layer.font_size.value_or(48.0f);
        out.definition.style.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
        out.definition.frame.size = {
            layer.box_width.value_or(static_cast<float>(canvas.width)),
            layer.box_height.value_or(static_cast<float>(canvas.height))};
        out.definition.frame.align = chronon3d::TextAlign::Center;
        out.definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    }
    // Presets supply placement and styling defaults; an explicit plan font is
    // authoritative so asset materialization remains deterministic.
    if (!layer.font.empty())
        out.definition.style.font.font_path = layer.font;
    if (layer.font_size)
        out.definition.style.font.font_size = *layer.font_size;
    apply_text_animation_intent(out.definition, layer, std::nullopt,
                                composition_frames);
    return out;
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

void apply_layer_timing(chronon3d::LayerBuilder& builder, const LayerPlan& layer,
                        std::string_view style_profile,
                        Frame composition_frames,
                        bool position_already_resolved = false) {
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    // A visual preset has already gone through the scene-wide layout pass.
    // Re-applying the legacy plan position here would overwrite the resolved
    // placement and reintroduce the old coordinate-system bug.
    if (!position_already_resolved && layer.position_dimensions >= 2)
        builder.position({layer.position[0], layer.position[1], layer.position[2]});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    if (layer.animation) {
        if (layer.animation->start_frame) builder.from(*layer.animation->start_frame);
    }

    // Resolve the layer-level animation intent ONCE (registry defaults +
    // plan overrides).  Entry and exit are treated separately:
    //   ENTRY — one-shot motion preset with the resolved enter duration
    //           wired through MotionParameters (fade_in stays a pure entry;
    //           it is never converted into a double animation).
    //   EXIT  — the transition catalog owns the fade-out via
    //           transition_out(); exit_duration drives its length.
    std::optional<chronon3d::registry::AnimationSpec> preset_animation;
    if (!layer.preset.empty()) {
        const auto& registry = chronon3d::registry::builtin_visual_preset_registry();
        if (registry.contains(layer.preset)) {
            preset_animation =
                registry.get_for_profile(layer.preset, style_profile).animation;
        }
    }
    if (!layer.animation && !preset_animation) return;

    const auto resolved =
        resolve_animation(preset_animation, layer, composition_frames);
    if (!resolved.preset.empty()) {
        presets::MotionParameters motion_params;
        motion_params.duration = resolved.enter_duration;
        builder.motion(resolved.preset, motion_params);
    }
    if (resolved.exit_duration > Frame{0}) {
        chronon3d::LayerTransitionSpec exit_spec;
        exit_spec.transition_id = "crossfade";
        exit_spec.direction = chronon3d::TransitionDirection::None;
        exit_spec.duration = builder.frame_rate().to_seconds(resolved.exit_duration);
        exit_spec.easing = chronon3d::Easing::InCubic;
        builder.transition_out(std::move(exit_spec));
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
                prepared_store.error().message,
                prepared_store.error().code ==
                        chronon3d::assets::AssetPreflightErrorCode::MissingAsset
                    ? "MissingAsset" : "",
                "asset_resolver"};
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

        // Real text bounds require a shaping engine.  Construct one against
        // the job's resolver so per-line HarfBuzz measurement uses the same
        // asset mount as font materialization.
        chronon3d::FontEngine font_engine(resolver);

        // COLLECT — materialize every text/image overlay first, deferring
        // placement.  Layout requests accumulate here so a SINGLE resolver
        // pass can see all overlays (and the regions they occupy) at once,
        // instead of resolving each overlay against a half-built scene.
        std::vector<std::optional<chronon3d::TextDefinition>> prepared_texts(
            plan.layers.size());
        std::vector<std::optional<chronon3d::Vec2>> prepared_image_positions(
            plan.layers.size());
        // Resolved image geometry (preset defaults + plan overrides) so the
        // APPLY phase and scene builder consume the same box/fit the
        // materializer used — they no longer re-read the raw plan box/fit.
        std::vector<std::optional<ResolvedImageLayer>> prepared_images(
            plan.layers.size());
        struct LayoutTarget {
            std::size_t layer_index;
            bool is_image;  // true → image position; false → text placement
        };
        std::vector<chronon3d::layout::OverlayLayoutRequest> layout_requests;
        std::vector<LayoutTarget> layout_targets;
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type == LayerType::Text) {
                auto materialized = materialize_text(
                    layer, canvas, plan.style_profile,
                    plan.canvas.duration.value, font_engine);
                if (materialized.layout) {
                    layout_requests.push_back(layout_request(
                        layer, *materialized.layout, plan.canvas.duration.value));
                    layout_targets.push_back({index, false});
                }
                prepared_texts[index] = std::move(materialized.definition);
            } else if (layer.type == LayerType::Image && !layer.preset.empty()) {
                const auto& visual_registry =
                    chronon3d::registry::builtin_visual_preset_registry();
                if (!visual_registry.contains(layer.preset)) {
                    throw std::runtime_error("unknown visual preset '" + layer.preset +
                                             "' for image layer '" + layer.id + "'");
                }
                if (visual_registry.get(layer.preset).supported_layer !=
                    chronon3d::registry::VisualLayerKind::Image) {
                    throw std::runtime_error("visual preset '" + layer.preset +
                                             "' is not valid for image layer '" +
                                             layer.id + "'");
                }
                // Image presets flow through the SAME materializer —
                // they resolve anchor + animation instead of staying
                // purely descriptive registry entries.
                const auto resolved =
                    VisualPresetMaterializer{}.materialize_image(
                        layer, canvas, plan.style_profile, visual_registry,
                        Frame{plan.canvas.duration.value});
                layout_requests.push_back(layout_request(
                    layer, resolved.layout, plan.canvas.duration.value));
                layout_targets.push_back({index, true});
                prepared_images[index] = resolved;
            }
        }

        // SOLVE TOGETHER — one deterministic greedy pass places every overlay
        // in priority order (descending), avoiding the regions already taken.
        const auto placements =
            chronon3d::layout::OverlayLayoutResolver{}.solve(
                canvas.width, canvas.height,
                std::move(layout_requests), {});

        // APPLY — fail-loud, then write the resolved x/y back onto each layer.
        for (std::size_t i = 0; i < placements.size(); ++i) {
            const auto& placement = placements[i];
            if (!placement.valid) {
                throw std::runtime_error(placement.warning);
            }
            const auto& target = layout_targets[i];
            if (target.is_image) {
                // OverlayLayoutResolver returns a top-left pixel position.
                // LayerBuilder's 2D transform is a world-space CENTER and
                // the graph later adds the canvas half-size to unpinned 2D
                // layers. Convert exactly once at this boundary:
                //   top-left + half box - half canvas = centered world pos.
                const auto& image_layer = plan.layers[target.layer_index];
                const auto& resolved_image = *prepared_images[target.layer_index];
                const float box_width = resolved_image.box_width;
                const float box_height = resolved_image.box_height;
                const float world_x = placement.x + box_width * 0.5f -
                                      static_cast<float>(plan.canvas.width) * 0.5f;
                const float world_y = placement.y + box_height * 0.5f -
                                      static_cast<float>(plan.canvas.height) * 0.5f;
                prepared_image_positions[target.layer_index] =
                    chronon3d::Vec2{world_x + image_layer.offset[0],
                                    world_y + image_layer.offset[1]};
            } else {
                // The layout resolver owns the top-left box coordinate.  Use
                // an absolute top-left text anchor so the renderer cannot
                // apply a second implicit center/lower-third offset.
                auto& definition = *prepared_texts[target.layer_index];
                definition.frame.anchor = chronon3d::TextAnchor::TopLeft;
                definition.frame.align = chronon3d::TextAlign::Left;
                definition.frame.placement = chronon3d::TextPlacement{
                    chronon3d::TextPlacementKind::Absolute,
                    {placement.x + plan.layers[target.layer_index].offset[0],
                     placement.y + plan.layers[target.layer_index].offset[1]}};
            }
        }

        CompositionDefinition definition;
        definition.composition = spec;
        definition.scene = [plan, canvas, subtitles, resolved_video_paths,
                            prepared_texts, prepared_image_positions,
                            prepared_images](
                               const FrameContext& ctx) {
                SceneBuilder scene(ctx);

                // User-provided backgrounds are emitted first, so they sit
                // behind every overlay regardless of the owning layer's
                // position in the input plan. Each visual layer may carry a
                // different background asset.
                for (const auto& layer : plan.layers) {
                    if (!layer.background || layer.background->asset.empty()) continue;
                    scene.layer(layer.id + "__background", [&](LayerBuilder& builder) {
                        ImageParams params;
                        params.asset_path = layer.background->asset;
                        params.size = {static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                        params.fit = fit_mode(layer.background->fit.value_or(FitMode::Cover));
                        builder.image("background", std::move(params));
                        if (layer.background->opacity)
                            builder.opacity(*layer.background->opacity);
                        builder.from(layer.start_frame.value_or(Frame{0}));
                        builder.duration(layer.duration_frames.value_or(plan.canvas.duration));
                    });
                }
                for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                    const auto& layer = plan.layers[index];
                    scene.layer(layer.id, [&](LayerBuilder& builder) {
                        switch (layer.type) {
                            case LayerType::Image: {
                                ImageParams params;
                                params.asset_path = layer.asset;
                                // Preset-driven images resolve their box/fit
                                // through the materializer (preset defaults +
                                // plan overrides); preset-less image primitives
                                // still read the plan box/fit.
                                const bool preset_resolved =
                                    prepared_images[index].has_value();
                                params.size = {
                                    preset_resolved
                                        ? prepared_images[index]->box_width
                                        : layer.box_width.value_or(
                                              static_cast<float>(plan.canvas.width)),
                                    preset_resolved
                                        ? prepared_images[index]->box_height
                                        : layer.box_height.value_or(
                                              static_cast<float>(plan.canvas.height))};
                                params.fit = fit_mode(
                                    preset_resolved
                                        ? prepared_images[index]->fit
                                        : layer.fit.value_or(FitMode::Cover));
                                builder.image("image", std::move(params));
                                // Anchor-resolved position from the image
                                // materializer (explicit `position` in the
                                // plan still wins via apply_layer_timing).
                                if (prepared_image_positions[index]) {
                                    builder.position({
                                        prepared_image_positions[index]->x,
                                        prepared_image_positions[index]->y,
                                        0.0f});
                                }
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
                                if (!prepared_texts[index])
                                    throw std::runtime_error("text layer was not prepared");
                                // Render-plan text uses the canonical lowered
                                // payload so the graph emits TextRunNode and
                                // can use the existing Vulkan GlyphAtlas
                                // path.  The older TextDefinition builder
                                // emits a legacy SourceNode and would force a
                                // CPU draw_node fallback in strict Vulkan.
                                (void)builder.text_run(
                                    "text", chronon3d::prepare_text(*prepared_texts[index]));
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
                        apply_layer_timing(
                            builder, layer, plan.style_profile,
                            plan.canvas.duration,
                            layer.type == LayerType::Image &&
                                prepared_image_positions[index].has_value());
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
        prepared.render_budget = plan.budget;
        return prepared;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
