#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/shape_path_generator.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {

using namespace std::string_view_literals;

ShapeContentTypePlan decode_shape_type(std::string_view name) {
    using Shape = ShapeContentTypePlan;
    static constexpr std::array types{
        std::pair{"rect"sv, Shape::Rect},
        std::pair{"rounded_rect"sv, Shape::RoundedRect},
        std::pair{"ellipse"sv, Shape::Ellipse},
        std::pair{"line"sv, Shape::Line},
        std::pair{"path"sv, Shape::Path},
        std::pair{"star"sv, Shape::Star},
        std::pair{"polygon"sv, Shape::Polygon},
        std::pair{"arrow"sv, Shape::Arrow},
        std::pair{"arc"sv, Shape::Arc},
        std::pair{"grid"sv, Shape::Grid},
        std::pair{"dot_grid"sv, Shape::DotGrid},
        std::pair{"dotgrid"sv, Shape::DotGrid},
        std::pair{"device_frame"sv, Shape::DeviceFrame},
        std::pair{"ring_segment"sv, Shape::RingSegment},
        std::pair{"monotone_line"sv, Shape::MonotoneLine},
        std::pair{"monotone_chart"sv, Shape::MonotoneChart},
    };
    for (const auto& [name_value, type] : types)
        if (name == name_value) return type;
    throw std::runtime_error("unsupported shape.type: " + std::string(name));
}

}  // namespace

