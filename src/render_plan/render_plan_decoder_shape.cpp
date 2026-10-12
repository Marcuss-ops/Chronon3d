#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/shape_path_generator.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {

graphics::GradientMesh decode_gradient_mesh(const nlohmann::json& value) {
    graphics::GradientMesh mesh;
    const auto& nodes = value.at("nodes");
    if (!nodes.is_array() || nodes.empty() || nodes.size() > 256)
        throw std::runtime_error("gradient mesh requires between 1 and 256 nodes");
    mesh.nodes.reserve(nodes.size());
    for (const auto& raw_node : nodes) {
        if (!raw_node.is_object() || !raw_node.contains("position") ||
            !raw_node.contains("color"))
            throw std::runtime_error("gradient mesh nodes require position and color");
        const auto& position = raw_node.at("position");
        const auto& color = raw_node.at("color");
        if (!position.is_array() || position.size() != 2 ||
            !color.is_array() || color.size() != 4)
            throw std::runtime_error("gradient mesh node position and color must contain two and four values");
        graphics::GradientMeshNode node;
        node.position = {position[0].get<float>(), position[1].get<float>()};
        node.color = Color{color[0].get<float>(), color[1].get<float>(),
                           color[2].get<float>(), color[3].get<float>()};
        node.weight = raw_node.value("weight", 1.0f);
        const auto bounded = [](float component) {
            return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
        };
        if (!bounded(node.position.x) || !bounded(node.position.y) ||
            !bounded(node.color.r) || !bounded(node.color.g) ||
            !bounded(node.color.b) || !bounded(node.color.a) ||
            !std::isfinite(node.weight) || node.weight < 0.0001f || node.weight > 1000.0f)
            throw std::runtime_error("gradient mesh positions and RGBA must be in [0, 1]; weights must be in [0.0001, 1000]");
        mesh.nodes.push_back(node);
    }
    return mesh;
}

GradientAnimationPlan decode_gradient_animation(const nlohmann::json& value) {
    GradientAnimationPlan animation;
    animation.easing = value.value("easing", std::string{"linear"});
    animation.bezier_curve = decode_bezier_curve(
        value, animation.easing, "fill_gradient_animation");
    for (const auto& key_value : value.at("keyframes")) {
        GradientKeyframePlan key;
        key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
        key.value = decode_gradient_definition(key_value.at("value"));
        animation.keyframes.push_back(std::move(key));
    }
    return animation;
}
}  // namespace

