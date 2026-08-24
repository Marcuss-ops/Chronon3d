#include "svg_importer.hpp"

#include <svgpp/parser/external_function/parse_path_data.hpp>
#include <svgpp/utility/arc_endpoint_to_center.hpp>
#include <svgpp/utility/arc_to_bezier.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace chronon3d::assets {
namespace {

struct PathAdapter final : svgpp::detail::path_events_interface<double> {
    PathShape path;
    bool relative_commands_seen{false};
    Vec2 current{0.0f, 0.0f};
    Vec2 subpath_start{0.0f, 0.0f};
    Vec2 last_cubic_control{0.0f, 0.0f};
    Vec2 last_quadratic_control{0.0f, 0.0f};
    bool has_cubic_control{false};
    bool has_quadratic_control{false};

    void reset_curve_state() {
        has_cubic_control = false;
        has_quadratic_control = false;
    }

    void move_to(Vec2 p) {
        path.commands.push_back(PathCommand::move_to(p));
        current = p;
        subpath_start = p;
        reset_curve_state();
    }

    void line_to(Vec2 p) {
        path.commands.push_back(PathCommand::line_to(p));
        current = p;
        reset_curve_state();
    }

    void cubic_to(Vec2 cp1, Vec2 cp2, Vec2 p) {
        path.commands.push_back(PathCommand::cubic_to(cp1, cp2, p));
        current = p;
        last_cubic_control = cp2;
        has_cubic_control = true;
        has_quadratic_control = false;
    }

    void quadratic_to(Vec2 cp, Vec2 p) {
        path.commands.push_back(PathCommand::quadratic_to(cp, p));
        current = p;
        last_quadratic_control = cp;
        has_quadratic_control = true;
        has_cubic_control = false;
    }

    void append_arc(double rx, double ry, double rotation,
                    bool large_arc, bool sweep, Vec2 endpoint) {
        if (rx == 0.0 || ry == 0.0 || current == endpoint) {
            line_to(endpoint);
            return;
        }

        constexpr double kPi = 3.14159265358979323846;
        const double phi = rotation * kPi / 180.0;
        double cx = 0.0;
        double cy = 0.0;
        double theta1 = 0.0;
        double theta2 = 0.0;
        double adjusted_rx = std::abs(rx);
        double adjusted_ry = std::abs(ry);
        svgpp::arc_endpoint_to_center(
            static_cast<double>(current.x), static_cast<double>(current.y),
            static_cast<double>(endpoint.x), static_cast<double>(endpoint.y),
            adjusted_rx, adjusted_ry, phi, large_arc, sweep,
            cx, cy, theta1, theta2);

        if (sweep) {
            if (theta2 < theta1) theta2 += 2.0 * kPi;
        } else if (theta2 > theta1) {
            theta2 -= 2.0 * kPi;
        }

        using Arc = svgpp::arc_to_bezier<double>;
        Arc arc(cx, cy, adjusted_rx, adjusted_ry, phi,
                Arc::circle_angle_tag(), theta1, theta2,
                Arc::max_angle_tag(), kPi / 2.0);
        for (typename Arc::iterator it(arc); !it.eof(); it.advance()) {
            cubic_to(
                Vec2{static_cast<float>(it.p1x()), static_cast<float>(it.p1y())},
                Vec2{static_cast<float>(it.p2x()), static_cast<float>(it.p2y())},
                Vec2{static_cast<float>(it.p3x()), static_cast<float>(it.p3y())});
        }
        current = endpoint;
        reset_curve_state();
    }

    void path_move_to(double x, double y, svgpp::tag::coordinate::absolute) override { move_to({static_cast<float>(x), static_cast<float>(y)}); }
    void path_move_to(double x, double y, svgpp::tag::coordinate::relative) override { relative_commands_seen = true; move_to({current.x + static_cast<float>(x), current.y + static_cast<float>(y)}); }
    void path_line_to(double x, double y, svgpp::tag::coordinate::absolute) override { line_to({static_cast<float>(x), static_cast<float>(y)}); }
    void path_line_to(double x, double y, svgpp::tag::coordinate::relative) override { relative_commands_seen = true; line_to({current.x + static_cast<float>(x), current.y + static_cast<float>(y)}); }
    void path_line_to_ortho(double coord, bool horizontal, svgpp::tag::coordinate::absolute) override { line_to(horizontal ? Vec2{static_cast<float>(coord), current.y} : Vec2{current.x, static_cast<float>(coord)}); }
    void path_line_to_ortho(double coord, bool horizontal, svgpp::tag::coordinate::relative) override { relative_commands_seen = true; line_to(horizontal ? Vec2{current.x + static_cast<float>(coord), current.y} : Vec2{current.x, current.y + static_cast<float>(coord)}); }

