#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/effects/cube_lut.hpp>
#include <cmath>
#include <string>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/color_utils.hpp>

#include "render_plan_compiler_detail.hpp"

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include "../scene/camera/camera_v1/camera_program_sources.hpp"
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
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
        std::vector<std::vector<EffectParams>> prepared_effects(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            for (const auto& effect : plan.layers[index].effects) {
                if (effect.kind == LayerPlan::EffectKindPlan::Lut3D) {
                    const auto resolved = resolver.resolve_logical(std::filesystem::path(effect.asset));
                    if (!resolved) throw std::runtime_error("3D LUT asset was not prepared: " + effect.asset);
                    std::ifstream file(*resolved, std::ios::binary);
                    if (!file) throw std::runtime_error("failed to open 3D LUT asset: " + effect.asset);
                    std::string contents((std::istreambuf_iterator<char>(file)), {});
                    if (contents.size() > 8U * 1024U * 1024U)
                        throw std::runtime_error("3D LUT asset exceeds the 8 MiB parser limit: " + effect.asset);
                    prepared_effects[index].emplace_back(effects::parse_cube_lut(contents, effect.amount));
                } else if (effect.kind == LayerPlan::EffectKindPlan::Echo) {
                    prepared_effects[index].emplace_back(EchoParams{
                        .echoes = effect.echoes, .decay = effect.decay,
                        .time_offset_frames = effect.time_offset_frames});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Keying) {
                    KeyingParams keying;
                    keying.mode = effect.keying_mode == LayerPlan::KeyingModePlan::Luma
                        ? KeyingMode::Luma : KeyingMode::Chroma;
                    keying.key_color = Color{effect.key_color[0], effect.key_color[1], effect.key_color[2], 1.0f};
                    keying.threshold = effect.threshold;
                    keying.softness = effect.softness;
                    keying.spill = effect.spill;
                    keying.clip_black = effect.clip_black;
                    keying.clip_white = effect.clip_white;
                    keying.choke = effect.choke;
                    keying.garbage_x = effect.garbage_rect[0];
                    keying.garbage_y = effect.garbage_rect[1];
                    keying.garbage_w = effect.garbage_rect[2];
                    keying.garbage_h = effect.garbage_rect[3];
                    keying.garbage_feather = effect.garbage_feather;
                    prepared_effects[index].emplace_back(keying);
                } else if (effect.kind == LayerPlan::EffectKindPlan::Threshold) {
                    prepared_effects[index].emplace_back(ThresholdParams{
                        .threshold = effect.threshold,
                        .softness = effect.threshold_softness});
                } else if (effect.kind == LayerPlan::EffectKindPlan::GaussianBlur) {
                    prepared_effects[index].emplace_back(BlurParams{.radius = effect.radius});
                } else if (effect.kind == LayerPlan::EffectKindPlan::DropShadow) {
                    prepared_effects[index].emplace_back(DropShadowParams{
                        .offset = Vec2{effect.offset[0], effect.offset[1]},
                        .color = Color{effect.effect_color[0], effect.effect_color[1],
                                       effect.effect_color[2], effect.effect_color[3]},
                        .radius = effect.radius});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Bloom) {
                    prepared_effects[index].emplace_back(BloomParams{
                        .threshold = effect.bloom_threshold,
                        .radius = effect.radius,
                        .intensity = effect.bloom_intensity});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Glow) {
                    prepared_effects[index].emplace_back(GlowParams{
                        .radius = effect.radius,
                        .intensity = effect.glow_intensity,
                        .color = Color{effect.effect_color[0], effect.effect_color[1],
                                       effect.effect_color[2], effect.effect_color[3]}});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Noise) {
                    prepared_effects[index].emplace_back(NoiseParams{
                        .amount = effect.amount,
                        .seed = effect.seed,
                        .animated = effect.animated,
                        .color_mode = effect.rgb_noise ? NoiseColorMode::RGB
                                                       : NoiseColorMode::Monochrome,
                        .size = effect.noise_size,
                        .shadow_response = effect.grain_shadow_response,
                        .midtone_response = effect.grain_midtone_response,
                        .highlight_response = effect.grain_highlight_response});
                } else if (effect.kind == LayerPlan::EffectKindPlan::FractalNoise) {
                    prepared_effects[index].emplace_back(FractalNoiseParams{
                        .seed = effect.seed,
                        .octaves = effect.octaves,
                        .frequency = effect.frequency,
                        .amplitude = effect.amplitude,
                        .evolution = effect.evolution,
                        .contrast = effect.contrast,
                        .brightness = effect.brightness,
                        .color_mode = effect.rgb_noise ? NoiseColorMode::RGB
                                                       : NoiseColorMode::Monochrome});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Sharpen) {
                    prepared_effects[index].emplace_back(SharpenParams{
                        .amount = effect.amount, .radius = effect.radius, .threshold = effect.threshold});
                } else if (effect.kind == LayerPlan::EffectKindPlan::TurbulentDisplace) {
                    prepared_effects[index].emplace_back(TurbulentDisplaceParams{
                        .amount=effect.amount,.size=effect.warp_size,.evolution=effect.warp_evolution,
                        .evolution_speed=effect.warp_speed,.complexity=effect.warp_complexity});
                } else if (effect.kind == LayerPlan::EffectKindPlan::WaveWarp) {
                    prepared_effects[index].emplace_back(WaveWarpParams{
                        .type=static_cast<WaveWarpParams::WaveType>(effect.wave_type),
                        .direction=Vec2{effect.warp_direction[0],effect.warp_direction[1]},
                        .amplitude=effect.amount,.wavelength=effect.warp_wavelength,
                        .speed=effect.warp_speed,.phase=effect.warp_phase});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Ripple) {
                    prepared_effects[index].emplace_back(RippleParams{
                        .center=Vec2{effect.warp_center[0],effect.warp_center[1]},
                        .radius=effect.warp_radius,.wave_speed=effect.warp_speed,
                        .width=effect.warp_width,.wave_height=effect.warp_height,.damping=effect.warp_damping});
                } else if (effect.kind == LayerPlan::EffectKindPlan::DisplacementMap) {
                    prepared_effects[index].emplace_back(DisplacementMapParams{
                        .map_layer=effect.map_layer,.intensity_x=effect.map_intensity_x,
                        .intensity_y=effect.map_intensity_y,
                        .channel=static_cast<DisplacementMapParams::Channel>(effect.map_channel)});
                } else if (effect.kind == LayerPlan::EffectKindPlan::CornerPin) {
                    prepared_effects[index].emplace_back(CornerPinParams{
                        .top_left = Vec2{effect.quad[0], effect.quad[1]},
                        .top_right = Vec2{effect.quad[2], effect.quad[3]},
                        .bottom_right = Vec2{effect.quad[4], effect.quad[5]},
                        .bottom_left = Vec2{effect.quad[6], effect.quad[7]}});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Twirl) {
                    prepared_effects[index].emplace_back(TwirlParams{
                        .center = Vec2{effect.warp_center[0], effect.warp_center[1]},
                        .angle_degrees = effect.warp_angle,
                        .radius = effect.warp_radius,
                        .falloff = effect.warp_falloff});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Bulge) {
                    prepared_effects[index].emplace_back(BulgeParams{
                        .center = Vec2{effect.warp_center[0], effect.warp_center[1]},
                        .amount = effect.warp_amount,
                        .radius = effect.warp_radius});
                } else if (effect.kind == LayerPlan::EffectKindPlan::MeshWarp) {
                    MeshWarpParams mesh;
                    mesh.cols = static_cast<int>(effect.mesh_cols);
                    mesh.rows = static_cast<int>(effect.mesh_rows);
                    mesh.smoothness = effect.mesh_smoothness;
                    mesh.offsets.reserve(effect.mesh_offsets.size() / 2);
                    for (std::size_t i = 0; i + 1 < effect.mesh_offsets.size(); i += 2)
                        mesh.offsets.push_back(Vec2{effect.mesh_offsets[i], effect.mesh_offsets[i + 1]});
                    prepared_effects[index].emplace_back(std::move(mesh));

                } else {
                    prepared_effects[index].emplace_back(MosaicParams{
                        .block_size = effect.block_size});
                }
            }
        }
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());

        CompositionDefinition definition;
        definition.composition = spec;
        const bool has_3d_layer = std::any_of(
            plan.layers.begin(), plan.layers.end(),
            [](const LayerPlan& layer) { return layer.enable_3d; });
        const auto plan_camera = detail::make_camera_descriptor(plan);
        definition.scene = [plan, resolved_video_paths, resolved_particle_sprite_paths, prepared_texts, prepared_text_bounds, prepared_effects, font_engine,
                            has_3d_layer, plan_camera](
                               const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.font_engine(font_engine.get());

            // Render-plan text positions are converted to centre-relative
            // layer coordinates by apply_layer_primitives(). Keep the
            // approved GlowBasics camera at that canonical world origin; a
            // second canvas-centre translation here would move the text out
            // of frame.
            if (has_3d_layer && !plan.camera) {
                Camera2_5D camera;
                camera.enabled = true;
                camera.optics_mode = CameraOpticsMode::Zoom;
                camera.position = Vec3{0.0f, 0.0f, -1000.0f};
                camera.zoom = 1000.0f;
                camera.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
                camera.point_of_interest_enabled = true;
                // The implicit camera has a fixed pose. Layer motion is
                // tracked on the layers themselves and must not keep the
                // camera in the per-frame animated-camera path.
                camera.is_animated = false;
                scene.camera().set(camera);
            }
            // The canonical CLI executor builds this scene callback directly
            // and does not pass through Composition::evaluate(), which is the
            // path that normally applies a composition's default camera. Apply
            // render-plan cameras here so camera tracks affect the rendered
            // frame in both execution paths.
            if (plan_camera) {
                chronon3d::camera_v1::PoseTracksSource pose;
                pose.position = chronon3d::AnimatedValue<chronon3d::Vec3>{
                    plan_camera->base.position};
                pose.rotation = chronon3d::AnimatedValue<chronon3d::Vec3>{
                    plan_camera->base.rotation};
                pose.fov_deg = chronon3d::AnimatedValue<float>{
                    plan.camera->fov_deg};
                pose.zoom = chronon3d::AnimatedValue<float>{
                    1000.0f * plan.camera->zoom};
                if (const auto* authored = std::get_if<
                        chronon3d::camera_v1::PoseTracksSource>(
                            &plan_camera->source)) {
                    pose = *authored;
                }
                const auto camera_context = chronon3d::camera_v1::CameraEvalContext::at(
                    ctx.frame(), ctx.frame_rate(), ctx.width, ctx.height);
                scene.camera().set(chronon3d::camera_v1::eval_pose_tracks(
                    plan_camera->base, pose, camera_context));
            }

            for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                const auto& layer = plan.layers[index];
                const auto author_layer = [&](LayerBuilder& builder) {
                    builder.font_engine(font_engine.get());
                    // Image textures carry their own color.  2.5D projection
                    // changes their pose, not their albedo, so keep them out
                    // of the scene lighting pass (which otherwise grays light
                    // product imagery and its transparent edge pixels).
                    if ((layer.type == LayerType::Image || layer.type == LayerType::Shape) && layer.enable_3d)
                        builder.accepts_lights(false).accepts_shadows(false);
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
                        case LayerType::Particles: {
                            if (!layer.particle_emitter)
                                throw std::runtime_error(
                                    "particles layer '" + layer.id + "' has no emitter parameters");
                            const double fps = static_cast<double>(plan.canvas.fps.numerator) /
                                static_cast<double>(plan.canvas.fps.denominator);
                            if (!std::isfinite(fps) || fps <= 0.0)
                                throw std::runtime_error("particles require a positive canvas frame rate");
                            const double seconds = static_cast<double>(ctx.effective_frame()) / fps;
                            const auto sampled = sample_particles(*layer.particle_emitter, seconds);
                            if (!sampled.ok())
                                throw std::runtime_error("particle sampling failed: " + sampled.error);
                            builder.accepts_lights(false).accepts_shadows(false);
                            for (const auto& particle : sampled.particles) {
                                if (!resolved_particle_sprite_paths[index].empty()) {
                                    ImageParams sprite;
                                    sprite.asset_path=resolved_particle_sprite_paths[index];
                                    sprite.size=Vec2{particle.size*particle.sprite_scale,particle.size*particle.sprite_scale};
                                    sprite.pos=Vec3{particle.position.x,particle.position.y,0.0f};
                                    sprite.opacity=particle.color.a;
                                    sprite.tint=Color{particle.color.r,particle.color.g,particle.color.b,1.0f};
                                    sprite.rotation_radians=particle.rotation;
                                    builder.image("particle_"+std::to_string(particle.index),std::move(sprite));
                                    continue;
                                }
                                CircleParams params;
                                params.radius = particle.size * 0.5f;
                                params.color = particle.color;
                                params.fill = graphics::FillStyle::solid(particle.color);
                                params.pos = Vec3{particle.position.x, particle.position.y, 0.0f};
                                builder.circle("particle_" + std::to_string(particle.index),
                                               std::move(params));
                            }
                            break;
                        }
                        case LayerType::Light: {
                            if (!layer.light)
                                throw std::runtime_error("light layer '" + layer.id + "' has no light payload");
                            const auto& light = *layer.light;
                            const Color color{
                                light.color[0] * light.intensity,
                                light.color[1] * light.intensity,
                                light.color[2] * light.intensity,
                                light.color[3]};
                            CircleParams params;
                            params.radius = light.radius;
                            params.color = color;
                            params.fill = graphics::FillStyle::radial(
                                {0.5f, 0.5f}, 0.5f,
                                {{0.0f, color}, {1.0f, Color{
                                    color.r, color.g, color.b, 0.0f}}});
                            builder.accepts_lights(false).accepts_shadows(false);
                            builder.circle("radial_light", std::move(params));
                            break;
                        }
                        case LayerType::Adjustment:
                            // Adjustment effects consume the composite built
                            // from preceding layers and intentionally draw no
                            // source geometry of their own.
                            break;
                        case LayerType::Shape: {
                            if (!layer.shape)
                                throw std::runtime_error("shape layer '" + layer.id + "' has no shape payload");
                            const auto& shape = *layer.shape;
                            const Vec2 size = layer.size_dimensions == 2
                                ? Vec2{layer.size[0], layer.size[1]}
                                : Vec2{static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                            const Color base_color{layer.color[0], layer.color[1],
                                                   layer.color[2], layer.color[3]};
                            std::optional<graphics::FillStyle> fill;
                            if (shape.gradient) {
                                fill = graphics::FillStyle::solid(base_color);
                                fill->gradient = shape.gradient;
                            } else if (shape.fill_color) {
                                fill = graphics::FillStyle::solid(Color{
                                    (*shape.fill_color)[0], (*shape.fill_color)[1],
                                    (*shape.fill_color)[2], (*shape.fill_color)[3]});
                            }
                            graphics::StrokeStyle stroke;
                            if (shape.stroke) {
                                stroke.enabled = true;
                                stroke.width = shape.stroke->width.value_or(1.0f);
                                if (shape.stroke->solid_color) {
                                    const auto& rgba = *shape.stroke->solid_color;
                                    stroke.color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
                                }
                                stroke.gradient = shape.stroke->gradient;
                                stroke.trim_start = shape.stroke->trim_start.value_or(0.0f);
                                stroke.trim_end = shape.stroke->trim_end.value_or(1.0f);
                                stroke.dash_array = shape.stroke->dash_array;
                                stroke.dash_offset = shape.stroke->dash_offset;
                                if (const auto animated_offset=detail::stroke_dash_offset_at(layer,ctx.effective_frame()))
                                    stroke.dash_offset=*animated_offset;
                            }
                            if (layer.position_dimensions < 2 &&
                                shape.type != ShapeContentTypePlan::Line &&
                                shape.type != ShapeContentTypePlan::Path) {
                                builder.pin_to(Anchor::Center);
                            }
                            switch (shape.type) {
                                case ShapeContentTypePlan::Rect: {
                                    RectParams params;
                                    params.size = size;
                                    params.color = base_color;
                                    params.fill = fill;
                                    params.stroke = stroke;
                                    builder.rect("shape", std::move(params));
                                    break;
                                }
                                case ShapeContentTypePlan::RoundedRect: {
                                    RoundedRectParams params;
                                    params.size = size;
                                    params.radius = shape.radius;
                                    params.corner_radius = shape.corner_radius;
                                    params.color = base_color;
                                    params.fill = fill;
                                    params.stroke = stroke;
                                    builder.rounded_rect("shape", std::move(params));
                                    break;
                                }
                                case ShapeContentTypePlan::Ellipse: {
                                    CircleParams params;
                                    params.radius = std::min(size.x, size.y) * 0.5f;
                                    params.color = base_color;
                                    params.fill = fill;
                                    params.stroke = stroke;
                                    builder.circle("shape", std::move(params));
                                    break;
                                }
                                case ShapeContentTypePlan::Line:
                                case ShapeContentTypePlan::Path: {
                                    PathParams params;
                                    for (const auto& command : shape.path) {
                                        const Vec2 point{command.point[0], command.point[1]};
                                        const Vec2 control1{command.control1[0], command.control1[1]};
                                        const Vec2 control2{command.control2[0], command.control2[1]};
                                        switch (command.type) {
                                            case ShapePathCommandTypePlan::MoveTo:
                                                params.commands.push_back(PathCommand::move_to(point)); break;
                                            case ShapePathCommandTypePlan::LineTo:
                                                params.commands.push_back(PathCommand::line_to(point)); break;
                                            case ShapePathCommandTypePlan::QuadraticTo:
                                                params.commands.push_back(PathCommand::quadratic_to(control1, point)); break;
                                            case ShapePathCommandTypePlan::CubicTo:
                                                params.commands.push_back(PathCommand::cubic_to(control1, control2, point)); break;
                                            case ShapePathCommandTypePlan::Close:
                                                params.commands.push_back(PathCommand::close());
                                                params.closed = true;
                                                break;
                                        }
                                    }
                                    if (shape.gradient) {
                                        params.fill.enabled = true;
                                        params.fill.type = fill_type_for_gradient(shape.gradient->type);
                                        params.fill.gradient = *shape.gradient;
                                    } else if (shape.fill_color) {
                                        params.fill = Fill::solid_color(Color{
                                            (*shape.fill_color)[0], (*shape.fill_color)[1],
                                            (*shape.fill_color)[2], (*shape.fill_color)[3]});
                                    }
                                    params.stroke = stroke.to_path_stroke();
                                    params.pos = {0.0f, 0.0f, 0.0f};
                                    if (shape.operators.empty()) {
                                builder.path("shape", std::move(params));
                            } else {
                                PathShape source_path;
                                source_path.commands = params.commands;
                                source_path.stroke = params.stroke;
                                source_path.fill = params.fill;
                                source_path.closed = params.closed;
                                std::vector<PathOperatorSpec> specs;
                                for (const auto& op : shape.operators) {
                                    if (op.kind == PathOperatorKindPlan::RoundCorners) {
                                        specs.push_back({PathOperatorKind::RoundCorners,
                                            RoundCornersParams{op.radius}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::ZigZag) {
                                        specs.push_back({PathOperatorKind::ZigZag,
                                            ZigZagParams{op.size, op.points_per_unit, op.smooth}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::OffsetPath) {
                                        specs.push_back({PathOperatorKind::OffsetPath,
                                            OffsetPathParams{op.amount, op.miter_limit}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::PuckerBloat) {
                                        specs.push_back({PathOperatorKind::PuckerBloat,
                                            PuckerBloatParams{op.amount, {op.anchor[0], op.anchor[1]}}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::Twist) {
                                        specs.push_back({PathOperatorKind::Twist,
                                            TwistParams{op.degrees, {op.center[0], op.center[1]}}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::WigglePath) {
                                        specs.push_back({PathOperatorKind::WigglePath,
                                            WigglePathParams{op.amount, op.frequency}, op.seed});
                                    } else if (op.kind == PathOperatorKindPlan::Morph) {
                                        MorphPathParams morph;
                                        morph.progress = op.morph_progress;
                                        morph.target.reserve(op.morph_target.size());
                                        for (const auto& command : op.morph_target) {
                                            const Vec2 point{command.point[0], command.point[1]};
                                            const Vec2 c1{command.control1[0], command.control1[1]};
                                            const Vec2 c2{command.control2[0], command.control2[1]};
                                            switch (command.type) {
                                                case ShapePathCommandTypePlan::MoveTo: morph.target.push_back(PathCommand::move_to(point)); break;
                                                case ShapePathCommandTypePlan::LineTo: morph.target.push_back(PathCommand::line_to(point)); break;
                                                case ShapePathCommandTypePlan::QuadraticTo: morph.target.push_back(PathCommand::quadratic_to(c1, point)); break;
                                                case ShapePathCommandTypePlan::CubicTo: morph.target.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                                case ShapePathCommandTypePlan::Close: morph.target.push_back(PathCommand::close()); break;
                                            }
                                        }
                                        if (op.morph_animation) {
                                            chronon3d::AnimatedValue<float> progress{op.morph_progress};
                                            for (const auto& key : op.morph_animation->keyframes) {
                                                if (key.value.size() != 1)
                                                    throw std::runtime_error("morph animation keyframes require scalar progress");
                                                progress.add_keyframe(
                                                    key.frame, key.value.front(),
                                                    detail::animation_keyframe_easing(*op.morph_animation, key));
                                            }
                                            morph.progress = std::clamp(
                                                progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                                        }
                                        specs.push_back({PathOperatorKind::Morph, std::move(morph), 0});
                                    } else if (op.kind == PathOperatorKindPlan::Merge) {
                                        MergeParams merge;
                                        for (const auto& planned_path : op.paths) {
                                            std::vector<PathCommand> subpath;
                                            for (const auto& command : planned_path) {
                                                const Vec2 point{command.point[0], command.point[1]};
                                                const Vec2 c1{command.control1[0], command.control1[1]};
                                                const Vec2 c2{command.control2[0], command.control2[1]};
                                                switch (command.type) {
                                                    case ShapePathCommandTypePlan::MoveTo: subpath.push_back(PathCommand::move_to(point)); break;
                                                    case ShapePathCommandTypePlan::LineTo: subpath.push_back(PathCommand::line_to(point)); break;
                                                    case ShapePathCommandTypePlan::QuadraticTo: subpath.push_back(PathCommand::quadratic_to(c1, point)); break;
                                                    case ShapePathCommandTypePlan::CubicTo: subpath.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                                    case ShapePathCommandTypePlan::Close: subpath.push_back(PathCommand::close()); break;
                                                }
                                            }
                                            merge.paths.push_back(std::move(subpath));
                                        }
                                        specs.push_back({PathOperatorKind::Merge, std::move(merge), 0});
                                    } else if (op.kind == PathOperatorKindPlan::Trim) {
                                        specs.push_back({PathOperatorKind::Trim,
                                            TrimPathParams{op.trim_start, op.trim_end, op.trim_offset}, 0});
                                    } else {
                                        const auto& repeater = op.repeater;
                                        RepeaterParams rp;
                                        rp.copies = repeater.copies;
                                        rp.position = {repeater.position[0], repeater.position[1]};
                                        rp.scale = {repeater.scale[0], repeater.scale[1]};
                                        rp.rotation_degrees = repeater.rotation_degrees;
                                        rp.anchor = {repeater.anchor[0], repeater.anchor[1]};
                                        rp.opacity_decay = repeater.opacity_decay;
                                        specs.push_back({PathOperatorKind::Repeater, rp, 0});
                                    }
                                }
                                auto expanded = apply_path_operators(source_path, specs);
                                for (std::size_t i = 0; i < expanded.size(); ++i) {
                                    PathParams copy = params;
                                    copy.commands = std::move(expanded[i].commands);
                                    copy.stroke = std::move(expanded[i].stroke);
                                    copy.fill = std::move(expanded[i].fill);
                                    copy.color.a *= expanded[i].opacity;
                                    builder.path("shape_repeater_" + std::to_string(i), std::move(copy));
                                }
                            }
                                    break;
                                }
                            }
                            if (layer.style && layer.style->backdrop) {
                                const auto& backdrop = *layer.style->backdrop;
                                builder.backdrop_blur(BackdropBlurParams{
                                    .blur = backdrop.blur,
                                    .brightness = backdrop.brightness,
                                    .saturation = backdrop.saturation});
                            }
                            break;
                        }
                        case LayerType::Text: {
                            builder.kind(LayerKind::Text);
                            // Text paint is authored in screen space; it is
                            // not a lit 3D material. Keeping this semantic
                            // contract at the layer boundary prevents the
                            // native GPU path from inserting CPU-only light/
                            // depth-grade passes between projected text and
                            // its compositor output.
                            builder.accepts_lights(false).accepts_shadows(false);
                            if (!prepared_texts[index])
                                throw std::runtime_error(
                                    "text layer was not prepared");
                            if (layer.style && layer.style->background) {
                                if (!prepared_text_bounds[index])
                                    throw std::runtime_error(
                                        "text card bounds were not measured for layer '" + layer.id + "'");
                                const auto& background = *layer.style->background;
                                const auto color = parse_hex_color(
                                    background.color,
                                    background.opacity.value_or(1.0f));
                                if (!color)
                                    throw std::runtime_error(
                                        "text layer '" + layer.id + "' has invalid background color");
                                const Vec2 padding = background.padding_dimensions == 2
                                    ? Vec2{background.padding[0], background.padding[1]}
                                    : Vec2{0.0f, 0.0f};
                                const Vec2 card_size{
                                    std::max(1.0f, prepared_text_bounds[index]->x + 2.0f * padding.x),
                                    std::max(1.0f, prepared_text_bounds[index]->y + 2.0f * padding.y)};
                                RoundedRectParams card;
                                card.size = card_size;
                                card.radius = background.radius.value_or(0.0f);
                                card.pos = {-card_size.x * 0.5f, -card_size.y * 0.5f, 0.0f};
                                card.color = *color;
                                card.fill = graphics::FillStyle::solid(*color);
                                builder.rounded_rect("text_card", std::move(card));
                            }
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
                        builder, layer, plan.canvas.width, plan.canvas.height, plan.canvas.fps);
                    if (layer.type == LayerType::Particles && layer.particle_emitter)
                        builder.blend(layer.particle_emitter->blend_mode);
                    if (layer.safe_insets) {
                                const auto& insets = *layer.safe_insets;
                                Vec2 content_size{};
                                if (layer.type == LayerType::Text &&
                                    prepared_text_bounds[index]) {
                                    content_size = *prepared_text_bounds[index];
                                    if (layer.style && layer.style->background) {
                                        const auto& padding_style = *layer.style->background;
                                        if (padding_style.padding_dimensions == 2) {
                                            content_size.x += 2.0f * padding_style.padding[0];
                                            content_size.y += 2.0f * padding_style.padding[1];
                                        }
                                    }
                                } else if (layer.type == LayerType::Shape &&
                                           layer.size_dimensions == 2) {
                                    content_size = {layer.size[0], layer.size[1]};
                                } else if ((layer.type == LayerType::Image ||
                                            layer.type == LayerType::Video) &&
                                           layer.size_dimensions == 2) {
                                    content_size = {layer.size[0], layer.size[1]};
                                } else if (layer.type == LayerType::Particles &&
                                           layer.particle_emitter) {
                                    content_size = layer.particle_emitter->emitter_size;
                                } else if (layer.type == LayerType::Light && layer.light) {
                                    content_size = Vec2{layer.light->radius * 2.0f};
                                }
                                content_size.x *= std::abs(layer.scale[0]);
                                content_size.y *= std::abs(layer.scale[1]);
                                const float angle = glm::radians(layer.rotation[2]);
                                const Vec2 rotated_size{
                                    std::abs(std::cos(angle)) * content_size.x +
                                        std::abs(std::sin(angle)) * content_size.y,
                                    std::abs(std::sin(angle)) * content_size.x +
                                        std::abs(std::cos(angle)) * content_size.y};
                                const float half_canvas_width =
                                    static_cast<float>(plan.canvas.width) * 0.5f;
                                const float half_canvas_height =
                                    static_cast<float>(plan.canvas.height) * 0.5f;
                                builder.keep_in_safe_area(chronon3d::SafeArea{
                                    .top = insets[0] - half_canvas_height,
                                    .bottom = half_canvas_height + insets[2] + rotated_size.y,
                                    .left = insets[3] - half_canvas_width,
                                    .right = half_canvas_width + insets[1] + rotated_size.x});
                    }
                    if (layer.type == LayerType::Shape && layer.style) {
                        const auto add_shadow = [&](const ShadowStyle& shadow) {
                            const float opacity = shadow.opacity.value_or(1.0f);
                            const auto color = parse_hex_color(shadow.color, opacity);
                            if (!color)
                                throw std::runtime_error("shape layer '" + layer.id + "' has invalid shadow color");
                            if (shadow.offset_dimensions != 2)
                                throw std::runtime_error("shape layer '" + layer.id + "' shadow requires a two-dimensional offset");
                            builder.effect(DropShadowParams{
                                .offset = Vec2{shadow.offset[0], shadow.offset[1]},
                                .color = *color,
                                .radius = shadow.blur.value_or(0.0f)});
                        };
                        if (layer.style->shadow) add_shadow(*layer.style->shadow);
                        for (const auto& shadow : layer.style->shadows) add_shadow(shadow);
                    }
                    for (std::size_t effect_index=0; effect_index<prepared_effects[index].size(); ++effect_index) {
                        const auto& effect=prepared_effects[index][effect_index];
                        const auto* effect_plan=effect_index<layer.effects.size()?&layer.effects[effect_index]:nullptr;
                        const auto time_control=effect_plan?effect_plan->posterize_fps:std::nullopt;
                        std::optional<TemporalCurve1D> time_curve;
                        if(effect_plan&&effect_plan->time_curve)
                            time_curve=detail::make_effect_time_curve(*effect_plan->time_curve);
                        const double composition_fps=static_cast<double>(plan.canvas.fps.numerator)/plan.canvas.fps.denominator;
                        const double duration_seconds=static_cast<double>(plan.canvas.duration.value)/composition_fps;
                        std::visit([&](const auto& params) {
                            using T = std::decay_t<decltype(params)>;
                            if constexpr (!std::is_same_v<T, std::monostate>)
                                builder.effect(params,time_control,time_curve,duration_seconds,composition_fps);
                        }, effect);
                    }
                    if (const auto motion_blur = detail::motion_blur_params_at(
                            layer, ctx.effective_frame())) {
                        builder.effect(*motion_blur);
                    }
                    if (layer.path_mask) {
                        PathMaskParams params;
                        params.feather = layer.path_mask->feather;
                        params.expansion = layer.path_mask->expansion;
                        params.inverted = layer.path_mask->inverted;
                        for (const auto& command : layer.path_mask->path) {
                            const Vec2 point{command.point[0], command.point[1]};
                            const Vec2 c1{command.control1[0], command.control1[1]};
                            const Vec2 c2{command.control2[0], command.control2[1]};
                            switch (command.type) {
                                case ShapePathCommandTypePlan::MoveTo: params.commands.push_back(PathCommand::move_to(point)); break;
                                case ShapePathCommandTypePlan::LineTo: params.commands.push_back(PathCommand::line_to(point)); break;
                                case ShapePathCommandTypePlan::QuadraticTo: params.commands.push_back(PathCommand::quadratic_to(c1, point)); break;
                                case ShapePathCommandTypePlan::CubicTo: params.commands.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                case ShapePathCommandTypePlan::Close: params.commands.push_back(PathCommand::close()); break;
                            }
                        }
                        builder.mask_path(std::move(params));
                    }
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
                };
                const bool full_canvas_backdrop = has_3d_layer &&
                    layer.type != LayerType::Text &&
                    layer.type != LayerType::Shape &&
                    layer.position_dimensions == 0 &&
                    (layer.size_dimensions != 2 ||
                     (layer.size[0] >= static_cast<float>(plan.canvas.width) &&
                      layer.size[1] >= static_cast<float>(plan.canvas.height)));
                // Full-canvas color, image and video plates are backdrops, not
                // camera-facing cards. Keep them in screen space around any
                // projected 3D content, independent of the concrete media type.
                if (layer.type == LayerType::Adjustment)
                    scene.adjustment_layer(layer.id, author_layer);
                else if (layer.screen_space || full_canvas_backdrop)
                    scene.screen_layer(layer.id, author_layer);
                else scene.layer(layer.id, author_layer);
            }
            return scene.build();
        };
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
