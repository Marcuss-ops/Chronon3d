#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::render_plan {
namespace {

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
    if (layer.preset == "title_centered")
        return chronon3d::presets::text::title_centered(layer.text, canvas);
    if (layer.preset == "subtitle_bottom")
        return chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
    if (layer.preset == "caption_safe_area")
        return chronon3d::presets::text::caption_safe_area(layer.text, canvas);
    if (layer.preset == "kinetic_word")
        return chronon3d::presets::text::kinetic_word(layer.text, canvas);
    if (layer.preset == "lower_third")
        return chronon3d::presets::text::lower_third(layer.text, canvas);

    chronon3d::TextDefinition definition;
    definition.content.value = layer.text;
    definition.style.font.font_path = layer.font;
    definition.style.font.font_size = layer.font_size.value_or(48.0f);
    definition.style.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
    definition.frame.size = {
        layer.box_width.value_or(static_cast<float>(canvas.width)),
        layer.box_height.value_or(static_cast<float>(canvas.height))};
    definition.frame.align = chronon3d::TextAlign::Center;
    definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
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
                .add(request_plan.output.crf);
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
    if (layer.animation) {
        if (layer.animation->start_frame) builder.from(*layer.animation->start_frame);
        if (layer.animation->duration_frames)
            builder.duration(*layer.animation->duration_frames);
        if (!layer.animation->preset.empty()) builder.motion(layer.animation->preset);
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

        CompositionSpec spec;
        spec.name = plan.job_id;
        spec.width = plan.canvas.width;
        spec.height = plan.canvas.height;
        spec.frame_rate = {plan.canvas.fps, 1};
        spec.duration = plan.canvas.duration;

        CompositionDefinition definition;
        definition.composition = spec;
        definition.scene = [plan, canvas, subtitles](const FrameContext& ctx) {
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
                            case LayerType::Video:
                                builder.video(video::VideoSource{.path = layer.source});
                                break;
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