    void path_cubic_bezier_to(double x1, double y1, double x2, double y2, double x, double y, svgpp::tag::coordinate::absolute) override { cubic_to({static_cast<float>(x1), static_cast<float>(y1)}, {static_cast<float>(x2), static_cast<float>(y2)}, {static_cast<float>(x), static_cast<float>(y)}); }
    void path_cubic_bezier_to(double x1, double y1, double x2, double y2, double x, double y, svgpp::tag::coordinate::relative) override {        relative_commands_seen = true; const Vec2 o = current; cubic_to({o.x + static_cast<float>(x1), o.y + static_cast<float>(y1)}, {o.x + static_cast<float>(x2), o.y + static_cast<float>(y2)}, {o.x + static_cast<float>(x), o.y + static_cast<float>(y)}); }
    void path_cubic_bezier_to(double x2, double y2, double x, double y, svgpp::tag::coordinate::absolute) override { const Vec2 cp = has_cubic_control ? Vec2{2.0f * current.x - last_cubic_control.x, 2.0f * current.y - last_cubic_control.y} : current; cubic_to(cp, {static_cast<float>(x2), static_cast<float>(y2)}, {static_cast<float>(x), static_cast<float>(y)}); }
    void path_cubic_bezier_to(double x2, double y2, double x, double y, svgpp::tag::coordinate::relative) override {        relative_commands_seen = true; const Vec2 o = current; const Vec2 cp = has_cubic_control ? Vec2{2.0f * o.x - last_cubic_control.x, 2.0f * o.y - last_cubic_control.y} : o; cubic_to(cp, {o.x + static_cast<float>(x2), o.y + static_cast<float>(y2)}, {o.x + static_cast<float>(x), o.y + static_cast<float>(y)}); }
    void path_quadratic_bezier_to(double x1, double y1, double x, double y, svgpp::tag::coordinate::absolute) override { quadratic_to({static_cast<float>(x1), static_cast<float>(y1)}, {static_cast<float>(x), static_cast<float>(y)}); }
    void path_quadratic_bezier_to(double x1, double y1, double x, double y, svgpp::tag::coordinate::relative) override {        relative_commands_seen = true; const Vec2 o = current; quadratic_to({o.x + static_cast<float>(x1), o.y + static_cast<float>(y1)}, {o.x + static_cast<float>(x), o.y + static_cast<float>(y)}); }
    void path_quadratic_bezier_to(double x, double y, svgpp::tag::coordinate::absolute) override { const Vec2 cp = has_quadratic_control ? Vec2{2.0f * current.x - last_quadratic_control.x, 2.0f * current.y - last_quadratic_control.y} : current; quadratic_to(cp, {static_cast<float>(x), static_cast<float>(y)}); }
    void path_quadratic_bezier_to(double x, double y, svgpp::tag::coordinate::relative) override {        relative_commands_seen = true; const Vec2 o = current; const Vec2 cp = has_quadratic_control ? Vec2{2.0f * o.x - last_quadratic_control.x, 2.0f * o.y - last_quadratic_control.y} : o; quadratic_to(cp, {o.x + static_cast<float>(x), o.y + static_cast<float>(y)}); }
    void path_elliptical_arc_to(double rx, double ry, double rotation, bool large_arc, bool sweep, double x, double y, svgpp::tag::coordinate::absolute) override { append_arc(rx, ry, rotation, large_arc, sweep, {static_cast<float>(x), static_cast<float>(y)}); }
    void path_elliptical_arc_to(double rx, double ry, double rotation, bool large_arc, bool sweep, double x, double y, svgpp::tag::coordinate::relative) override { relative_commands_seen = true; append_arc(rx, ry, rotation, large_arc, sweep, {current.x + static_cast<float>(x), current.y + static_cast<float>(y)}); }
    void path_close_subpath() override { path.commands.push_back(PathCommand::close()); current = subpath_start; path.closed = true; reset_curve_state(); }
    void path_exit() override {}
};

bool only_space_remaining(std::string::const_iterator it, std::string::const_iterator end) {
    return std::all_of(it, end, [](unsigned char c) { return std::isspace(c) != 0; });
}

} // namespace

SvgPathLoadResult SvgImporter::import_path_data(std::string_view d, SvgPathLoadOptions options) const {
    std::string input(d);
    auto it = input.begin();
    PathAdapter adapter;
    try {
        const bool parsed = svgpp::detail::parse_path_data<
            std::string::iterator, double>(it, input.end(), adapter);
        if (!parsed || !only_space_remaining(it, input.end())) {
            return {.path = {}, .ok = false, .error = "Invalid SVG path data"};
        }
        if (!options.support_relative_commands && adapter.relative_commands_seen) {
            return {.path = {}, .ok = false, .error = "Relative SVG path commands are disabled"};
        }
        return {.path = std::move(adapter.path), .ok = true, .error = {}};
    } catch (const std::exception& e) {
        return {.path = {}, .ok = false, .error = e.what()};
    }
}

} // namespace chronon3d::assets
