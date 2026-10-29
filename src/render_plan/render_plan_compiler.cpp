#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/hdri_environment.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_entry_detail.hpp"
#include "effect_kind_coverage.hpp"
#include "audio_react_baker.hpp"
#include "audio_react_plan_baker.hpp"
#include "render_plan_effect_preparation.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace chronon3d::render_plan {

Result<PreparedRenderPlan, PlanDecodeError> compile_render_plan(
    const RenderPlan& input_plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options,
    const compiler::CompiledProgram* canonical_program,
    const RuntimeCameraSampleProvider* runtime_camera) {
    try {
        std::shared_ptr<const chronon3d::assets::HdriEnvV1> environment_probe;
        if (input_plan.environment_light) {
            const auto environment = chronon3d::assets::load_hdri_env_v1(
                resolver, input_plan.environment_light->probe_asset);
            if (!environment)
                return PlanDecodeError{"environment_light.probe_asset", environment.error().message,
                    "environment_asset_invalid", "render-plan-lighting"};
            for (std::size_t i = 0; i < input_plan.environment_light->irradiance_sh.size(); ++i) {
                const float supplied = input_plan.environment_light->irradiance_sh[i];
                const float stored = environment->irradiance_sh[i];
                const float tolerance = 1.0e-5f * std::max({1.0f, std::abs(supplied), std::abs(stored)});
                if (std::abs(supplied - stored) > tolerance)
                    return PlanDecodeError{"environment_light.irradiance_sh",
                        "RenderPlan SH values do not match the referenced HDRIENV1 asset",
                        "environment_sh_asset_mismatch", "render-plan-lighting"};
            }
            environment_probe = std::make_shared<const chronon3d::assets::HdriEnvV1>(*environment);
        }
        if (auto error = compiler_entry_detail::validate_compile_request(
                input_plan, runtime_camera))
            return *error;
        auto expanded_plan = input_plan;
        auto& plan = expanded_plan;
        detail::materialize_media_background_cards(plan);
        detail::bake_audio_react_plan(plan, resolver);
        if (auto asset_error = detail::prepare_data_feed_assets(plan, resolver))
            return *asset_error;
        if (auto lottie_error = detail::expand_lottie_assets(plan, resolver))
            return *lottie_error;
        if (auto svg_error = detail::prepare_svg_layers(plan, resolver))
            return *svg_error;
        // Lottie and SVG lowering can introduce parent, matte, text-mask, and
        // effect-source relations that did not exist in the authored V3 plan.
        // Revalidate the expanded graph before compiling it so generated
        // dependency cycles fail as typed plan errors instead of reaching the
        // render graph builder.
        if (const auto relation_error = validate_render_plan_layer_relations(plan))
            return *relation_error;
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
        auto effective_fingerprint_options = fingerprint_options;
        if (runtime_camera != nullptr)
            effective_fingerprint_options.runtime_camera_fingerprint = runtime_camera->fingerprint;
        const auto fingerprints = compute_render_job_fingerprint(
            plan, assets, effective_fingerprint_options);
        auto content_plan = plan;
        content_plan.job_id.clear();
        content_plan.output = {};
        const auto content_fingerprint =
            compute_render_plan_content_fingerprint(content_plan);
        const auto scene_content_fingerprint = runtime_camera == nullptr
            ? content_fingerprint
            : chronon3d::core::hash::HashBuilder{}
                  .add("chronon3d.runtime-camera-scene.v1")
                  .add(content_fingerprint)
                  .add(runtime_camera->fingerprint)
                  .finish();
        fingerprint_ms = chronon3d::profiling::duration_ms(
            fingerprint_t0, chronon3d::profiling::now());

        const auto canvas = CanvasInfo::from_dimensions(
            static_cast<float>(plan.canvas.width),
            static_cast<float>(plan.canvas.height));

        const auto media_t0 = chronon3d::profiling::now();
        auto resolved_media = detail::resolve_layer_media_assets(plan, resolver);
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
        auto prepared_text = detail::prepare_text_layers(
            plan, resolver, canvas, font_engine);
        auto prepared_effects = detail::prepare_layer_effects(plan, resolver);
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());

        CompositionDefinition definition;
        definition.composition = spec;
        const bool has_3d_layer = std::any_of(
            plan.layers.begin(), plan.layers.end(),
            [](const LayerPlan& layer) {
                return layer.enable_3d || layer.type == LayerType::Mesh;
            });
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
            .environment_probe = environment_probe,
            .resolved_video_paths = resolved_media.video_paths,
            .resolved_particle_sprite_paths = resolved_media.particle_sprite_paths,
            .prepared_texts = prepared_text.texts,
            .prepared_text_counters = prepared_text.counters,
            .prepared_text_bounds = prepared_text.bounds,
            .prepared_effects = prepared_effects,
            .font_engine = font_engine,
            .has_3d_layer = has_3d_layer,
            .camera_evaluator = runtime_camera != nullptr
                ? detail::make_camera_evaluator(plan, runtime_camera->sample)
                : detail::make_camera_evaluator(plan),
            .follow_path_evaluators = follow_path_evaluators,
            .resolved_ocio_config_path = resolved_ocio_config_path});

        // RenderPlan camera values are resolved directly by the scene
        // evaluator into Camera2_5D renderer state. They are not composition
        // Camera V1 descriptors.
        definition.scene_content_fingerprint = scene_content_fingerprint;

        CompiledComposition compiled_value;
        std::shared_ptr<const Composition> composition;
        double composition_stage_ms = 0.0;
        if (auto composition_error = compiler_entry_detail::compile_composition_view(
                definition, canonical_program,
                compiled_value, composition, composition_stage_ms))
            return *composition_error;
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



}  // namespace chronon3d::render_plan
