#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>

#include "render_plan_compiler_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace easing_name = chronon3d::render_plan::detail::easing_name;

namespace chronon3d::render_plan::detail {
namespace {

bool shape_uses_authored_origin(ShapeContentTypePlan type) {
    switch (type) {
        case ShapeContentTypePlan::Line:
        case ShapeContentTypePlan::Path:
        case ShapeContentTypePlan::Star:
        case ShapeContentTypePlan::Polygon:
        case ShapeContentTypePlan::Arrow:
        case ShapeContentTypePlan::Arc:
        case ShapeContentTypePlan::Grid:
        case ShapeContentTypePlan::DotGrid:
        case ShapeContentTypePlan::DeviceFrame:
        case ShapeContentTypePlan::RingSegment:
        case ShapeContentTypePlan::MonotoneLine:
        case ShapeContentTypePlan::MonotoneChart:
            return true;
        default:
            return false;
    }
}

} // namespace

void configure_shape_path_gradient_fill(
    chronon3d::Fill& fill, const graphics::GradientDefinition& gradient) {
    fill.enabled = true;
    fill.type = fill_type_for_gradient(gradient.type);
    fill.gradient = gradient;
}

void append_shape_layer(chronon3d::LayerBuilder& builder,
                        const LayerPlan& layer, const RenderPlan& plan,
                        const chronon3d::FrameContext& ctx) {
    if (!layer.shape)
        throw std::runtime_error("shape layer '" + layer.id + "' has no shape payload");
    const auto& shape = *layer.shape;
    const Vec2 size = layer.size_dimensions == 2
        ? Vec2{layer.size[0], layer.size[1]}
        : Vec2{static_cast<float>(plan.canvas.width),
               static_cast<float>(plan.canvas.height)};
    const Color authored_color{layer.color[0], layer.color[1],
                               layer.color[2], layer.color[3]};
    const Color base_color = detail::fill_color_at(
        layer, ctx.effective_frame(), authored_color,
        plan.canvas.fps).value_or(authored_color);
    const auto animated_gradient = detail::fill_gradient_at(
        layer, ctx.effective_frame());
    const auto& active_gradient = animated_gradient
        ? animated_gradient : shape.gradient;
    std::optional<graphics::Field2D> sampled_field = shape.field
        ? std::optional<graphics::Field2D>{*shape.field} : std::nullopt;
    if (sampled_field && shape.field_drift) {
        const double fps = static_cast<double>(plan.canvas.fps.numerator) /
            static_cast<double>(plan.canvas.fps.denominator);
        if (!std::isfinite(fps) || fps <= 0.0)
            throw std::runtime_error("animated Field2D requires a positive canvas frame rate");
        const double seconds = static_cast<double>(ctx.effective_frame()) / fps;
        try {
            sampled_field = sampled_field->drifted(seconds, *shape.field_drift);
        } catch (const std::invalid_argument& error) {
            throw std::runtime_error(error.what());
        }
    }
    if ((active_gradient || shape.gradient_mesh) && shape.field)
        throw std::runtime_error("shape '" + layer.id +
            "' cannot combine gradient/mesh fill with Field2D");
    std::optional<graphics::FillStyle> fill;
    if (active_gradient) {
        fill = graphics::FillStyle::solid(base_color);
        fill->gradient = *active_gradient;
    } else if (shape.gradient_mesh) {
        fill = graphics::FillStyle::solid(base_color);
        fill->gradient_mesh = *shape.gradient_mesh;
    } else if (shape.fill_color) {
        fill = graphics::FillStyle::solid(base_color);
    }
    if (shape.field) {
        fill = graphics::FillStyle::solid(base_color);
        fill->field = *sampled_field;
        fill->field_ramp = shape.field_ramp;
        fill->field_render_scale = shape.field_render_scale;
        fill->field_render_size = size;
    }
    graphics::StrokeStyle stroke;
    if (shape.stroke) {
        stroke.enabled = true;
        stroke.width = shape.stroke->width.value_or(1.0f);
        if (shape.stroke->solid_color) {
            const auto& rgba = *shape.stroke->solid_color;
            stroke.color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
        } else if (!shape.stroke->color.empty() && !shape.stroke->gradient) {
            const auto parsed = parse_hex_color(shape.stroke->color);
            if (!parsed)
                throw std::runtime_error("shape layer '" + layer.id + "' has invalid stroke color");
            stroke.color = *parsed;
        }
        stroke.gradient = shape.stroke->gradient;
        stroke.brush = shape.stroke->brush;
        stroke.cap = shape.stroke->cap == "round" ? LineCap::Round
            : shape.stroke->cap == "square" ? LineCap::Square : LineCap::Butt;
        stroke.join = shape.stroke->join == "round" ? LineJoin::Round
            : shape.stroke->join == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
        stroke.miter_limit = shape.stroke->miter_limit;
        stroke.taper_start = shape.stroke->taper_start;
        stroke.taper_end = shape.stroke->taper_end;
        stroke.trim_start = shape.stroke->trim_start.value_or(0.0f);
        stroke.trim_end = shape.stroke->trim_end.value_or(1.0f);
        stroke.dash_array = shape.stroke->dash_array;
        stroke.dash_offset = shape.stroke->dash_offset;
        if (const auto animated_width = detail::stroke_width_at(
                layer, ctx.effective_frame(), stroke.width,
                plan.canvas.fps))
            stroke.width = *animated_width;
        if (!shape.stroke->gradient) {
            if (const auto animated_color = detail::stroke_color_at(
                    layer, ctx.effective_frame(), stroke.color,
                    plan.canvas.fps))
                stroke.color = *animated_color;
        }
        if (const auto animated_offset=detail::stroke_dash_offset_at(
                layer, ctx.effective_frame(), plan.canvas.fps))
            stroke.dash_offset=*animated_offset;
    }
    if (layer.position_dimensions < 2 && !shape_uses_authored_origin(shape.type))
        builder.pin_to(Anchor::Center);
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
        case ShapeContentTypePlan::Path:
        case ShapeContentTypePlan::Star:
        case ShapeContentTypePlan::Polygon:
        case ShapeContentTypePlan::Arrow:
        case ShapeContentTypePlan::Arc:
        case ShapeContentTypePlan::Grid:
        case ShapeContentTypePlan::DotGrid:
        case ShapeContentTypePlan::RingSegment:
        case ShapeContentTypePlan::MonotoneLine:
        case ShapeContentTypePlan::MonotoneChart: {
            append_path_shape(builder, layer, plan, ctx, shape, size, base_color,
                              active_gradient, sampled_field, stroke);
            break;
        }
        case ShapeContentTypePlan::DeviceFrame: {
            const float width = size.x;
            const float height = size.y;
            if (layer.position_dimensions < 2)
                builder.pin_to(Anchor::Center);
            const bool phone = shape.device_frame_kind == DeviceFrameKindPlan::Phone;
            const float bezel = std::min(width, height) * (phone ? 0.035f : 0.02f);
            const Color shell_color{0.035f, 0.045f, 0.06f, 1.0f};
            const Color chrome_color{0.055f, 0.065f, 0.085f, 1.0f};
            RoundedRectParams shell;
            shell.size = size;
            shell.radius = std::min(width, height) * (phone ? 0.12f : 0.055f);
            shell.color = shell_color;
            shell.fill = graphics::FillStyle::solid(shell_color);
            shell.stroke = stroke;
            shell.pos = {-width * 0.5f, -height * 0.5f, 0.0f};
            builder.rounded_rect("device_shell", std::move(shell));

            RoundedRectParams screen;
            screen.size = {std::max(1.0f, width - bezel * 2.0f),
                           std::max(1.0f, height - bezel * 2.0f)};
            const Vec2 screen_size = screen.size;
            screen.radius = std::min(screen.size.x, screen.size.y) *
                (phone ? 0.085f : 0.025f);
            screen.color = base_color;
            screen.fill = fill;
            screen.pos = {-width * 0.5f + bezel,
                          -height * 0.5f + bezel, 0.0f};
            builder.rounded_rect("device_screen", std::move(screen));

            if (shape.device_frame_chrome && phone) {
                const float notch_width = std::min(screen_size.x * 0.70f, width * 0.30f);
                const float notch_height = std::min(screen_size.y * 0.20f,
                    std::max(2.0f, height * 0.035f));
                RoundedRectParams notch;
                notch.size = {notch_width, notch_height};
                notch.radius = notch_height * 0.5f;
                notch.color = chrome_color;
                notch.fill = graphics::FillStyle::solid(chrome_color);
                notch.pos = {-notch_width * 0.5f,
                    -height * 0.5f + bezel, 0.0f};
                builder.rounded_rect("device_notch", std::move(notch));
            } else if (shape.device_frame_chrome) {
                const float toolbar_height = std::min(screen_size.y,
                    std::max(8.0f, height * 0.13f));
                RoundedRectParams toolbar;
                toolbar.size = {width - bezel * 2.0f, toolbar_height};
                toolbar.radius = std::min(4.0f, bezel);
                toolbar.color = chrome_color;
                toolbar.fill = graphics::FillStyle::solid(chrome_color);
                toolbar.pos = {-width * 0.5f + bezel,
                    -height * 0.5f + bezel, 0.0f};
                builder.rounded_rect("browser_toolbar", std::move(toolbar));
                const std::array<Color, 3> lights{
                    Color{0.96f, 0.28f, 0.25f, 1.0f},
                    Color{0.98f, 0.72f, 0.20f, 1.0f},
                    Color{0.23f, 0.78f, 0.36f, 1.0f}};
                for (std::size_t light = 0; light < lights.size(); ++light) {
                    CircleParams dot;
                    dot.radius = std::min(toolbar_height * 0.22f,
                        std::max(1.0f, bezel * 0.20f));
                    dot.color = lights[light];
                    dot.fill = graphics::FillStyle::solid(lights[light]);
                    dot.pos = {-width * 0.5f + bezel +
                        dot.radius + static_cast<float>(light) * dot.radius * 3.2f,
                        -height * 0.5f + bezel + toolbar_height * 0.5f, 0.0f};
                    builder.circle("browser_traffic_light_" +
                        std::to_string(light), std::move(dot));
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
            .saturation = backdrop.saturation,
            .rim_light = backdrop.rim_light,
            .refraction = backdrop.refraction,
            .surface_noise = backdrop.surface_noise});
    }

}

} // namespace chronon3d::render_plan::detail
