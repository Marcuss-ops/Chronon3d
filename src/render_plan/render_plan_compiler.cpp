#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include "render_plan_compiler_detail.hpp"

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace chronon3d::render_plan {

Result<PreparedRenderPlan, PlanDecodeError> compile_render_plan(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options,
    const compiler::CompiledProgram* canonical_program) {
    try {
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
        std::unordered_map<std::string, std::string> text_package_key_by_layer;
        if (plan.prepared_overlay_package) {
            text_package_key_by_layer.reserve(
                plan.prepared_overlay_package->overlays.size());
            for (const auto& overlay : plan.prepared_overlay_package->overlays) {
                if (overlay.kind == "text") {
                    text_package_key_by_layer.emplace(overlay.id, overlay.text_key);
                }
            }
        }
        std::unordered_map<
            std::string,
            std::shared_ptr<const chronon3d::PreparedTextRun>>
            prepared_text_runs_by_key;
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
            prepared_texts[index] = chronon3d::prepare_text(definition);
#ifdef CHRONON3D_USE_BLEND2D
            // Cross the prepare barrier only for genuinely static text. The
            // render-plan scene callback attaches `layer.text_animators`
            // after this loop, so pre-preparing an animated layer would cache
            // a layout snapshot with animators=[] and every later shape would
            // lose its selector before it reaches the renderer. Animated
            // layers are still shaped through the same canonical path, but
            // their PreparedTextRun is materialized after the animator stack
            // has been attached by LayerBuilder::build().
            if (layer.text_animators.empty()) {
                std::string package_key;
                if (const auto it = text_package_key_by_layer.find(layer.id);
                    it != text_package_key_by_layer.end()) {
                    package_key = it->second;
                }
                if (!package_key.empty()) {
                    if (const auto cached = prepared_text_runs_by_key.find(package_key);
                        cached != prepared_text_runs_by_key.end()) {
                        prepared_texts[index]->prepared_run = cached->second;
                    } else {
                        auto prepared_run = chronon3d::prepare_text_run(
                            *prepared_texts[index], font_engine.get());
                        prepared_texts[index]->prepared_run = prepared_run;
                        if (prepared_run) {
                            prepared_text_runs_by_key.emplace(
                                package_key, std::move(prepared_run));
                        }
                    }
                } else {
                    prepared_texts[index]->prepared_run =
                        chronon3d::prepare_text_run(
                            *prepared_texts[index], font_engine.get());
                }
                if (!prepared_texts[index]->prepared_run) {
                    throw std::runtime_error(
                        "text layer could not be prepared");
                }
            }
#endif
        }
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());

        CompositionDefinition definition;
        definition.composition = spec;
        const bool has_3d_layer = std::any_of(
            plan.layers.begin(), plan.layers.end(),
            [](const LayerPlan& layer) { return layer.enable_3d; });
        definition.scene = [plan, resolved_video_paths, prepared_texts, font_engine, has_3d_layer](
                               const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.font_engine(font_engine.get());

            // Render-plan text positions are converted to centre-relative
            // layer coordinates by apply_layer_primitives(). Keep the
            // approved GlowBasics camera at that canonical world origin; a
            // second canvas-centre translation here would move the text out
            // of frame.
            if (has_3d_layer) {
                Camera2_5D camera;
                camera.enabled = true;
                camera.optics_mode = CameraOpticsMode::Zoom;
                camera.position = Vec3{0.0f, 0.0f, -1000.0f};
                camera.zoom = 1000.0f;
                camera.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
                camera.point_of_interest_enabled = true;
                camera.is_animated = true;
                scene.camera().set(camera);
            }

            for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                const auto& layer = plan.layers[index];
                scene.layer(layer.id, [&](LayerBuilder& builder) {
                    builder.font_engine(font_engine.get());
                    switch (layer.type) {
                        case LayerType::Image: {
                            ImageParams params;
                            params.asset_path = layer.asset;
                            params.size = layer.size_dimensions == 2
                                ? Vec2{layer.size[0], layer.size[1]}
                                : Vec2{static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                            params.fit = detail::fit_mode(
                                layer.fit.value_or(FitMode::Cover));
                            params.radius = layer.radius.value_or(0.0f);
                            builder.image("image", std::move(params));
                            break;
                        }
                        case LayerType::Video: {
                            // The canvas frame rate is the source sampling
                            // rate: the video must advance in lockstep with the
                            // timeline. A guessed 30 fps played 24 fps content
                            // at 0.8x while the audio ran at 100%, desyncing
                            // from second 0. validate_render_budget() already
                            // guarantees a positive rate, and every consumer
                            // (VideoNode, DirectYuvProgram) falls back to the
                            // frame-input rate if it is ever non-positive.
                            const float canvas_fps = static_cast<float>(plan.canvas.fps.numerator) /
                                (plan.canvas.fps.denominator > 0 ? static_cast<float>(plan.canvas.fps.denominator) : 1.0f);
                            video::VideoSource source{
                                .path = resolved_video_paths[index],
                                .source_fps = canvas_fps};
                            // Render-plan video geometry is expressed as a
                            // top-left canvas box.  The canonical scene
                            // transform, consumed by both the graph and
                            // DirectYUV, is centre-relative.  Preserve the
                            // effective displayed size by making the source
                            // size the pre-scale box; this also prevents the
                            // DirectYUV path from applying `size` and
                            // `scale` twice.
                            if (layer.size_dimensions == 2) {
                                const float sx = std::abs(layer.scale[0]) > 1e-6f
                                    ? std::abs(layer.scale[0]) : 1.0f;
                                const float sy = std::abs(layer.scale[1]) > 1e-6f
                                    ? std::abs(layer.scale[1]) : 1.0f;
                                source.size = {
                                    layer.size[0] / sx,
                                    layer.size[1] / sy};
                            }
                            if (layer.loop)
                                source.loop_mode = video::VideoLoopMode::Loop;
                            builder.video(std::move(source));
                            break;
                        }
                        case LayerType::Color: {
                            RectParams params;
                            params.size = layer.size_dimensions == 2
                                ? Vec2{layer.size[0], layer.size[1]}
                                : Vec2{static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                            params.color = {layer.color[0], layer.color[1],
                                            layer.color[2], layer.color[3]};
                            if (layer.position_dimensions < 2) {
                                builder.pin_to(Anchor::Center);
                                params.pos = {-params.size.x * 0.5f, -params.size.y * 0.5f, 0.0f};
                            }
                            builder.rect("color", std::move(params));
                            break;
                        }
                        case LayerType::Text: {
                            builder.kind(LayerKind::Text);
                            if (!prepared_texts[index])
                                throw std::runtime_error(
                                    "text layer was not prepared");
                            auto& text_builder = builder.text_run(
                                "text",
                                *prepared_texts[index])
                                .font_engine(font_engine.get());
                            detail::apply_text_animators(text_builder, layer);
                            detail::apply_text_glow(builder, layer);
                            break;
                        }
                    }
                    detail::apply_layer_primitives(
                        builder, layer, plan.canvas.width, plan.canvas.height);
                    if (layer.type == LayerType::Video &&
                        layer.position_dimensions >= 2) {
                        const float sx = std::abs(layer.scale[0]) > 1e-6f
                            ? std::abs(layer.scale[0]) : 1.0f;
                        const float sy = std::abs(layer.scale[1]) > 1e-6f
                            ? std::abs(layer.scale[1]) : 1.0f;
                        const float displayed_w = layer.size_dimensions == 2
                            ? layer.size[0] : static_cast<float>(plan.canvas.width) * sx;
                        const float displayed_h = layer.size_dimensions == 2
                            ? layer.size[1] : static_cast<float>(plan.canvas.height) * sy;
                        builder.position({
                            layer.position[0] + displayed_w * 0.5f -
                                static_cast<float>(plan.canvas.width) * 0.5f,
                            layer.position[1] + displayed_h * 0.5f -
                                static_cast<float>(plan.canvas.height) * 0.5f,
                            layer.position[2]});
                    }
                });
            }
            return scene.build();
        };
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
