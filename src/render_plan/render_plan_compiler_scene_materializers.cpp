#include "render_plan_compiler_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {
std::array<float, 3> inline_mesh_position_at(
    const InlineMeshPlan& mesh, std::size_t vertex, double frame) {
    if (mesh.animation.empty()) return mesh.vertices[vertex].position;
    const auto upper = std::lower_bound(
        mesh.animation.begin(), mesh.animation.end(), frame,
        [](const InlineMeshFrameSamplePlan& sample, double value) {
            return static_cast<double>(sample.frame) < value;
        });
    if (upper == mesh.animation.begin()) return upper->positions[vertex];
    if (upper == mesh.animation.end()) return mesh.animation.back().positions[vertex];
    if (static_cast<double>(upper->frame) == frame) return upper->positions[vertex];
    const auto& lower = *std::prev(upper);
    const float t = static_cast<float>((frame - static_cast<double>(lower.frame)) /
                                       static_cast<double>(upper->frame - lower.frame));
    const auto& a = lower.positions[vertex];
    const auto& b = upper->positions[vertex];
    return {a[0] + (b[0] - a[0]) * t,
            a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t};
}
} // namespace

void append_image_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    const std::string& resolved_ocio_config_path) {
    ImageParams params;
    params.asset_path = layer.asset;
    params.embedded_payload = layer.embedded_image_payload;
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
    return;

}

void append_video_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    std::size_t index,
    const std::vector<std::string>& resolved_video_paths,
    const std::string& resolved_ocio_config_path) {
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
    return;

}

void append_color_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    const chronon3d::FrameContext& ctx) {
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
        return;
    }
    RectParams params;
    params.size = solid_size;
    params.color = solid_color;
    if (layer.position_dimensions < 2) {
        builder.pin_to(Anchor::Center);
        params.pos = {-params.size.x * 0.5f, -params.size.y * 0.5f, 0.0f};
    }
    builder.rect("color", std::move(params));
    return;

}

void append_particles_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    std::size_t index,
    const std::vector<std::string>& resolved_particle_sprite_paths,
    const chronon3d::FrameContext& ctx) {
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
        return;
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
    return;

}

void append_light_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    const chronon3d::FrameContext& ctx,
    std::vector<rendering::PointLightSource>& point_sources) {
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
        return;
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
    return;

}

void append_mesh_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const chronon3d::FrameContext& ctx) {
    if (!layer.inline_mesh)
        throw std::runtime_error("mesh layer '" + layer.id + "' has no inline geometry");
    InlineMeshParams params;
    params.name = layer.id;
    params.texture_asset = layer.asset;
    params.vertices.reserve(layer.inline_mesh->vertices.size());
    const double mesh_frame = ctx.effective_frame();
    for (std::size_t vertex_index = 0;
         vertex_index < layer.inline_mesh->vertices.size(); ++vertex_index) {
        const auto& vertex = layer.inline_mesh->vertices[vertex_index];
        const auto position = inline_mesh_position_at(
            *layer.inline_mesh, vertex_index, mesh_frame);
        params.vertices.push_back(Vertex{
            Vec3{position[0], position[1], position[2]},
            Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            Vec2{vertex.uv[0], vertex.uv[1]}});
    }
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
    node.inline_mesh_animated = !layer.inline_mesh->animation.empty();
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
    return;

}

void append_shape_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    const chronon3d::FrameContext& ctx) {
    detail::append_shape_layer(builder, layer, plan, ctx);
    return;

}

void append_text_layer_for_scene(
    LayerBuilder& builder,
    const LayerPlan& layer,
    const RenderPlan& plan,
    std::size_t index,
    const std::vector<std::optional<chronon3d::PreparedText>>& prepared_texts,
    const std::vector<std::shared_ptr<const chronon3d::AnimatedTextDocument>>& prepared_text_counters,
    const std::vector<std::optional<chronon3d::Vec2>>& prepared_text_bounds,
    const std::shared_ptr<chronon3d::FontEngine>& font_engine) {
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
    return;

}



} // namespace chronon3d::render_plan::detail
