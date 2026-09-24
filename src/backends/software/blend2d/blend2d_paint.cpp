// ──────────────────────────────────────────────────────────────────────
// blend2d_paint.cpp — implementations of build_gradient_stops and
// build_bl_gradient.  See blend2d_paint.hpp for the rationale.
// ──────────────────────────────────────────────────────────────────────

#include "blend2d_paint.hpp"

#include <cmath>

namespace chronon3d::blend2d_bridge::paint {

std::vector<BLGradientStop> build_gradient_stops(const chronon3d::Fill& fill) {
    std::vector<BLGradientStop> stops;
    stops.reserve(fill.gradient.color_stops.size());
    for (const auto& stop : fill.gradient.color_stops) {
        Color color = stop.color;
        if (!fill.gradient.opacity_stops.empty())
            color.a *= graphics::detail::sample_opacity_stops(
                fill.gradient.opacity_stops,
                graphics::detail::apply_spread(stop.position, fill.gradient.spread));
        stops.emplace_back(static_cast<double>(stop.position), to_bl_rgba(color));
    }
    return stops;
}

std::optional<BLGradient> build_bl_gradient(
    const chronon3d::Fill& fill,
    float origin_x,
    float origin_y,
    float width,
    float height
) {
    if (fill.type == chronon3d::FillType::Solid) return std::nullopt;
    if (fill.gradient.color_stops.empty()) return std::nullopt;

    const float safe_w = std::max(1.0f, width);
    const float safe_h = std::max(1.0f, height);

    auto stops = build_gradient_stops(fill);

    switch (fill.type) {
        case chronon3d::FillType::LinearGradient: {
            const BLLinearGradientValues values(
                origin_x + fill.gradient.start.x * safe_w,
                origin_y + fill.gradient.start.y * safe_h,
                origin_x + fill.gradient.end.x * safe_w,
                origin_y + fill.gradient.end.y * safe_h);
            const auto extend = fill.gradient.spread == graphics::GradientSpread::Repeat
                ? BL_EXTEND_MODE_REPEAT
                : fill.gradient.spread == graphics::GradientSpread::Reflect
                    ? BL_EXTEND_MODE_REFLECT : BL_EXTEND_MODE_PAD;
            return BLGradient(values, extend, stops.data(), stops.size());
        }
        case chronon3d::FillType::RadialGradient: {
            const float radius = std::max(0.001f,
                std::max(safe_w, safe_h) * fill.gradient.radius);
            const BLRadialGradientValues values(
                origin_x + fill.gradient.center.x * safe_w,
                origin_y + fill.gradient.center.y * safe_h,
                origin_x + fill.gradient.center.x * safe_w,
                origin_y + fill.gradient.center.y * safe_h,
                0.0, radius);
            const auto extend = fill.gradient.spread == graphics::GradientSpread::Repeat
                ? BL_EXTEND_MODE_REPEAT
                : fill.gradient.spread == graphics::GradientSpread::Reflect
                    ? BL_EXTEND_MODE_REFLECT : BL_EXTEND_MODE_PAD;
            return BLGradient(values, extend, stops.data(), stops.size());
        }
        case chronon3d::FillType::ConicGradient: {
            const double cx = origin_x + fill.gradient.center.x * safe_w;
            const double cy = origin_y + fill.gradient.center.y * safe_h;
            const auto extend = fill.gradient.spread == graphics::GradientSpread::Repeat
                ? BL_EXTEND_MODE_REPEAT
                : fill.gradient.spread == graphics::GradientSpread::Reflect
                    ? BL_EXTEND_MODE_REFLECT : BL_EXTEND_MODE_PAD;
            const BLConicGradientValues values(cx, cy, fill.gradient.angle);
            return BLGradient(values, extend, stops.data(), stops.size());
        }
        case chronon3d::FillType::Solid:
            return std::nullopt;  // handled above
    }
    return std::nullopt;
}

} // namespace chronon3d::blend2d_bridge::paint
