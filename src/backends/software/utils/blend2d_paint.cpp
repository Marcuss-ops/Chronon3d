// ──────────────────────────────────────────────────────────────────────
// blend2d_paint.cpp — implementations of build_gradient_stops and
// build_bl_gradient.  See blend2d_paint.hpp for the rationale.
// ──────────────────────────────────────────────────────────────────────

#include "blend2d_paint.hpp"

#include <cmath>

namespace chronon3d::blend2d_bridge::paint {

std::vector<BLGradientStop> build_gradient_stops(const chronon3d::Fill& fill) {
    std::vector<BLGradientStop> stops;
    stops.reserve(fill.gradient.stops.size());
    for (const auto& stop : fill.gradient.stops) {
        stops.emplace_back(static_cast<double>(stop.offset),
                           to_bl_rgba(stop.color));
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
    if (fill.type() == chronon3d::FillType::Solid) return std::nullopt;
    if (fill.gradient.stops.empty()) return std::nullopt;

    const float safe_w = std::max(1.0f, width);
    const float safe_h = std::max(1.0f, height);

    auto stops = build_gradient_stops(fill);

    switch (fill.type) {
        case chronon3d::FillType::LinearGradient: {
            const BLLinearGradientValues values(
                origin_x + fill.gradient.from.x * safe_w,
                origin_y + fill.gradient.from.y * safe_h,
                origin_x + fill.gradient.to.x   * safe_w,
                origin_y + fill.gradient.to.y   * safe_h
            );
            return BLGradient(values, BL_EXTEND_MODE_PAD,
                              stops.data(), stops.size());
        }
        case chronon3d::FillType::RadialGradient: {
            const float radius_norm = std::max(
                0.001f, fill.gradient.to.x - fill.gradient.from.x);
            const float radius = std::max(safe_w, safe_h) * radius_norm;
            const BLRadialGradientValues values(
                origin_x + fill.gradient.from.x * safe_w,
                origin_y + fill.gradient.from.y * safe_h,
                origin_x + fill.gradient.from.x * safe_w,
                origin_y + fill.gradient.from.y * safe_h,
                0.0,
                radius
            );
            return BLGradient(values, BL_EXTEND_MODE_PAD,
                              stops.data(), stops.size());
        }
        case chronon3d::FillType::ConicGradient: {
            const double cx = origin_x + fill.gradient.from.x * safe_w;
            const double cy = origin_y + fill.gradient.from.y * safe_h;
            const auto dir = fill.gradient.to - fill.gradient.from;
            const double angle = std::atan2(dir.y, dir.x);
            const BLConicGradientValues values(cx, cy, angle);
            return BLGradient(values, BL_EXTEND_MODE_PAD,
                              stops.data(), stops.size());
        }
        case chronon3d::FillType::Solid:
            return std::nullopt;  // handled above
    }
    return std::nullopt;
}

} // namespace chronon3d::blend2d_bridge::paint