void decode_shape_and_fill_gradient(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("shape")) {
        const auto& raw_shape = value.at("shape");
        ShapeContentPlan shape;
        const auto shape_type = raw_shape.at("type").get<std::string>();
        if (shape_type == "rect") shape.type = ShapeContentTypePlan::Rect;
        else if (shape_type == "rounded_rect") shape.type = ShapeContentTypePlan::RoundedRect;
        else if (shape_type == "ellipse") shape.type = ShapeContentTypePlan::Ellipse;
        else if (shape_type == "line") shape.type = ShapeContentTypePlan::Line;
        else if (shape_type == "path") shape.type = ShapeContentTypePlan::Path;
        else if (shape_type == "star") shape.type = ShapeContentTypePlan::Star;
        else if (shape_type == "polygon") shape.type = ShapeContentTypePlan::Polygon;
        else if (shape_type == "arrow") shape.type = ShapeContentTypePlan::Arrow;
        else if (shape_type == "arc") shape.type = ShapeContentTypePlan::Arc;
        else if (shape_type == "grid") shape.type = ShapeContentTypePlan::Grid;
        else if (shape_type == "dot_grid" || shape_type == "dotgrid") shape.type = ShapeContentTypePlan::DotGrid;
        else if (shape_type == "device_frame") shape.type = ShapeContentTypePlan::DeviceFrame;
        else throw std::runtime_error("unsupported shape.type: " + shape_type);
        if (raw_shape.contains("asset")) {
            if (shape.type != ShapeContentTypePlan::Path || raw_shape.contains("path"))
                throw std::runtime_error("shape.asset is only valid for path shapes and cannot be combined with path commands");
            shape.svg_asset = raw_shape.at("asset").get<std::string>();
            if (detail::invalid_logical_path(*shape.svg_asset) ||
                std::filesystem::path(*shape.svg_asset).extension() != ".svg")
                throw std::runtime_error("shape.asset must be a relative logical .svg path");
        }
        if (raw_shape.contains("lottie")) {
            if (shape.type != ShapeContentTypePlan::Path || raw_shape.contains("path") ||
                raw_shape.contains("asset"))
                throw std::runtime_error("shape.lottie is only valid for path shapes and cannot be combined with path commands or SVG");
            shape.lottie_asset = raw_shape.at("lottie").get<std::string>();
            if (detail::invalid_logical_path(*shape.lottie_asset) ||
                std::filesystem::path(*shape.lottie_asset).extension() != ".json")
                throw std::runtime_error("shape.lottie must be a relative logical .json path");
        }

        if (raw_shape.contains("fill_rule")) {
            if (shape.type != ShapeContentTypePlan::Path)
                throw std::runtime_error("shape.fill_rule is only valid for path shapes");
            const auto fill_rule = raw_shape.at("fill_rule").get<std::string>();
            if (fill_rule == "even_odd") shape.fill_rule = ShapeFillRulePlan::EvenOdd;
            else if (fill_rule == "non_zero") shape.fill_rule = ShapeFillRulePlan::NonZero;
            else throw std::runtime_error("shape.fill_rule must be 'even_odd' or 'non_zero'");
        }
        if (raw_shape.contains("field") || raw_shape.contains("field_ramp") ||
            raw_shape.contains("field_render_scale") || raw_shape.contains("field_drift"))
            throw std::runtime_error("Field2D source, ramp, scale and drift must be nested under shape.fill");
        if (layer.size_dimensions != 2 || !std::isfinite(layer.size[0]) ||
            !std::isfinite(layer.size[1]) || layer.size[0] <= 0.0f || layer.size[1] <= 0.0f)
            throw std::runtime_error("shape layers require positive two-dimensional size");
        shape.radius = raw_shape.value("radius", 0.0f);
        if (raw_shape.contains("corner_radius")) {
            if (shape.type != ShapeContentTypePlan::RoundedRect)
                throw std::runtime_error("shape.corner_radius is only valid for rounded_rect");
            if (raw_shape.contains("radius"))
                throw std::runtime_error("shape.radius and shape.corner_radius are mutually exclusive");
            const auto& corners = raw_shape.at("corner_radius");
            if (!corners.is_array() || corners.size() != 4)
                throw std::runtime_error("shape.corner_radius must contain [tl, tr, br, bl]");
            std::array<float, 4> decoded{};
            const float max_radius = std::min(layer.size[0], layer.size[1]) * 0.5f;
            for (std::size_t i = 0; i < decoded.size(); ++i) {
                decoded[i] = corners.at(i).get<float>();
                if (!std::isfinite(decoded[i]) || decoded[i] < 0.0f || decoded[i] > max_radius)
                    throw std::runtime_error("rounded_rect corner radius must be finite, non-negative, and not exceed half the smaller size dimension");
            }
            shape.corner_radius = decoded;
        }
        if (!std::isfinite(shape.radius) || shape.radius < 0.0f)
            throw std::runtime_error("shape.radius must be finite and non-negative");
        if (shape.type == ShapeContentTypePlan::RoundedRect) {
            const float max_radius = std::min(layer.size[0], layer.size[1]) * 0.5f;
            if (shape.radius > max_radius)
                throw std::runtime_error(
                    "rounded_rect radius must not exceed half the smaller size dimension");
        }
        if (shape.type == ShapeContentTypePlan::Line && !raw_shape.contains("stroke"))
            throw std::runtime_error("line shapes require a stroke");
        if ((shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon ||
             shape.type == ShapeContentTypePlan::Arrow || shape.type == ShapeContentTypePlan::Arc) &&
            (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie")))
            throw std::runtime_error("procedural shapes cannot be combined with authored path or asset data");
        const bool procedural = shape.type == ShapeContentTypePlan::Star ||
            shape.type == ShapeContentTypePlan::Polygon || shape.type == ShapeContentTypePlan::Arrow ||
            shape.type == ShapeContentTypePlan::Arc || shape.type == ShapeContentTypePlan::Grid ||
            shape.type == ShapeContentTypePlan::DotGrid || shape.type == ShapeContentTypePlan::DeviceFrame;
        if (shape.type != ShapeContentTypePlan::DeviceFrame &&
            (raw_shape.contains("device") || raw_shape.contains("chrome")))
            throw std::runtime_error("device and chrome options are only valid for device_frame");
        if (shape.type == ShapeContentTypePlan::DeviceFrame && raw_shape.contains("operators"))
            throw std::runtime_error("device_frame does not accept path operators");
        if (procedural && (raw_shape.contains("radius") || raw_shape.contains("corner_radius")))
            throw std::runtime_error("procedural shapes do not accept rounded-rectangle radius fields");
        if (shape.type == ShapeContentTypePlan::Star &&
            (raw_shape.contains("shaft_ratio") || raw_shape.contains("head_ratio") ||
             raw_shape.contains("start_degrees") || raw_shape.contains("sweep_degrees") ||
             raw_shape.contains("segments")))
            throw std::runtime_error("star received parameters for another procedural shape");
        if (shape.type == ShapeContentTypePlan::Polygon &&
            (raw_shape.contains("inner_radius_ratio") || raw_shape.contains("shaft_ratio") ||
             raw_shape.contains("head_ratio") || raw_shape.contains("start_degrees") ||
             raw_shape.contains("sweep_degrees") || raw_shape.contains("segments")))
            throw std::runtime_error("polygon received parameters for another procedural shape");
        if (shape.type == ShapeContentTypePlan::Arrow &&
            (raw_shape.contains("points") || raw_shape.contains("inner_radius_ratio") ||
             raw_shape.contains("rotation_degrees") || raw_shape.contains("start_degrees") ||
             raw_shape.contains("sweep_degrees") || raw_shape.contains("segments")))
            throw std::runtime_error("arrow received parameters for another procedural shape");
        if (shape.type == ShapeContentTypePlan::Arc &&
            (raw_shape.contains("points") || raw_shape.contains("inner_radius_ratio") ||
             raw_shape.contains("rotation_degrees") || raw_shape.contains("shaft_ratio") ||
             raw_shape.contains("head_ratio") || raw_shape.contains("fill") || value.contains("color")))
            throw std::runtime_error("arc received parameters for another procedural shape or a fill");
        if (shape.type == ShapeContentTypePlan::Grid &&
            (raw_shape.contains("points") || raw_shape.contains("inner_radius_ratio") ||
             raw_shape.contains("rotation_degrees") || raw_shape.contains("shaft_ratio") ||
             raw_shape.contains("head_ratio") || raw_shape.contains("start_degrees") ||
             raw_shape.contains("sweep_degrees") || raw_shape.contains("segments") ||
             raw_shape.contains("dot_radius") || raw_shape.contains("fill") || value.contains("color")))
            throw std::runtime_error("grid accepts spacing and stroke only");
        if (shape.type == ShapeContentTypePlan::DotGrid &&
            (raw_shape.contains("points") || raw_shape.contains("inner_radius_ratio") ||
             raw_shape.contains("rotation_degrees") || raw_shape.contains("shaft_ratio") ||
             raw_shape.contains("head_ratio") || raw_shape.contains("start_degrees") ||
             raw_shape.contains("sweep_degrees") || raw_shape.contains("segments")))
            throw std::runtime_error("dot_grid received parameters for another procedural shape");
        if (shape.type == ShapeContentTypePlan::DeviceFrame &&
            (raw_shape.contains("points") || raw_shape.contains("inner_radius_ratio") ||
             raw_shape.contains("rotation_degrees") || raw_shape.contains("shaft_ratio") ||
             raw_shape.contains("head_ratio") || raw_shape.contains("start_degrees") ||
             raw_shape.contains("sweep_degrees") || raw_shape.contains("segments") ||
             raw_shape.contains("spacing") || raw_shape.contains("dot_radius")))
            throw std::runtime_error("device_frame received parameters for another procedural shape");
        if (shape.type == ShapeContentTypePlan::Star) {
            const auto points = raw_shape.value("points", 5u);
            const auto inner = raw_shape.value("inner_radius_ratio", 0.5f);
            const auto rotation = raw_shape.value("rotation_degrees", 0.0f);
            shape.path = make_star_path(layer.size[0], layer.size[1], points, inner, rotation);
        } else if (shape.type == ShapeContentTypePlan::Polygon) {
            shape.path = make_polygon_path(layer.size[0], layer.size[1],
                raw_shape.value("points", 6u), raw_shape.value("rotation_degrees", 0.0f));
        } else if (shape.type == ShapeContentTypePlan::Arrow) {
            shape.path = make_arrow_path(layer.size[0], layer.size[1],
                raw_shape.value("shaft_ratio", 0.28f), raw_shape.value("head_ratio", 0.75f));
        } else if (shape.type == ShapeContentTypePlan::Arc) {
            if (!raw_shape.contains("stroke"))
                throw std::runtime_error("arc shapes require a stroke");
            shape.path = make_arc_path(layer.size[0], layer.size[1],
                raw_shape.value("start_degrees", 0.0f), raw_shape.value("sweep_degrees", 270.0f),
                raw_shape.value("segments", 64u));
        } else if (shape.type == ShapeContentTypePlan::Grid) {
            if (!raw_shape.contains("stroke"))
                throw std::runtime_error("grid shapes require a stroke");
            shape.path = make_grid_path(layer.size[0], layer.size[1],
                raw_shape.value("spacing", 16.0f));
        } else if (shape.type == ShapeContentTypePlan::DotGrid) {
            const float spacing = raw_shape.value("spacing", 16.0f);
            const float radius = raw_shape.value("dot_radius", spacing * 0.12f);
            shape.path = make_dot_grid_path(layer.size[0], layer.size[1], spacing, radius);
        } else if (shape.type == ShapeContentTypePlan::DeviceFrame) {
            const auto device = raw_shape.value("device", std::string{"phone"});
            if (device == "phone") shape.device_frame_kind = DeviceFrameKindPlan::Phone;
            else if (device == "browser") shape.device_frame_kind = DeviceFrameKindPlan::Browser;
            else throw std::runtime_error("device_frame device must be 'phone' or 'browser'");
            shape.device_frame_chrome = raw_shape.value("chrome", true);
            if (!raw_shape.contains("fill"))
                throw std::runtime_error("device_frame requires shape.fill for the screen");
        }
        if (raw_shape.contains("fill") && raw_shape.at("fill").is_array()) {
            // Shape-local fill is canonical. Legacy layer.color is only an alias
            // for plans that omit shape.fill; it never overrides explicit fill.
        } else if (raw_shape.contains("fill") && raw_shape.at("fill").is_object()) {
            // Gradient payload is decoded below.
        } else if (value.contains("color") && value.at("color").is_array() &&
                   shape.type != ShapeContentTypePlan::Grid && shape.type != ShapeContentTypePlan::DotGrid) {
            const auto& alias = value.at("color");
            if (alias.size() != 4)
                throw std::runtime_error("legacy layer.color shape alias must contain RGBA");
            shape.fill_color = std::array<float, 4>{
                alias[0].get<float>(), alias[1].get<float>(),
                alias[2].get<float>(), alias[3].get<float>()};
        }
        const auto finite_point = [](const std::array<float, 2>& point) {
            return std::isfinite(point[0]) && std::isfinite(point[1]);
        };
        const auto finite_vec2 = [](const Vec2& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        };
        if (raw_shape.contains("path")) {
            for (const auto& raw_command : raw_shape.at("path")) {
                shape.path.push_back(decode_shape_path_command(raw_command, "shape path"));
            }
        }
        if ((shape.type == ShapeContentTypePlan::Line ||
             shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon ||
             shape.type == ShapeContentTypePlan::Arrow || shape.type == ShapeContentTypePlan::Arc ||
             shape.type == ShapeContentTypePlan::Grid || shape.type == ShapeContentTypePlan::DotGrid ||
             (shape.type == ShapeContentTypePlan::Path && !shape.svg_asset &&
              !shape.lottie_asset)) && shape.path.empty())
            throw std::runtime_error("line and path shapes require path commands");
        // ── Star / Polygon: points, inner_ratio, rotation ───────────────
        if (shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon) {
            if (raw_shape.contains("points")) {
                const int pts = raw_shape.at("points").get<int>();
                if (pts < 3 || pts > 64)
                    throw std::runtime_error("shape.points must be in [3, 64]");
                shape.points = pts;
            } else {
                shape.points = (shape.type == ShapeContentTypePlan::Star ? 5 : 6);
            }
            if (raw_shape.contains("inner_radius_ratio")) {
                shape.inner_radius_ratio = raw_shape.at("inner_radius_ratio").get<float>();
            }
            if (raw_shape.contains("innerRatio"))
                shape.inner_radius_ratio = raw_shape.at("innerRatio").get<float>();
            if (!std::isfinite(shape.inner_radius_ratio) || shape.inner_radius_ratio < 0.05f || shape.inner_radius_ratio > 0.95f)
                throw std::runtime_error("shape.inner_radius_ratio must be finite in [0.05, 0.95]");
            if (raw_shape.contains("shape_rotation_deg"))
                shape.shape_rotation_deg = raw_shape.at("shape_rotation_deg").get<float>();
            if (raw_shape.contains("rotation_deg"))
                shape.shape_rotation_deg = raw_shape.at("rotation_deg").get<float>();
            if (!std::isfinite(shape.shape_rotation_deg))
                throw std::runtime_error("shape_rotation_deg must be finite");
            // Forbid unrelated payloads
            if (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie"))
                throw std::runtime_error("star/polygon shapes cannot carry path/asset/lottie");
        }
        // ── Arrow ─────────────────────────────────────────────────────────
        if (shape.type == ShapeContentTypePlan::Arrow) {
            if (raw_shape.contains("arrow_head_length"))
                shape.arrow_head_length = raw_shape.at("arrow_head_length").get<float>();
            if (raw_shape.contains("head_length"))
                shape.arrow_head_length = raw_shape.at("head_length").get<float>();
            if (raw_shape.contains("arrow_head_width"))
                shape.arrow_head_width = raw_shape.at("arrow_head_width").get<float>();
            if (raw_shape.contains("head_width"))
                shape.arrow_head_width = raw_shape.at("head_width").get<float>();
            if (raw_shape.contains("arrow_shaft_width"))
                shape.arrow_shaft_width = raw_shape.at("arrow_shaft_width").get<float>();
            if (raw_shape.contains("shaft_width"))
                shape.arrow_shaft_width = raw_shape.at("shaft_width").get<float>();
            if (raw_shape.contains("arrow_direction"))
                shape.arrow_direction = raw_shape.at("arrow_direction").get<std::string>();
            if (raw_shape.contains("direction"))
                shape.arrow_direction = raw_shape.at("direction").get<std::string>();
            if (shape.arrow_head_length && (!std::isfinite(*shape.arrow_head_length) || *shape.arrow_head_length <= 0.0f || *shape.arrow_head_length > 10000.0f))
                throw std::runtime_error("arrow_head_length must be finite in (0, 10000]");
            if (shape.arrow_head_width && (!std::isfinite(*shape.arrow_head_width) || *shape.arrow_head_width <= 0.0f || *shape.arrow_head_width > 10000.0f))
                throw std::runtime_error("arrow_head_width must be finite in (0, 10000]");
            if (!std::isfinite(shape.arrow_shaft_width) || shape.arrow_shaft_width <= 0.0f || shape.arrow_shaft_width > 10000.0f)
                throw std::runtime_error("arrow_shaft_width must be finite in (0, 10000]");
            if (shape.arrow_direction != "right" && shape.arrow_direction != "left" &&
                shape.arrow_direction != "up" && shape.arrow_direction != "down" &&
                shape.arrow_direction != "both")
                throw std::runtime_error("arrow_direction must be right, left, up, down, or both");
            if (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie"))
                throw std::runtime_error("arrow shapes cannot carry path/asset/lottie");
        }
        // ── Grid / DotGrid ────────────────────────────────────────────────
        if (shape.type == ShapeContentTypePlan::Grid || shape.type == ShapeContentTypePlan::DotGrid) {
            if (raw_shape.contains("grid_spacing"))
                shape.grid_spacing = raw_shape.at("grid_spacing").get<float>();
            if (raw_shape.contains("spacing"))
                shape.grid_spacing = raw_shape.at("spacing").get<float>();
            if (!std::isfinite(shape.grid_spacing) || shape.grid_spacing < 4.0f || shape.grid_spacing > 512.0f)
                throw std::runtime_error("grid_spacing must be finite in [4, 512]");
            if (shape.type == ShapeContentTypePlan::Grid) {
                if (raw_shape.contains("grid_line_width"))
                    shape.grid_line_width = raw_shape.at("grid_line_width").get<float>();
                if (raw_shape.contains("line_width"))
                    shape.grid_line_width = raw_shape.at("line_width").get<float>();
                if (!std::isfinite(shape.grid_line_width) || shape.grid_line_width < 0.25f || shape.grid_line_width > 32.0f)
                    throw std::runtime_error("grid_line_width must be finite in [0.25, 32]");
            } else {
                if (raw_shape.contains("dot_radius"))
                    shape.dot_radius = raw_shape.at("dot_radius").get<float>();
                if (raw_shape.contains("radius")) // arc-style radius alias not applicable
                    ; // keep dot_radius separate; radius alias already handled above as shape.radius
                if (raw_shape.contains("dotRadius"))
                    shape.dot_radius = raw_shape.at("dotRadius").get<float>();
                if (!std::isfinite(shape.dot_radius) || shape.dot_radius < 0.5f || shape.dot_radius > 64.0f)
                    throw std::runtime_error("dot_radius must be finite in [0.5, 64]");
            }
            if (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie"))
                throw std::runtime_error("grid/dot_grid shapes cannot carry path/asset/lottie");
        }
        // ── Arc ───────────────────────────────────────────────────────────
        if (shape.type == ShapeContentTypePlan::Arc) {
            if (raw_shape.contains("arc_start_deg"))
                shape.arc_start_deg = raw_shape.at("arc_start_deg").get<float>();
            if (raw_shape.contains("start_deg"))
                shape.arc_start_deg = raw_shape.at("start_deg").get<float>();
            if (raw_shape.contains("arc_sweep_deg"))
                shape.arc_sweep_deg = raw_shape.at("arc_sweep_deg").get<float>();
            if (raw_shape.contains("sweep_deg"))
                shape.arc_sweep_deg = raw_shape.at("sweep_deg").get<float>();
            if (raw_shape.contains("arc_thickness"))
                shape.arc_thickness = raw_shape.at("arc_thickness").get<float>();
            if (raw_shape.contains("thickness"))
                shape.arc_thickness = raw_shape.at("thickness").get<float>();
            if (!std::isfinite(shape.arc_start_deg))
                throw std::runtime_error("arc_start_deg must be finite");
            if (!std::isfinite(shape.arc_sweep_deg) || shape.arc_sweep_deg < 1.0f || shape.arc_sweep_deg > 359.9f)
                throw std::runtime_error("arc_sweep_deg must be finite in [1, 359.9]");
            if (!std::isfinite(shape.arc_thickness) || shape.arc_thickness < 0.0f || shape.arc_thickness > 10000.0f)
                throw std::runtime_error("arc_thickness must be finite in [0, 10000]");
            if (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie"))
                throw std::runtime_error("arc shapes cannot carry path/asset/lottie");
        }
        // ── DeviceFrame (additive, not primary shape type) ───────────────
        if (raw_shape.contains("device_frame")) {
            const auto& src = raw_shape.at("device_frame");
            if (!src.is_object()) throw std::runtime_error("device_frame must be an object");
            DeviceFramePlan df;
            if (src.contains("frame_type")) df.frame_type = src.at("frame_type").get<std::string>();
            if (src.contains("type")) df.frame_type = src.at("type").get<std::string>();
            if (df.frame_type != "browser" && df.frame_type != "phone" && df.frame_type != "window")
                throw std::runtime_error("device_frame type must be browser, phone, or window");
            if (src.contains("radius")) df.radius = src.at("radius").get<float>();
            if (src.contains("border_width")) df.border_width = src.at("border_width").get<float>();
            if (!std::isfinite(df.radius) || df.radius < 0.0f || df.radius > 512.0f) throw std::runtime_error("device_frame radius out of range");
            if (!std::isfinite(df.border_width) || df.border_width < 0.0f || df.border_width > 64.0f) throw std::runtime_error("device_frame border_width out of range");
            if (src.contains("border_color")) df.border_color = src.at("border_color").get<std::string>();
            if (src.contains("show_chrome")) df.show_chrome = src.at("show_chrome").get<bool>();
            shape.device_frame = std::move(df);
        }
        if (raw_shape.contains("operators")) {
            const auto& raw_ops = raw_shape.at("operators");
            if (!raw_ops.is_array() || raw_ops.size() > 16)
                throw std::runtime_error("shape.operators must be an array with at most 16 entries");
            for (const auto& raw_op : raw_ops) {
                if (!raw_op.is_object() || !raw_op.contains("kind") || !raw_op.contains("params"))
                    throw std::runtime_error("unsupported path operator");
                const auto& p = raw_op.at("params");
                if (!p.is_object()) throw std::runtime_error("path operator params must be an object");
                const auto kind = raw_op.at("kind").get<std::string>();
                PathOperatorPlan decoded_op;
                if (kind == "repeater") {
                    decoded_op.kind = PathOperatorKindPlan::Repeater;
                    auto& r = decoded_op.repeater;
                    r.copies = p.at("copies").get<std::uint32_t>();
                    const auto read2 = [&](const char* key, std::array<float, 2>& out) {
                        if (!p.contains(key)) return;
                        const auto& a = p.at(key);
                        if (!a.is_array() || a.size() != 2)
                            throw std::runtime_error(std::string("repeater ") + key + " requires two values");
                        out = {a[0].get<float>(), a[1].get<float>()};
                    };
                    read2("position", r.position);
                    read2("scale", r.scale);
                    read2("anchor", r.anchor);
                    r.rotation_degrees = p.value("rotation_degrees", 0.0f);
                    r.opacity_decay = p.value("opacity_decay", 1.0f);
                    if (r.copies == 0 || r.copies > 10000 ||
                        !std::isfinite(r.rotation_degrees) || !std::isfinite(r.opacity_decay) ||
                        r.opacity_decay < 0.0f || r.opacity_decay > 1.0f ||
                        !std::isfinite(r.position[0]) || !std::isfinite(r.position[1]) ||
                        !std::isfinite(r.scale[0]) || !std::isfinite(r.scale[1]) ||
                        !std::isfinite(r.anchor[0]) || !std::isfinite(r.anchor[1]))
                        throw std::runtime_error("invalid repeater parameters");
                } else if (kind == "round_corners") {
                    decoded_op.kind = PathOperatorKindPlan::RoundCorners;
                    decoded_op.radius = p.at("radius").get<float>();
                    if (!std::isfinite(decoded_op.radius) || decoded_op.radius < 0.0f)
                        throw std::runtime_error("round-corners radius must be finite and non-negative");
                } else if (kind == "zig_zag") {
                    decoded_op.kind = PathOperatorKindPlan::ZigZag;
                    decoded_op.size = p.at("size").get<float>();
                    decoded_op.points_per_unit = p.value("points_per_unit", 0.1f);
                    decoded_op.smooth = p.value("smooth", false);
                    if (!std::isfinite(decoded_op.size) || decoded_op.size < 0.0f ||
                        !std::isfinite(decoded_op.points_per_unit) ||
                        decoded_op.points_per_unit <= 0.0f || decoded_op.points_per_unit > 1000.0f)
                        throw std::runtime_error("invalid zig-zag parameters");
                } else if (kind == "offset_path") {
                    decoded_op.kind = PathOperatorKindPlan::OffsetPath;
                    decoded_op.amount = p.at("amount").get<float>();
                    decoded_op.miter_limit = p.value("miter_limit", 4.0f);
                    if (!std::isfinite(decoded_op.amount) || !std::isfinite(decoded_op.miter_limit) ||
                        decoded_op.miter_limit < 1.0f || decoded_op.miter_limit > 1000.0f)
                        throw std::runtime_error("invalid offset-path parameters");
                } else if (kind == "pucker_bloat") {
                    decoded_op.kind = PathOperatorKindPlan::PuckerBloat;
                    decoded_op.amount = p.at("amount").get<float>();
                    if (p.contains("anchor")) {
                        const auto& anchor = p.at("anchor");
                        if (!anchor.is_array() || anchor.size() != 2)
                            throw std::runtime_error("pucker-bloat anchor requires two values");
                        decoded_op.anchor = {anchor[0].get<float>(), anchor[1].get<float>()};
                    }
                    if (!std::isfinite(decoded_op.amount) || decoded_op.amount < -1.0f ||
                        decoded_op.amount > 1.0f || !std::isfinite(decoded_op.anchor[0]) ||
                        !std::isfinite(decoded_op.anchor[1]))
                        throw std::runtime_error("invalid pucker-bloat parameters");
                } else if (kind == "twist") {
                    decoded_op.kind = PathOperatorKindPlan::Twist;
                    decoded_op.degrees = p.at("degrees").get<float>();
                    if (p.contains("center")) {
                        const auto& center = p.at("center");
                        if (!center.is_array() || center.size() != 2)
                            throw std::runtime_error("twist center requires two values");
                        decoded_op.center = {center[0].get<float>(), center[1].get<float>()};
                    }
                    if (!std::isfinite(decoded_op.degrees) || !std::isfinite(decoded_op.center[0]) ||
                        !std::isfinite(decoded_op.center[1]))
                        throw std::runtime_error("invalid twist parameters");
                } else if (kind == "wiggle_path") {
                    decoded_op.kind = PathOperatorKindPlan::WigglePath;
                    decoded_op.amount = p.at("amount").get<float>();
                    decoded_op.frequency = p.value("frequency", 1.0f);
                    decoded_op.seed = p.value("seed", 0u);
                    if (!std::isfinite(decoded_op.amount) || decoded_op.amount < 0.0f ||
                        !std::isfinite(decoded_op.frequency) || decoded_op.frequency <= 0.0f ||
                        decoded_op.frequency > 1000.0f)
                        throw std::runtime_error("invalid wiggle-path parameters");
                } else if (kind == "merge") {
                    decoded_op.kind = PathOperatorKindPlan::Merge;
                    const auto& raw_paths = p.at("paths");
                    if (!raw_paths.is_array() || raw_paths.size() > 1024)
                        throw std::runtime_error("merge paths must contain at most 1024 subpaths");
                    std::size_t total_commands = 0;
                    for (const auto& raw_subpath : raw_paths) {
                        if (!raw_subpath.is_array() || raw_subpath.empty() ||
                            raw_subpath.size() > 65536 || total_commands + raw_subpath.size() > 1000000)
                            throw std::runtime_error("merge subpath has invalid command count");
                        std::vector<ShapePathCommandPlan> subpath;
                        for (const auto& raw_command : raw_subpath) {
                            auto command = decode_shape_path_command(raw_command, "merge path");
                            const auto finite = [](const auto& v) {
                                return std::isfinite(v[0]) && std::isfinite(v[1]);
                            };
                            if ((command.type != ShapePathCommandTypePlan::Close && !finite(command.point)) ||
                                !finite(command.control1) || !finite(command.control2))
                                throw std::runtime_error("merge command coordinates must be finite");
                            subpath.push_back(command);
                        }
                        if (subpath.front().type != ShapePathCommandTypePlan::MoveTo)
                            throw std::runtime_error("each merged subpath must begin with move_to");
                        total_commands += subpath.size();
                        decoded_op.paths.push_back(std::move(subpath));
                    }
                } else if (kind == "morph") {
                    decoded_op.kind = PathOperatorKindPlan::Morph;
                    if (!p.contains("target"))
                        throw std::runtime_error("morph operator requires target commands");
                    const auto& raw_target = p.at("target");
                    if (!raw_target.is_array() || raw_target.empty() || raw_target.size() > 65536)
                        throw std::runtime_error("morph target must contain between 1 and 65536 commands");
                    for (const auto& raw_command : raw_target) {
                        auto command = decode_shape_path_command(raw_command, "morph path");
                        if ((command.type != ShapePathCommandTypePlan::Close &&
                             !finite_point(command.point)) ||
                            !finite_point(command.control1) || !finite_point(command.control2))
                            throw std::runtime_error("morph path coordinates must be finite");
                        decoded_op.morph_target.push_back(command);
                    }
                    std::vector<PathCommand> source_commands;
                    source_commands.reserve(shape.path.size());
                    const auto make_path_command = [](const ShapePathCommandPlan& command) {
                        const Vec2 point{command.point[0], command.point[1]};
                        const Vec2 control1{command.control1[0], command.control1[1]};
                        const Vec2 control2{command.control2[0], command.control2[1]};
                        switch (command.type) {
                            case ShapePathCommandTypePlan::MoveTo: return PathCommand::move_to(point);
                            case ShapePathCommandTypePlan::LineTo: return PathCommand::line_to(point);
                            case ShapePathCommandTypePlan::QuadraticTo: return PathCommand::quadratic_to(control1, point);
                            case ShapePathCommandTypePlan::CubicTo: return PathCommand::cubic_to(control1, control2, point);
                            case ShapePathCommandTypePlan::Close: return PathCommand::close();
                        }
                        return PathCommand::close();
                    };
                    for (const auto& command : shape.path)
                        source_commands.push_back(make_path_command(command));
                    std::vector<PathCommand> target_commands;
                    target_commands.reserve(decoded_op.morph_target.size());
                    for (const auto& command : decoded_op.morph_target)
                        target_commands.push_back(make_path_command(command));
                    if (const auto topology_error = validate_morph_path_topology(
                            source_commands, target_commands))
                        throw std::runtime_error(*topology_error);
                    decoded_op.morph_progress = p.value("progress", 0.0f);
                    if (!std::isfinite(decoded_op.morph_progress) ||
                        decoded_op.morph_progress < 0.0f || decoded_op.morph_progress > 1.0f)
                        throw std::runtime_error("morph progress must be finite and in [0, 1]");
                    if (p.contains("animation")) {
                        auto animation = decode_animation_track(p.at("animation"));
                        animation.property = "morph_progress";
                        for (const auto& key : animation.keyframes) {
                            if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                                key.value.front() < 0.0f || key.value.front() > 1.0f)
                                throw std::runtime_error("morph animation keyframes require scalar progress in [0, 1]");
                        }
                        decoded_op.morph_animation = std::move(animation);
                    }
                } else if (kind == "trim") {
                    decoded_op.kind = PathOperatorKindPlan::Trim;
                    decoded_op.trim_start = p.value("start", 0.0f);
                    decoded_op.trim_end = p.value("end", 1.0f);
                    decoded_op.trim_offset = p.value("offset", 0.0f);
                    if (!std::isfinite(decoded_op.trim_start) || !std::isfinite(decoded_op.trim_end) ||
                        !std::isfinite(decoded_op.trim_offset) ||
                        decoded_op.trim_start < 0.0f || decoded_op.trim_start > 1.0f ||
                        decoded_op.trim_end < 0.0f || decoded_op.trim_end > 1.0f ||
                        decoded_op.trim_end < decoded_op.trim_start ||
                        std::abs(decoded_op.trim_offset) > 1000000.0f)
                        throw std::runtime_error("invalid trim-path parameters");
                    if (p.contains("animation")) {
                        auto animation = decode_animation_track(p.at("animation"));
                        animation.property = "trim";
                        for (const auto& key : animation.keyframes) {
                            if (key.value.empty() || key.value.size() > 3 ||
                                (key.value.size() > 0 &&
                                 (!std::isfinite(key.value[0]) || key.value[0] < 0.0f || key.value[0] > 1.0f)) ||
                                (key.value.size() > 1 &&
                                 (!std::isfinite(key.value[1]) || key.value[1] < 0.0f || key.value[1] > 1.0f)) ||
                                (key.value.size() > 2 && !std::isfinite(key.value[2])))
                                throw std::runtime_error("trim animation keyframes require 1..3 finite values [start, end, offset]");
                        }
                        decoded_op.trim_animation = std::move(animation);
                    }
                } else {
                    throw std::runtime_error("unsupported path operator: " + kind);
                }
                shape.operators.push_back(decoded_op);
            }
        }
        for (const auto& command : shape.path) {
            const bool needs_point = command.type != ShapePathCommandTypePlan::Close;
            if ((needs_point && !finite_point(command.point)) ||
                !finite_point(command.control1) || !finite_point(command.control2))
                throw std::runtime_error("shape path coordinates must be finite");
        }
        if (shape.field && shape.type == ShapeContentTypePlan::Path &&
            std::none_of(shape.path.begin(), shape.path.end(), [](const auto& command) {
                return command.type == ShapePathCommandTypePlan::Close;
            }))
            throw std::runtime_error("Field2D path fills require at least one closed subpath");
        if (raw_shape.contains("fill")) {
            const auto& fill = raw_shape.at("fill");
            if (fill.is_array()) {
                if (fill.size() != 4)
                    throw std::runtime_error("shape.fill must contain RGBA");
                shape.fill_color = std::array<float, 4>{
                    fill[0].get<float>(), fill[1].get<float>(),
                    fill[2].get<float>(), fill[3].get<float>()};
            } else if (fill.is_object()) {
                if (fill.value("type", std::string{}) == "mesh") {
                    for (auto it = fill.begin(); it != fill.end(); ++it)
                        if (it.key() != "type" && it.key() != "mesh")
                            throw std::runtime_error("mesh fill accepts only type and mesh");
                    if (fill.contains("nodes"))
                        throw std::runtime_error("mesh fill nodes must be nested under fill.mesh.nodes");
                    if (!fill.contains("mesh"))
                        throw std::runtime_error("mesh fill requires a mesh object");
                    const bool closed_shape = shape.type == ShapeContentTypePlan::Rect ||
                        shape.type == ShapeContentTypePlan::RoundedRect ||
                        shape.type == ShapeContentTypePlan::Ellipse ||
                        shape.type == ShapeContentTypePlan::Star ||
                        shape.type == ShapeContentTypePlan::Polygon ||
                        shape.type == ShapeContentTypePlan::Arrow ||
                        shape.type == ShapeContentTypePlan::DotGrid;
                    const bool closed_path = shape.type == ShapeContentTypePlan::Path &&
                        !shape.svg_asset && !shape.lottie_asset &&
                        std::any_of(shape.path.begin(), shape.path.end(), [](const auto& command) {
                            return command.type == ShapePathCommandTypePlan::Close;
                        });
                    if (!closed_shape && !closed_path)
                        throw std::runtime_error("mesh fills require a closed filled shape or closed authored path");
                    shape.gradient_mesh = decode_gradient_mesh(fill.at("mesh"));
                } else if (fill.value("type", std::string{}) == "field") {
                    for (auto it = fill.begin(); it != fill.end(); ++it)
                        if (it.key() != "type" && it.key() != "field" && it.key() != "field_ramp" &&
                            it.key() != "field_render_scale" && it.key() != "field_drift")
                            throw std::runtime_error("field fill accepts only field, ramp, render scale, and drift");
                    if (!fill.contains("field"))
                        throw std::runtime_error("field fill requires a field object");
                    if (shape.type == ShapeContentTypePlan::Line ||
                        shape.type == ShapeContentTypePlan::Arc ||
                        shape.type == ShapeContentTypePlan::Grid ||
                        shape.type == ShapeContentTypePlan::DotGrid ||
                        shape.type == ShapeContentTypePlan::DeviceFrame ||
                        raw_shape.contains("asset") || raw_shape.contains("lottie"))
                        throw std::runtime_error("field fill requires a closed procedural shape or authored path");
                    if (fill.contains("mesh"))
                        throw std::runtime_error("field fill cannot contain a mesh payload");
                    shape.field = decode_field2d_plan(fill.at("field"));
                    if (fill.contains("field_ramp"))
                        shape.field_ramp = decode_gradient_definition(fill.at("field_ramp"));
                    if (fill.contains("field_render_scale")) {
                        shape.field_render_scale = fill.at("field_render_scale").get<std::uint32_t>();
                        if (shape.field_render_scale != 1 && shape.field_render_scale != 2 &&
                            shape.field_render_scale != 4)
                            throw std::runtime_error("field fill render scale must be 1, 2, or 4");
                    }
                    if (fill.contains("field_drift")) {
                        const auto& drift = fill.at("field_drift");
                        if (!drift.is_array() || drift.size() != 2)
                            throw std::runtime_error("field fill drift must contain two values");
                        const Vec2 decoded{drift[0].get<float>(), drift[1].get<float>()};
                        if (!std::isfinite(decoded.x) || !std::isfinite(decoded.y) ||
                            std::abs(decoded.x) > 100.f || std::abs(decoded.y) > 100.f)
                            throw std::runtime_error("field fill drift components must be finite and within [-100, 100]");
                        shape.field_drift = decoded;
                    }
                } else {
                    shape.gradient = decode_gradient_definition(fill);
                }
            }
        }
        if (shape.gradient && (shape.field || shape.gradient_mesh))
            throw std::runtime_error("gradient fill cannot be combined with Field2D or Gradient Mesh");
        if (shape.gradient_mesh && shape.field)
            throw std::runtime_error("Gradient Mesh cannot be combined with Field2D");
        if (shape.type == ShapeContentTypePlan::DotGrid && !shape.fill_color)
            throw std::runtime_error("dot_grid shapes require a solid RGBA fill");
        if (shape.gradient) {
            const auto valid_stop = [](float position, float alpha) {
                return std::isfinite(position) && position >= 0.0f && position <= 1.0f &&
                       std::isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f;
            };
            if (shape.gradient->color_stops.size() < 2)
                throw std::runtime_error("gradient requires at least two color stops");
            for (const auto& stop : shape.gradient->color_stops) {
                if (!valid_stop(stop.position, stop.color.a) ||
                    !std::isfinite(stop.color.r) || !std::isfinite(stop.color.g) ||
                    !std::isfinite(stop.color.b) || stop.color.r < 0.0f || stop.color.r > 1.0f ||
                    stop.color.g < 0.0f || stop.color.g > 1.0f ||
                    stop.color.b < 0.0f || stop.color.b > 1.0f)
                    throw std::runtime_error("gradient color stops must be finite and in [0, 1]");
            }
            for (const auto& stop : shape.gradient->opacity_stops) {
                if (!valid_stop(stop.position, stop.opacity))
                    throw std::runtime_error("gradient opacity stops must be finite and in [0, 1]");
            }
            const auto& gradient = *shape.gradient;
            if (!finite_vec2(gradient.start) || !finite_vec2(gradient.end) ||
                !finite_vec2(gradient.center) || !std::isfinite(gradient.radius) ||
                gradient.radius <= 0.0f || !std::isfinite(gradient.angle))
                throw std::runtime_error("gradient geometry must be finite with positive radius");
        }
        if (raw_shape.contains("stroke")) {
            const auto& stroke = raw_shape.at("stroke");
            StrokeStyle decoded_stroke;
            decoded_stroke.color = "#FFFFFF";
            if (stroke.contains("color")) {
                const auto& stroke_color = stroke.at("color");
                if (stroke_color.is_array()) {
                    if (stroke_color.size() != 4)
                        throw std::runtime_error("shape.stroke.color must contain RGBA");
                    decoded_stroke.solid_color = std::array<float, 4>{
                        stroke_color[0].get<float>(), stroke_color[1].get<float>(),
                        stroke_color[2].get<float>(), stroke_color[3].get<float>()};
                } else if (stroke_color.is_string()) {
                    decoded_stroke.color = stroke_color.get<std::string>();
                } else if (stroke_color.is_object()) {
                    decoded_stroke.gradient = decode_gradient_definition(stroke_color);
                }
            }
            if (stroke.contains("width")) decoded_stroke.width = stroke.at("width").get<float>();
            if (stroke.contains("trim_start")) decoded_stroke.trim_start = stroke.at("trim_start").get<float>();
            if (stroke.contains("trim_end")) decoded_stroke.trim_end = stroke.at("trim_end").get<float>();
            if (stroke.contains("dash_array")) {
                const auto& dash = stroke.at("dash_array");
                if (!dash.is_array()) throw std::runtime_error("stroke.dash_array must be an array");
                for (const auto& entry : dash) {
                    const float value = entry.get<float>();
                    if (!std::isfinite(value) || value < 0.0f)
                        throw std::runtime_error("stroke.dash_array entries must be finite and >= 0");
                    decoded_stroke.dash_array.push_back(value);
                }
            }
            if (stroke.contains("dash_offset")) {
                decoded_stroke.dash_offset = stroke.at("dash_offset").get<float>();
                if (!std::isfinite(decoded_stroke.dash_offset))
                    throw std::runtime_error("stroke.dash_offset must be finite");
            }
            decoded_stroke.cap = stroke.value("cap", decoded_stroke.cap);
            decoded_stroke.join = stroke.value("join", decoded_stroke.join);
            if (stroke.contains("brush")) {
                if (shape.type != ShapeContentTypePlan::Line &&
                    shape.type != ShapeContentTypePlan::Path &&
                    shape.type != ShapeContentTypePlan::Star &&
                    shape.type != ShapeContentTypePlan::Polygon &&
                    shape.type != ShapeContentTypePlan::Arrow &&
                    shape.type != ShapeContentTypePlan::Arc &&
                    shape.type != ShapeContentTypePlan::Grid &&
                    shape.type != ShapeContentTypePlan::DotGrid)
                    throw std::runtime_error("brush strokes require a path-backed shape");
                if (shape.svg_asset || shape.lottie_asset)
                    throw std::runtime_error("brush strokes are not supported on imported asset paths");
                const auto& raw_brush = stroke.at("brush");
                graphics::BrushProfile brush;
                brush.size = raw_brush.value("size", 0.0f);
                const bool has_brush_size = raw_brush.contains("size") && brush.size > 0.0f;
                brush.hardness = raw_brush.value("hardness", brush.hardness);
                brush.opacity = raw_brush.value("opacity", brush.opacity);
                brush.flow = raw_brush.value("flow", brush.flow);
                brush.opacity_start = raw_brush.value("opacity_start", brush.opacity_start);
                brush.opacity_end = raw_brush.value("opacity_end", brush.opacity_end);
                brush.bleed_radius = raw_brush.value("bleed_radius", brush.bleed_radius);
                brush.edge_density = raw_brush.value("edge_density", brush.edge_density);
                brush.spacing = raw_brush.value("spacing", brush.spacing);
                brush.roundness = raw_brush.value("roundness", brush.roundness);
                brush.angle = raw_brush.value("angle", brush.angle);
                brush.taper_start = raw_brush.value("taper_start", brush.taper_start);
                brush.taper_end = raw_brush.value("taper_end", brush.taper_end);
                brush.pressure_width = raw_brush.value("pressure_width", brush.pressure_width);
                brush.pressure_opacity = raw_brush.value("pressure_opacity", brush.pressure_opacity);
                brush.width_jitter = raw_brush.value("width_jitter", brush.width_jitter);
                brush.opacity_jitter = raw_brush.value("opacity_jitter", brush.opacity_jitter);
                brush.angle_jitter = raw_brush.value("angle_jitter", brush.angle_jitter);
                brush.scatter = raw_brush.value("scatter", brush.scatter);
                brush.grain_strength = raw_brush.value("grain", brush.grain_strength);
                brush.grain_scale = raw_brush.value("grain_scale", brush.grain_scale);
                brush.edge_noise = raw_brush.value("edge_noise", brush.edge_noise);
                brush.edge_softness = raw_brush.value("edge_softness", brush.edge_softness);
                brush.seed = raw_brush.value("seed", brush.seed);
                if (raw_brush.contains("texture"))
                    brush.texture = decode_field2d_plan(raw_brush.at("texture"));
                if (raw_brush.contains("along_path_gradient")) {
                    brush.along_path_gradient = decode_gradient_definition(raw_brush.at("along_path_gradient"));
                    const auto& gradient = *brush.along_path_gradient;
                    if (gradient.type != graphics::GradientType::Linear ||
                        gradient.color_stops.size() < 2 ||
                        std::any_of(gradient.color_stops.begin(), gradient.color_stops.end(), [](const auto& stop) {
                            return !std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                                !std::isfinite(stop.color.r) || !std::isfinite(stop.color.g) ||
                                !std::isfinite(stop.color.b) || !std::isfinite(stop.color.a) ||
                                stop.color.r < 0.0f || stop.color.r > 1.0f || stop.color.g < 0.0f || stop.color.g > 1.0f ||
                                stop.color.b < 0.0f || stop.color.b > 1.0f || stop.color.a < 0.0f || stop.color.a > 1.0f;
                        }) ||
                        std::any_of(gradient.opacity_stops.begin(), gradient.opacity_stops.end(), [](const auto& stop) {
                            return !std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                                !std::isfinite(stop.opacity) || stop.opacity < 0.0f || stop.opacity > 1.0f;
                        }))
                        throw std::runtime_error("brush along-path gradient stops must be finite and in [0, 1]");
                }
                if (raw_brush.contains("pressure_curve")) {
                    const auto& curve = raw_brush.at("pressure_curve");
                    if (!curve.is_array() || curve.size() > 256)
                        throw std::runtime_error("stroke.brush.pressure_curve must contain at most 256 points");
                    for (const auto& point : curve) {
                        if (!point.is_array() || point.size() != 2)
                            throw std::runtime_error("stroke.brush.pressure_curve points must be [position, pressure]");
                        brush.pressure_curve.push_back({point[0].get<float>(), point[1].get<float>()});
                    }
                }
                if (!brush.valid())
                    throw std::runtime_error("shape.stroke.brush parameters are outside the supported range");
                if (!has_brush_size && (!decoded_stroke.width ||
                    !std::isfinite(*decoded_stroke.width) || *decoded_stroke.width <= 0.0f))
                    throw std::runtime_error("brush stroke requires an explicit positive width when brush.size is zero");
                decoded_stroke.brush = std::move(brush);
            }
            decoded_stroke.taper_start = stroke.value("taper_start", 1.0f);
            decoded_stroke.taper_end = stroke.value("taper_end", 1.0f);
            if (decoded_stroke.brush && (stroke.contains("taper_start") || stroke.contains("taper_end")))
                throw std::runtime_error("brush taper must be authored in stroke.brush");
            if ((decoded_stroke.cap != "butt" && decoded_stroke.cap != "round" &&
                 decoded_stroke.cap != "square") ||
                (decoded_stroke.join != "miter" && decoded_stroke.join != "round" &&
                 decoded_stroke.join != "bevel") ||
                !std::isfinite(decoded_stroke.taper_start) || decoded_stroke.taper_start < 0.0f ||
                decoded_stroke.taper_start > 1.0f || !std::isfinite(decoded_stroke.taper_end) ||
                decoded_stroke.taper_end < 0.0f || decoded_stroke.taper_end > 1.0f)
                throw std::runtime_error("shape.stroke cap, join, or taper is outside the supported range");
            if (stroke.contains("gradient"))
                decoded_stroke.gradient = decode_gradient_definition(stroke.at("gradient"));
            if (decoded_stroke.brush && stroke.contains("color") && stroke.at("color").is_object())
                throw std::runtime_error("brush stroke color must be a solid color; use brush.along_path_gradient for a color ramp");
            const bool closed_stroke_path =
                shape.type == ShapeContentTypePlan::Star ||
                shape.type == ShapeContentTypePlan::Polygon ||
                shape.type == ShapeContentTypePlan::Arrow ||
                shape.type == ShapeContentTypePlan::DotGrid ||
                ((shape.type == ShapeContentTypePlan::Line ||
                  shape.type == ShapeContentTypePlan::Path) &&
                 std::any_of(shape.path.begin(), shape.path.end(), [](const auto& command) {
                    return command.type == ShapePathCommandTypePlan::Close;
                }));
            if (closed_stroke_path &&
                ((decoded_stroke.brush &&
                  (decoded_stroke.brush->taper_start != 1.0f ||
                   decoded_stroke.brush->taper_end != 1.0f)) ||
                 (!decoded_stroke.brush &&
                  (decoded_stroke.taper_start != 1.0f ||
                   decoded_stroke.taper_end != 1.0f))))
                throw std::runtime_error("tapered strokes require an open path");
            if (decoded_stroke.brush && decoded_stroke.gradient)
                throw std::runtime_error("brush strokes use brush.along_path_gradient or brush opacity, not a competing stroke gradient");
            if (decoded_stroke.gradient) {
                const auto& gradient = *decoded_stroke.gradient;
                if (gradient.color_stops.size() < 2)
                    throw std::runtime_error("stroke gradient requires at least two color stops");
                for (const auto& stop : gradient.color_stops) {
                    if (!std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                        !std::isfinite(stop.color.r) || !std::isfinite(stop.color.g) ||
                        !std::isfinite(stop.color.b) || !std::isfinite(stop.color.a) ||
                        stop.color.r < 0.0f || stop.color.r > 1.0f ||
                        stop.color.g < 0.0f || stop.color.g > 1.0f ||
                        stop.color.b < 0.0f || stop.color.b > 1.0f ||
                        stop.color.a < 0.0f || stop.color.a > 1.0f)
                        throw std::runtime_error("stroke gradient color stops must be finite and in [0, 1]");
                }
                for (const auto& stop : gradient.opacity_stops) {
                    if (!std::isfinite(stop.position) || stop.position < 0.0f || stop.position > 1.0f ||
                        !std::isfinite(stop.opacity) || stop.opacity < 0.0f || stop.opacity > 1.0f)
                        throw std::runtime_error("stroke gradient opacity stops must be finite and in [0, 1]");
                }
                if (!finite_vec2(gradient.start) || !finite_vec2(gradient.end) ||
                    !finite_vec2(gradient.center) || !std::isfinite(gradient.radius) ||
                    gradient.radius <= 0.0f || !std::isfinite(gradient.angle))
                    throw std::runtime_error("stroke gradient geometry must be finite with positive radius");
            }
            shape.stroke = std::move(decoded_stroke);
        }
        if (shape.fill_color) {
            for (const auto component : *shape.fill_color) {
                if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
                    throw std::runtime_error("shape fill RGBA must be finite and in [0, 1]");
            }
            // Normalize both names to the same solid paint value for ordinary
            // shapes. Paths keep layer.color independent: it modulates stroke
            // opacity while shape.fill_color controls only the enclosed fill.
            if (shape.type != ShapeContentTypePlan::Path)
                layer.color = *shape.fill_color;
        }
        if (shape.stroke) {
            if (shape.stroke->color.empty() && shape.stroke->solid_color)
                shape.stroke->color = "#FFFFFF";
            if (shape.stroke->solid_color && shape.stroke->gradient)
                throw std::runtime_error("shape stroke cannot specify both solid color and gradient");
            if (shape.stroke->color.empty() && shape.stroke->gradient)
                shape.stroke->color = "#FFFFFF";
            if (shape.stroke->width &&
                (!std::isfinite(*shape.stroke->width) || *shape.stroke->width < 0.0f))
                throw std::runtime_error("shape stroke width must be finite and non-negative");
            if (shape.stroke->trim_start &&
                (!std::isfinite(*shape.stroke->trim_start) || *shape.stroke->trim_start < 0.0f ||
                 *shape.stroke->trim_start > 1.0f))
                throw std::runtime_error("shape stroke trim_start must be finite and in [0, 1]");
            if (shape.stroke->trim_end &&
                (!std::isfinite(*shape.stroke->trim_end) || *shape.stroke->trim_end < 0.0f ||
                 *shape.stroke->trim_end > 1.0f))
                throw std::runtime_error("shape stroke trim_end must be finite and in [0, 1]");
            if (shape.stroke->solid_color) {
                for (const auto component : *shape.stroke->solid_color) {
                    if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
                        throw std::runtime_error("shape stroke RGBA must be finite and in [0, 1]");
                }
            }
        }
        layer.shape = std::move(shape);
    }
    if (value.contains("fill_gradient_animation")) {
        if (layer.animation && std::any_of(layer.animation->tracks.begin(),
                layer.animation->tracks.end(), [](const AnimationTrackPlan& track) {
                    return track.property == "fill_color";
                }))
            throw std::runtime_error(
                "fill_color and fill_gradient_animation tracks are mutually exclusive");
        if (layer.type != LayerType::Shape || !layer.shape ||
            layer.shape->type == ShapeContentTypePlan::Line ||
            layer.shape->type == ShapeContentTypePlan::Path ||
            layer.shape->type == ShapeContentTypePlan::Arc ||
            layer.shape->type == ShapeContentTypePlan::Grid ||
            !layer.shape->gradient) {
            throw std::runtime_error(
                "fill_gradient_animation requires a filled rect, rounded_rect, or ellipse shape");
        }
        auto animation = decode_gradient_animation(value.at("fill_gradient_animation"));
        if (animation.keyframes.empty())
            throw std::runtime_error("fill_gradient_animation requires keyframes");
        const auto& base = *layer.shape->gradient;
        const auto compatible = [&](const graphics::GradientDefinition& candidate) {
            return candidate.type == base.type && candidate.spread == base.spread;
        };
        for (const auto& key : animation.keyframes) {
            if (key.frame.integral() < 0 || key.frame.integral() > 1000000 ||
                !compatible(key.value))
                throw std::runtime_error(
                    "fill gradient keyframes must use the base gradient type and spread");
        }
        std::stable_sort(animation.keyframes.begin(), animation.keyframes.end(),
            [](const auto& a, const auto& b) { return a.frame < b.frame; });
        for (std::size_t index = 1; index < animation.keyframes.size(); ++index)
            if (animation.keyframes[index - 1].frame == animation.keyframes[index].frame)
                throw std::runtime_error("fill gradient keyframe frames must be unique");
        layer.fill_gradient_animation = std::move(animation);
    }
}

}  // namespace chronon3d::render_plan::detail
