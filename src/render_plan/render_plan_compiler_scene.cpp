#include "render_plan_compiler_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include "light_attenuation.hpp"
#include <chronon3d/effects/effect_param_animation.hpp>
#include "../scene/camera/camera_v1/camera_program_sources.hpp"
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace chronon3d::render_plan::detail {
std::function<chronon3d::Scene(const chronon3d::FrameContext&)>
make_scene_evaluator(SceneMaterializationInputs inputs) {
    return [plan = std::move(inputs.plan),
            resolved_video_paths = std::move(inputs.resolved_video_paths),
            resolved_particle_sprite_paths = std::move(inputs.resolved_particle_sprite_paths),
            prepared_texts = std::move(inputs.prepared_texts),
            prepared_text_counters = std::move(inputs.prepared_text_counters),
            prepared_text_bounds = std::move(inputs.prepared_text_bounds),
            prepared_effects = std::move(inputs.prepared_effects),
            font_engine = std::move(inputs.font_engine),
            has_3d_layer = inputs.has_3d_layer,
            plan_camera = std::move(inputs.plan_camera),
            follow_path_evaluators = std::move(inputs.follow_path_evaluators),
            resolved_ocio_config_path = std::move(inputs.resolved_ocio_config_path)](
               const chronon3d::FrameContext& ctx) {

        SceneBuilder scene(ctx);
        scene.font_engine(font_engine.get());
        configure_scene_camera_and_lighting(
            scene, plan, has_3d_layer, plan_camera, ctx);

        // Point/spot/directional sources are scene-level, not layer geometry.
        std::vector<rendering::PointLightSource> point_sources;
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            const auto author_layer = [&](LayerBuilder& builder) {
                builder.font_engine(font_engine.get());
                if (layer.type != LayerType::Light && layer.type != LayerType::Adjustment) {
                    if (layer.casts_shadows) builder.casts_shadows(*layer.casts_shadows);
                    if (layer.accepts_shadows) builder.accepts_shadows(*layer.accepts_shadows);
                }
                if (layer.flow_layout) {
                    const auto& p=*layer.flow_layout; LayoutFlow flow;
                    flow.group_id=p.group_id; flow.main_axis=p.axis=="column"?Axis::Column:Axis::Row;
                    flow.gap=p.gap; flow.wrap=p.wrap; flow.cell_size={p.cell_size[0],p.cell_size[1]};
                    flow.align=p.align=="center"?AlignItems::Center:p.align=="end"?AlignItems::End:p.align=="stretch"?AlignItems::Stretch:AlignItems::Start;
                    flow.justify=p.justify=="center"?JustifyContent::Center:p.justify=="end"?JustifyContent::End:p.justify=="space_between"?JustifyContent::SpaceBetween:p.justify=="space_around"?JustifyContent::SpaceAround:p.justify=="space_evenly"?JustifyContent::SpaceEvenly:JustifyContent::Start;
                    builder.flow_layout(std::move(flow));
                }
                if (layer.grid_layout) {
                    const auto& p=*layer.grid_layout; LayoutGrid grid;
                    grid.group_id=p.group_id; grid.columns=p.columns; grid.rows=p.rows;
                    grid.fit=p.fit=="fixed_rows"?GridFit::FixedRows:p.fit=="fixed_columns"?GridFit::FixedColumns:GridFit::Auto;
                    grid.gap={p.gap[0],p.gap[1]}; grid.cell_size={p.cell_size[0],p.cell_size[1]};
                    builder.grid_layout(std::move(grid));
                }
                // Image textures carry their own color.  2.5D projection
                // changes their pose, not their albedo, so keep them out
                // of the scene lighting pass (which otherwise grays light
                // product imagery and its transparent edge pixels).
                if ((layer.type == LayerType::Image || layer.type == LayerType::Shape ||
                     layer.type == LayerType::Mesh) && layer.enable_3d)
                    builder.accepts_lights(false).accepts_shadows(false);
                switch (layer.type) {
                    case LayerType::Image: {
                        ImageParams params;
                        params.asset_path = layer.asset;
                        if (!layer.ocio_input_space.empty()) {
                            if (!plan.color_management)
                                throw std::runtime_error("image ocio_input_space requires plan color_management");
                            params.decode_options.ocio_config_path = resolved_ocio_config_path;
                            params.decode_options.ocio_input_space = layer.ocio_input_space;
                            params.decode_options.ocio_working_space =
                                plan.color_management->working_space;
                        }
                        params.decode_options.color_space =
                            layer.input_color_space == "linearsrgb"
                                ? chronon3d::ColorSpace::LinearSRGB
                                : (layer.input_color_space == "displayp3"
                                    ? chronon3d::ColorSpace::DisplayP3
                                    : (layer.input_color_space == "rec709"
                                        ? chronon3d::ColorSpace::Rec709
                                        : chronon3d::ColorSpace::SRGB));
                        params.size = layer.size_dimensions == 2
                            ? Vec2{layer.size[0], layer.size[1]}
                            : Vec2{static_cast<float>(plan.canvas.width),
                                   static_cast<float>(plan.canvas.height)};
                        params.fit = detail::fit_mode(
                            layer.fit.value_or(FitMode::Cover));
                        if (layer.crop) {
                            params.crop.enabled = layer.crop->enabled;
                            params.crop.origin = Vec2{layer.crop->origin[0], layer.crop->origin[1]};
                            params.crop.size = Vec2{layer.crop->size[0], layer.crop->size[1]};
                            params.crop.flip_x = layer.crop->flip_x;
                        }
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
                        const std::string& video_input_space =
                            layer.ocio_input_space.empty() && plan.color_management
                                ? plan.color_management->video_input_space
                                : layer.ocio_input_space;
                        if (!video_input_space.empty()) {
                            if (resolved_ocio_config_path.empty() || !plan.color_management)
                                throw std::runtime_error("video OCIO input space requires plan color_management");
                            source.ocio_config_path = resolved_ocio_config_path;
                            source.ocio_input_space = video_input_space;
                            source.ocio_working_space = plan.color_management->working_space;
                        }
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
                        // Rounded corners on a decoded video source. The
                        // video node samples a decoded surface, so there is
                        // no crop to pre-round (the image path rounds its
                        // crop) and no coverage unit in the sampling
                        // kernel: the corner coverage is expressed as the
                        // layer's own rounded-rect mask instead, which is
                        // the same alpha ramp. Two consequences are
                        // deliberate and measured:
                        //   - mask_enabled resolves the layer off the
                        //     DirectYUV lane (structural check ->
                        //     Unsupported); Auto falls back to the
                        //     faithful graph lane, Require hard-fails
                        //     instead of silently emitting square corners.
                        //   - the mask is rasterized in the source
                        //     framebuffer's pixel space (like the image
                        //     path's scaled_radius), so the displayed
                        //     radius is converted through the layer's
                        //     pre-scale box.
                        if (const auto authored_radius = layer.radius;
                            authored_radius && *authored_radius > 0.0f) {
                            if (!std::isfinite(*authored_radius))
                                throw std::runtime_error(
                                    "video layer '" + layer.id + "' has a non-finite radius");
                            const float scale_x = std::abs(layer.scale[0]) > 1e-6f
                                ? std::abs(layer.scale[0]) : 1.0f;
                            const float scale_y = std::abs(layer.scale[1]) > 1e-6f
                                ? std::abs(layer.scale[1]) : 1.0f;
                            const float box_w = layer.size_dimensions == 2
                                ? layer.size[0] : static_cast<float>(plan.canvas.width);
                            const float box_h = layer.size_dimensions == 2
                                ? layer.size[1] : static_cast<float>(plan.canvas.height);
                            const float scaled_radius = std::min(
                                *authored_radius / std::min(scale_x, scale_y),
                                std::min(box_w, box_h) * 0.5f);
                            if (scaled_radius > 0.0f) {
                                Mask rounded;
                                rounded.type = MaskType::RoundedRect;
                                rounded.size = Vec2{box_w, box_h};
                                rounded.radius = scaled_radius;
                                // Keep the rounded video edge antialiased in
                                // output-pixel space. Mask feather is measured
                                // before the layer transform, so divide the
                                // one-pixel coverage ramp by the smaller scale.
                                rounded.feather = 1.0f / std::min(scale_x, scale_y);
                                rounded.pos = Vec3{0.0f, 0.0f, 0.0f};
                                builder.mask_composite(std::move(rounded));
                            }
                        }
                        builder.video(std::move(source));
                        break;
                    }
                    case LayerType::Color: {
                        const Vec2 solid_size = layer.size_dimensions == 2
                            ? Vec2{layer.size[0], layer.size[1]}
                            : Vec2{static_cast<float>(plan.canvas.width),
                                   static_cast<float>(plan.canvas.height)};
                        Color solid_color{layer.color[0], layer.color[1],
                                          layer.color[2], layer.color[3]};
                        if (const auto animated = detail::fill_color_at(
                                layer, ctx.effective_frame(), solid_color, plan.canvas.fps))
                            solid_color = *animated;
                        const float authored_radius = layer.radius.value_or(0.0f);
                        if (!std::isfinite(authored_radius) || authored_radius < 0.0f)
                            throw std::runtime_error(
                                "color layer '" + layer.id + "' has a non-finite radius");
                        // A flat colour plate may be rounded: `radius` is the
                        // top-level layer corner contract (image and shape
                        // layers already honour it), so a colour plate used
                        // as a card/frame is not forced to square corners.
                        if (authored_radius > 0.0f) {
                            RoundedRectParams params;
                            params.size = solid_size;
                            params.radius = std::min(authored_radius,
                                std::min(solid_size.x, solid_size.y) * 0.5f);
                            params.color = solid_color;
                            params.fill = graphics::FillStyle::solid(solid_color);
                            if (layer.position_dimensions < 2) {
                                builder.pin_to(Anchor::Center);
                                params.pos = {-params.size.x * 0.5f, -params.size.y * 0.5f, 0.0f};
                            }
                            builder.rounded_rect("color", std::move(params));
                            break;
                        }
                        RectParams params;
                        params.size = solid_size;
                        params.color = solid_color;
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
                        // Opaque, normal-blend particles can share path nodes
                        // while retaining their authored color order. Runs of
                        // equal paint become one path with multiple contours;
                        // translucent, additive and sprite particles retain
                        // individual nodes because batching would change their
                        // compositing or sampling semantics.
                        const bool opaque_cpu_batch =
                            resolved_particle_sprite_paths[index].empty() &&
                            layer.particle_emitter->blend_mode == BlendMode::Normal &&
                            std::all_of(sampled.particles.begin(), sampled.particles.end(),
                                [](const auto& particle) { return particle.color.a >= 1.0f; });
                        if (opaque_cpu_batch && !sampled.particles.empty()) {
                            std::optional<PathParams> batch;
                            Color batch_color{};
                            const auto flush_batch = [&] {
                                if (batch && !batch->commands.empty())
                                    builder.path("particle_batch", std::move(*batch));
                                batch.reset();
                            };
                            constexpr float tau = 6.2831853071795864769f;
                            for (const auto& particle : sampled.particles) {
                                const auto& color = particle.color;
                                if (!batch || color.r != batch_color.r || color.g != batch_color.g ||
                                    color.b != batch_color.b || color.a != batch_color.a) {
                                    flush_batch();
                                    batch.emplace();
                                    batch->fill = Fill::solid_color(color);
                                    batch->fill_rule = PathFillRule::NonZero;
                                    batch->stroke.enabled = false;
                                    batch_color = color;
                                }
                                const float radius = particle.size * 0.5f;
                                if (radius <= 0.0f) continue;
                                const std::size_t segments = radius <= 4.0f ? 16U :
                                    (radius <= 20.0f ? 32U : 64U);
                                const Vec2 center{particle.position.x, particle.position.y};
                                for (std::size_t segment = 0; segment < segments; ++segment) {
                                    const float angle = tau * static_cast<float>(segment) /
                                        static_cast<float>(segments);
                                    const Vec2 point = center + Vec2{
                                        std::cos(angle) * radius, std::sin(angle) * radius};
                                    if (segment == 0) batch->commands.push_back(PathCommand::move_to(point));
                                    else batch->commands.push_back(PathCommand::line_to(point));
                                }
                                batch->commands.push_back(PathCommand::close());
                            }
                            flush_batch();
                            break;
                        }
                        for (const auto& particle : sampled.particles) {
                            if (!resolved_particle_sprite_paths[index].empty()) {
                                ImageParams sprite;
                                sprite.asset_path=resolved_particle_sprite_paths[index];
                                sprite.size=Vec2{particle.size*particle.sprite_scale,particle.size*particle.sprite_scale};
                                sprite.pos=Vec3{particle.position.x,particle.position.y,
                                    layer.particle_emitter->environment ? (1.0f - particle.depth) * 100.0f : 0.0f};
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
                            if (layer.particle_emitter->bokeh) {
                                const Color transparent{particle.color.r, particle.color.g,
                                                         particle.color.b, 0.0f};
                                params.fill = graphics::FillStyle::radial(
                                    {0.5f, 0.5f}, 0.5f,
                                    {{0.0f, particle.color}, {1.0f, transparent}});
                            }
                            params.pos = Vec3{particle.position.x, particle.position.y,
                                layer.particle_emitter->environment ? (1.0f - particle.depth) * 100.0f : 0.0f};
                            builder.circle("particle_" + std::to_string(particle.index),
                                           std::move(params));
                        }
                        break;
                    }
                    case LayerType::Light: {
                        if (!layer.light)
                            throw std::runtime_error("light layer '" + layer.id + "' has no light payload");
                        const auto& light = *layer.light;
                        float intensity = light.intensity;
                        Color light_color{light.color[0], light.color[1], light.color[2], light.color[3]};
                        if (layer.animation) {
                            if (const auto track = std::find_if(layer.animation->tracks.begin(),
                                    layer.animation->tracks.end(), [](const auto& value) {
                                        return value.property == "light_intensity";
                                    }); track != layer.animation->tracks.end()) {
                                intensity = detail::evaluate_scalar_track(*track,
                                    static_cast<double>(ctx.effective_frame()), light.intensity,
                                    plan.canvas.fps);
                            }
                            if (const auto animated = detail::shadow_color_at(layer, "light_color",
                                    static_cast<double>(ctx.effective_frame()), light_color,
                                    plan.canvas.fps))
                                light_color = *animated;
                        }
                        if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 16.0f)
                            throw std::runtime_error("animated light_intensity must be in [0, 16]");
                        const Color color{
                            light_color.r * intensity,
                            light_color.g * intensity,
                            light_color.b * intensity,
                            light_color.a};
                        if (light.kind != LightKindPlan::Radial) {
                            // Point, spot and directional lights are sources, not
                            // geometry: they shade the lit layers per pixel and draw nothing.
                            rendering::PointLightSource source;
                            source.kind = light.kind == LightKindPlan::Point ? rendering::PointLightKind::Point
                                : light.kind == LightKindPlan::Spot ? rendering::PointLightKind::Spot
                                : rendering::PointLightKind::Directional;
                            const float default_x = static_cast<float>(plan.canvas.width) * 0.5f;
                            const float default_y = static_cast<float>(plan.canvas.height) * 0.5f;
                            source.position = detail::light_position_at(layer, ctx.effective_frame(), Vec3{
                                layer.position_dimensions >= 2 ? layer.position[0] : default_x,
                                layer.position_dimensions >= 2 ? layer.position[1] : default_y,
                                // A 2D light has no height: default to its radius so it
                                // lights the plane below it instead of grazing it at z=0.
                                layer.position_dimensions >= 3 ? layer.position[2] : light.radius}, plan.canvas.fps);
                            source.direction = detail::light_direction_at(
                                layer, ctx.effective_frame(), plan.canvas.fps);
                            source.color = color;
                            source.radius = light.radius;
                            source.range = light.range;
                            source.decay = light.decay;
                            source.angle = light.angle;
                            source.penumbra = light.penumbra;
                            source.casts_shadow = light.cast_shadow;
                            source.shadow_radius = light.shadow_radius;
                            source.shadow_bias = light.shadow_bias;
                            point_sources.push_back(source);
                            builder.accepts_lights(false).accepts_shadows(false);
                            break;
                        }
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
                    case LayerType::Mesh: {
                        if (!layer.inline_mesh)
                            throw std::runtime_error("mesh layer '" + layer.id + "' has no inline geometry");
                        InlineMeshParams params;
                        params.name = layer.id;
                        params.vertices.reserve(layer.inline_mesh->vertices.size());
                        for (const auto& vertex : layer.inline_mesh->vertices)
                            params.vertices.push_back(Vertex{
                                Vec3{vertex.position[0], vertex.position[1], vertex.position[2]},
                                Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                                Vec2{vertex.uv[0], vertex.uv[1]}});
                        params.indices = layer.inline_mesh->indices;
                        builder.accepts_lights(false).accepts_shadows(false);
                        builder.inline_mesh(std::move(params));
                        if (layer.casts_shadows || layer.accepts_shadows) {
                            if (layer.casts_shadows)
                                builder.casts_shadows(*layer.casts_shadows);
                            if (layer.accepts_shadows)
                                builder.accepts_shadows(*layer.accepts_shadows);
                        }
                        auto& node = builder.last_node_handle().node();
                        node.color = Color{layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                        if (layer.inline_mesh->material) {
                            const auto& source = *layer.inline_mesh->material;
                            RenderNode::InlineMeshMaterial material;
                            material.diffuse = source.diffuse;
                            material.specular = source.specular;
                            material.shininess = source.shininess;
                            material.rim_intensity = source.rim_intensity;
                            if (source.base_color)
                                material.base_color = Color{(*source.base_color)[0], (*source.base_color)[1],
                                                            (*source.base_color)[2], (*source.base_color)[3]};
                            node.inline_mesh_material = material;
                            builder.accepts_lights(true);
                        }
                        break;
                    }
                    case LayerType::Shape: {
                        detail::append_shape_layer(builder, layer, plan, ctx);
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
                        if (prepared_text_counters[index])
                            text_builder.from_animated_document(prepared_text_counters[index]);
                        detail::apply_text_animators(text_builder, layer, plan.canvas.fps);
                        detail::apply_text_glow(builder, layer);
                        break;
                    }
                }
                detail::apply_layer_primitives(
                    builder, layer, plan.canvas.width, plan.canvas.height, plan.canvas.fps,
                    layer.follow_path ? &follow_path_evaluators[index] : nullptr);
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
                // style.shadow/shadows is a LAYER style, not a shape style:
                // a drop shadow is one generic effect-stack entry
                // (DropShadowParams) behind the builder.effect() API, and
                // shape, image and video layers composite through the same
                // stack. Gating it on LayerType::Shape is what made a card
                // frame (a shape) able to cast a shadow while the clip
                // inside it could not.
                //
                // Contract of the widened gate: an enabled effect is an
                // RGBA post effect, and any RGBA post effect resolves the
                // layer off the DirectYUV lane (rgba_post_effects ->
                // RequiresFullFrame in call; Auto falls back to the
                // faithful full-frame graph, Require hard-fails rather than
                // approximating). A video/image layer with a shadow
                // therefore renders on the graph lane, exactly like a
                // shadowed shape does today.
                if ((layer.type == LayerType::Shape || layer.type == LayerType::Video ||
                     layer.type == LayerType::Image || layer.type == LayerType::Color) &&
                    layer.style) {
                    std::size_t shadow_index = 0;
                    const auto add_shadow = [&](const ShadowStyle& shadow) {
                        const std::string prefix = "shadow_" + std::to_string(shadow_index++);
                        const auto color = parse_hex_color(shadow.color, 1.0f);
                        if (!color)
                            throw std::runtime_error("layer '" + layer.id + "' has invalid shadow color");
                        if (shadow.offset_dimensions != 2)
                            throw std::runtime_error("layer '" + layer.id + "' shadow requires a two-dimensional offset");
                        const float opacity = detail::shadow_scalar_at(
                            layer, prefix + "_opacity", ctx.effective_frame(),
                            shadow.opacity.value_or(1.0f), plan.canvas.fps)
                            .value_or(shadow.opacity.value_or(1.0f));
                        const float blur = detail::shadow_scalar_at(
                            layer, prefix + "_blur", ctx.effective_frame(),
                            shadow.blur.value_or(0.0f), plan.canvas.fps)
                            .value_or(shadow.blur.value_or(0.0f));
                        const float offset_x = detail::shadow_scalar_at(
                            layer, prefix + "_offset_x", ctx.effective_frame(),
                            shadow.offset[0], plan.canvas.fps).value_or(shadow.offset[0]);
                        const float offset_y = detail::shadow_scalar_at(
                            layer, prefix + "_offset_y", ctx.effective_frame(),
                            shadow.offset[1], plan.canvas.fps).value_or(shadow.offset[1]);
                        const auto animated_color = detail::shadow_color_at(
                            layer, prefix + "_color", ctx.effective_frame(),
                            *color, plan.canvas.fps);
                        Color final_color = animated_color.value_or(*color);
                        final_color.a *= opacity;
                        if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
                            !std::isfinite(blur) || blur < 0.0f ||
                            !std::isfinite(offset_x) || !std::isfinite(offset_y))
                            throw std::runtime_error("animated shadow parameters are outside supported bounds");
                        builder.effect(DropShadowParams{
                            .offset = Vec2{offset_x, offset_y},
                            .color = final_color,
                            .radius = blur});
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
                    std::string_view canonical_effect_id;
                    const auto effect_type = effects::detect_effect_type(effect);
                    for (const auto& entry : effects::builtin_effect_catalog())
                        if (entry.type == effect_type) { canonical_effect_id = entry.id; break; }
                    std::optional<EffectParams> animated_effect;
                    if (!canonical_effect_id.empty()) {
                        for (const auto& param_track : layer.effect_param_tracks) {
                            if (param_track.effect_id != canonical_effect_id) continue;
                            if (!animated_effect) animated_effect.emplace(effect);
                            const auto base = effects::get_animatable_effect_param(
                                *animated_effect, param_track.effect_id, param_track.param);
                            if (!base)
                                throw std::runtime_error("effect parameter track is incompatible with the compiled effect parameter type");
                            const float value = detail::evaluate_scalar_track(
                                param_track.track, ctx.effective_frame(), *base, plan.canvas.fps);
                            const auto* bounds = effects::find_animatable_effect_param(
                                param_track.effect_id, param_track.param);
                            if (!bounds || !std::isfinite(value))
                                throw std::runtime_error("effect parameter track produced a non-finite sample");
                            if (!effects::set_animatable_effect_param(
                                    *animated_effect, param_track.effect_id, param_track.param,
                                    std::clamp(value, bounds->minimum, bounds->maximum)))
                                throw std::runtime_error("failed to apply compiled effect parameter track");
                        }
                    }
                    const auto& dispatch_effect = animated_effect ? *animated_effect : effect;
                    std::optional<EffectParams> time_evolved_effect;
                    if (const auto* roughen = std::get_if<RoughenEdgesParams>(&dispatch_effect);
                        roughen && roughen->evolution_speed != 0.0f) {
                        time_evolved_effect.emplace(dispatch_effect);
                        auto& sampled = std::get<RoughenEdgesParams>(*time_evolved_effect);
                        sampled.evolution += sampled.evolution_speed *
                            static_cast<float>(ctx.effective_frame() /
                                (static_cast<double>(plan.canvas.fps.numerator) /
                                 plan.canvas.fps.denominator));
                        sampled.evolution_speed = 0.0f;
                    }
                    const auto& frame_effect = time_evolved_effect
                        ? *time_evolved_effect : dispatch_effect;
                    std::visit([&](const auto& params) {
                        using T = std::decay_t<decltype(params)>;
                        if constexpr (!std::is_same_v<T, std::monostate>)
                            builder.effect(params,time_control,time_curve,duration_seconds,composition_fps);
                    }, frame_effect);
                }
                if (const auto motion_blur = detail::motion_blur_params_at(
                        layer, ctx.effective_frame(), plan.canvas.fps,
                        layer.follow_path ? &follow_path_evaluators[index] : nullptr)) {
                    builder.effect(*motion_blur);
                }
                if (layer.path_mask || !layer.masks.empty()) {
                    const auto convert_mask_path = [](const auto& input) {
                        std::vector<PathCommand> output;
                        output.reserve(input.size());
                        for (const auto& command : input) {
                            const Vec2 point{command.point[0], command.point[1]};
                            const Vec2 c1{command.control1[0], command.control1[1]};
                            const Vec2 c2{command.control2[0], command.control2[1]};
                            switch (command.type) {
                                case ShapePathCommandTypePlan::MoveTo: output.push_back(PathCommand::move_to(point)); break;
                                case ShapePathCommandTypePlan::LineTo: output.push_back(PathCommand::line_to(point)); break;
                                case ShapePathCommandTypePlan::QuadraticTo: output.push_back(PathCommand::quadratic_to(c1, point)); break;
                                case ShapePathCommandTypePlan::CubicTo: output.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                case ShapePathCommandTypePlan::Close: output.push_back(PathCommand::close()); break;
                            }
                        }
                        return output;
                    };
                    const auto build_operation = [&](const LayerPlan::PathMaskPlan& source) {
                        MaskOperation operation;
                        operation.type = source.type == "path" ? MaskType::Path
                            : source.type == "rect" ? MaskType::Rect
                            : source.type == "rounded_rect" ? MaskType::RoundedRect
                            : source.type == "field" ? MaskType::Field2D
                            : source.type == "text" ? MaskType::Text
                            : MaskType::Ellipse;
                        operation.mode = source.mode == "add" ? MaskCombineMode::Add
                            : source.mode == "subtract" ? MaskCombineMode::Subtract
                            : source.mode == "intersect" ? MaskCombineMode::Intersect
                            : MaskCombineMode::Difference;
                        operation.pos = Vec3{source.position[0], source.position[1], 0.0f};
                        operation.size = Vec2{source.size[0], source.size[1]};
                        operation.radius = source.radius;
                        operation.path = convert_mask_path(source.path);
                        operation.field = source.field;
                        operation.text_source = source.text_source;
                        operation.feather = source.feather_track
                            ? std::clamp(detail::evaluate_scalar_track(*source.feather_track,
                                ctx.effective_frame(), source.feather, plan.canvas.fps), 0.0f, 1000000.0f)
                            : source.feather;
                        operation.expansion = source.expansion_track
                            ? std::clamp(detail::evaluate_scalar_track(*source.expansion_track,
                                ctx.effective_frame(), source.expansion, plan.canvas.fps), -1000000.0f, 1000000.0f)
                            : source.expansion;
                        operation.opacity = source.opacity_track
                            ? std::clamp(detail::evaluate_scalar_track(*source.opacity_track,
                                ctx.effective_frame(), source.opacity, plan.canvas.fps), 0.0f, 1.0f)
                            : source.opacity;
                        operation.inverted = source.inverted;
                        Vec2 path_center{0.0f, 0.0f};
                        if (source.type == "path" && source.size[0] == 0.0f && source.size[1] == 0.0f) {
                            float min_x = std::numeric_limits<float>::max();
                            float min_y = std::numeric_limits<float>::max();
                            float max_x = std::numeric_limits<float>::lowest();
                            float max_y = std::numeric_limits<float>::lowest();
                            for (const auto& command : operation.path) {
                                if (command.type == PathCommandType::Close) continue;
                                const auto include = [&](Vec2 point) {
                                    min_x = std::min(min_x, point.x); min_y = std::min(min_y, point.y);
                                    max_x = std::max(max_x, point.x); max_y = std::max(max_y, point.y);
                                };
                                include(command.p0);
                                if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                                    include(command.p1);
                                if (command.type == PathCommandType::CubicTo) include(command.p2);
                            }
                            path_center = Vec2{(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f};
                            operation.size = Vec2{max_x - min_x, max_y - min_y};
                            operation.pos += Vec3{path_center, 0.0f};
                            for (auto& command : operation.path) {
                                if (command.type == PathCommandType::Close) continue;
                                command.p0 -= path_center;
                                if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                                    command.p1 -= path_center;
                                if (command.type == PathCommandType::CubicTo) command.p2 -= path_center;
                            }
                        }
                        if (source.morph_animation) {
                            AnimatedValue<float> progress{0.0f};
                            for (const auto& key : source.morph_animation->keyframes)
                                progress.add_keyframe(key.frame, key.value.front(),
                                    detail::animation_keyframe_easing(*source.morph_animation, key));
                            MorphPathParams morph;
                            morph.progress = std::clamp(progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                            morph.target = convert_mask_path(source.morph_target);
                            if (source.type == "path" && source.size[0] == 0.0f && source.size[1] == 0.0f)
                                for (auto& command : morph.target) {
                                    if (command.type == PathCommandType::Close) continue;
                                    command.p0 -= path_center;
                                    if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                                        command.p1 -= path_center;
                                    if (command.type == PathCommandType::CubicTo) command.p2 -= path_center;
                                }
                            PathShape path;
                            path.commands = std::move(operation.path);
                            operation.path = apply_path_operators(path, {
                                PathOperatorSpec{PathOperatorKind::Morph, std::move(morph), 0}}).front().commands;
                            if (source.type == "path" && source.size[0] == 0.0f &&
                                source.size[1] == 0.0f) {
                                float min_x = std::numeric_limits<float>::max();
                                float min_y = std::numeric_limits<float>::max();
                                float max_x = std::numeric_limits<float>::lowest();
                                float max_y = std::numeric_limits<float>::lowest();
                                for (const auto& command : operation.path) {
                                    if (command.type == PathCommandType::Close) continue;
                                    const auto include = [&](Vec2 point) {
                                        min_x = std::min(min_x, point.x);
                                        min_y = std::min(min_y, point.y);
                                        max_x = std::max(max_x, point.x);
                                        max_y = std::max(max_y, point.y);
                                    };
                                    include(command.p0);
                                    if (command.type == PathCommandType::QuadraticTo ||
                                        command.type == PathCommandType::CubicTo)
                                        include(command.p1);
                                    if (command.type == PathCommandType::CubicTo)
                                        include(command.p2);
                                }
                                operation.size = Vec2{max_x - min_x, max_y - min_y};
                            }
                        }
                        return operation;
                    };
                    std::vector<LayerPlan::PathMaskPlan> plan_masks;
                    if (layer.path_mask) plan_masks.push_back(*layer.path_mask);
                    plan_masks.insert(plan_masks.end(), layer.masks.begin(), layer.masks.end());
                    Mask composite;
                    const auto primary = build_operation(plan_masks.front());
                    composite.initial_mode = primary.mode;
                    composite.type = primary.type;
                    composite.pos = primary.pos;
                    composite.size = primary.size;
                    composite.radius = primary.radius;
                    composite.path = primary.path;
                    composite.field = primary.field;
                    composite.text_source = primary.text_source;
                    composite.feather = primary.feather;
                    composite.expansion = primary.expansion;
                    composite.opacity = primary.opacity;
                    composite.inverted = primary.inverted;
                    for (std::size_t index = 1; index < plan_masks.size(); ++index)
                        composite.operations.push_back(build_operation(plan_masks[index]));
                    builder.mask_composite(std::move(composite));
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
        for (const auto& source : point_sources) scene.point_light_source(source);
        return scene.build();
        
    };
}
} // namespace chronon3d::render_plan::detail