void decode_shape_and_fill_gradient(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("shape")) {
        const auto& raw_shape = value.at("shape");
        ShapeContentPlan shape;
        shape.type = decode_shape_type(raw_shape.at("type").get<std::string>());
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
        if (shape.type == ShapeContentTypePlan::MonotoneLine && !raw_shape.contains("stroke"))
            throw std::runtime_error("monotone_line shapes require a stroke");
        if (shape.type == ShapeContentTypePlan::MonotoneLine && raw_shape.contains("fill"))
            throw std::runtime_error("monotone_line is an open stroked path and does not accept fill");
        if (shape.type != ShapeContentTypePlan::MonotoneLine &&
            shape.type != ShapeContentTypePlan::MonotoneChart && raw_shape.contains("data_points"))
            throw std::runtime_error("shape.data_points is only valid for monotone_line or monotone_chart");
        if (shape.type == ShapeContentTypePlan::MonotoneLine &&
            (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie")))
            throw std::runtime_error("monotone_line data cannot be combined with authored path or asset data");
        if ((shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon ||
             shape.type == ShapeContentTypePlan::Arrow || shape.type == ShapeContentTypePlan::Arc) &&
            (raw_shape.contains("path") || raw_shape.contains("asset") || raw_shape.contains("lottie")))
            throw std::runtime_error("procedural shapes cannot be combined with authored path or asset data");
        const bool procedural = shape.type == ShapeContentTypePlan::Star ||
            shape.type == ShapeContentTypePlan::Polygon || shape.type == ShapeContentTypePlan::Arrow ||
            shape.type == ShapeContentTypePlan::Arc || shape.type == ShapeContentTypePlan::Grid ||
            shape.type == ShapeContentTypePlan::DotGrid || shape.type == ShapeContentTypePlan::DeviceFrame ||
            shape.type == ShapeContentTypePlan::RingSegment ||
            shape.type == ShapeContentTypePlan::MonotoneLine ||
            shape.type == ShapeContentTypePlan::MonotoneChart;
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
        if (shape.type == ShapeContentTypePlan::RingSegment &&
            (raw_shape.contains("points") || raw_shape.contains("rotation_degrees") ||
             raw_shape.contains("shaft_ratio") || raw_shape.contains("head_ratio") ||
             raw_shape.contains("spacing") || raw_shape.contains("dot_radius")))
            throw std::runtime_error("ring_segment received parameters for another procedural shape");
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
        } else if (shape.type == ShapeContentTypePlan::RingSegment) {
            shape.inner_radius_ratio = raw_shape.value("inner_radius_ratio", 0.0f);
            shape.arc_start_deg = raw_shape.value("start_degrees", 0.0f);
            shape.arc_sweep_deg = raw_shape.value("sweep_degrees", 90.0f);
            shape.path = make_ring_segment_path(layer.size[0], layer.size[1],
                shape.arc_start_deg, shape.arc_sweep_deg, shape.inner_radius_ratio,
                raw_shape.value("segments", 64u));
        } else if (shape.type == ShapeContentTypePlan::MonotoneLine) {
            const auto& raw_points = raw_shape.at("data_points");
            if (!raw_points.is_array() || raw_points.size() < 2 || raw_points.size() > 4096)
                throw std::runtime_error("monotone_line data_points must contain 2..4096 [x,y] pairs");
            std::vector<std::array<float, 2>> points;
            points.reserve(raw_points.size());
            for (const auto& raw_point : raw_points) {
                if (!raw_point.is_array() || raw_point.size() != 2 ||
                    !raw_point[0].is_number() || !raw_point[1].is_number())
                    throw std::runtime_error("monotone_line data_points entries must be [x,y] number pairs");
                points.push_back({raw_point[0].get<float>(), raw_point[1].get<float>()});
            }
            shape.path = make_monotone_cubic_path(points);
        } else if (shape.type == ShapeContentTypePlan::MonotoneChart) {
            decode_monotone_chart(layer, shape, raw_shape);
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
        if (raw_shape.contains("path")) {
            for (const auto& raw_command : raw_shape.at("path")) {
                shape.path.push_back(decode_shape_path_command(raw_command, "shape path"));
            }
        }
        if ((shape.type == ShapeContentTypePlan::Line ||
             shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon ||
             shape.type == ShapeContentTypePlan::Arrow || shape.type == ShapeContentTypePlan::Arc ||
             shape.type == ShapeContentTypePlan::Grid || shape.type == ShapeContentTypePlan::DotGrid ||
             shape.type == ShapeContentTypePlan::RingSegment ||
             shape.type == ShapeContentTypePlan::MonotoneLine ||
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
        if (raw_shape.contains("element_animations")) {
            if (shape.type != ShapeContentTypePlan::Path || !shape.svg_asset)
                throw std::runtime_error(
                    "shape.element_animations requires an imported SVG asset");
            std::set<std::pair<std::string, std::string>> bindings;
            for (const auto& raw_binding : raw_shape.at("element_animations")) {
                const auto target = raw_binding.at("target").get<std::string>();
                const auto property = raw_binding.at("property").get<std::string>();
                if (target.empty() || target.size() > 128 ||
                    !bindings.emplace(target, property).second)
                    throw std::runtime_error(
                        "SVG element animation targets must be unique and bounded");
                if (property != "opacity" && property != "transform.translate" &&
                    property != "transform.scale" && property != "transform.rotation" &&
                    property != "fill.color" && property != "fill.opacity" &&
                    property != "stroke.color" && property != "stroke.opacity" &&
                    property != "stroke.width")
                    throw std::runtime_error(
                        "unsupported SVG element animation property: " + property);
                auto animation = decode_animation_track(raw_binding.at("animation"));
                animation.property = property;
                const auto expected_components = property == "transform.translate" ? 2u :
                    (property == "fill.color" || property == "stroke.color" ? 4u : 1u);
                std::int64_t previous_frame = -1;
                for (const auto& key : animation.keyframes) {
                    if (key.frame.integral() <= previous_frame ||
                        key.value.size() != expected_components ||
                        std::any_of(key.value.begin(), key.value.end(),
                            [](float value) { return !std::isfinite(value); }))
                        throw std::runtime_error(
                            "SVG element animation keyframes must have increasing frames and finite values with the property's component count");
                    previous_frame = key.frame.integral();
                    if ((property == "opacity" || property == "fill.opacity" ||
                         property == "stroke.opacity") &&
                        (key.value[0] < 0.0f || key.value[0] > 1.0f))
                        throw std::runtime_error(
                            "SVG opacity animation values must be in [0,1]");
                    if ((property == "fill.color" || property == "stroke.color") &&
                        std::any_of(key.value.begin(), key.value.end(),
                            [](float value) { return value < 0.0f || value > 1.0f; }))
                        throw std::runtime_error(
                            "SVG color animation components must be in [0,1]");
                    if (property == "stroke.width" && key.value[0] < 0.0f)
                        throw std::runtime_error(
                            "SVG stroke width animation must be non-negative");
                }
                shape.svg_element_animations.push_back(
                    SvgElementAnimationPlan{target, property, std::move(animation)});
            }
        }
        decode_shape_path_operators(shape, raw_shape);
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
        // Shape-local fill is canonical; shape-local fill wins over the legacy layer.color alias.
        decode_shape_fill_and_stroke(layer, shape, raw_shape);
        layer.shape = std::move(shape);
    }
    decode_shape_fill_gradient_animation(layer, value);
}

}  // namespace chronon3d::render_plan::detail
