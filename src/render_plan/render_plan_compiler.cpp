#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <cmath>
#include <string>
#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include "render_plan_compiler_detail.hpp"

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include "../scene/camera/camera_v1/camera_program_sources.hpp"
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <filesystem>
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
        }
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());

        CompositionDefinition definition;
        definition.composition = spec;
        const bool has_3d_layer = std::any_of(
            plan.layers.begin(), plan.layers.end(),
            [](const LayerPlan& layer) { return layer.enable_3d; });
        const auto plan_camera = detail::make_camera_descriptor(plan);
        definition.scene = [plan, resolved_video_paths, prepared_texts, font_engine,
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
                camera.is_animated = true;
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
                    if (layer.type == LayerType::Image && layer.enable_3d)
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
                };
                if (layer.screen_space) scene.screen_layer(layer.id, author_layer);
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
