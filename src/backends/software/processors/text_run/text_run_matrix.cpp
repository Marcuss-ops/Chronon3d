// ── Glyph matrix builder for text run processor ─────────────────────────
// Extracted from text_run_processor.cpp.
// Builds a 2D affine matrix for a single glyph following the
// After Effects transform order.

#include <chronon3d/backends/software/text_run_processor.hpp>
#include <blend2d.h>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

// ── Portable PI ──────────────────────────────────────────────────────
constexpr double kPi = 3.14159265358979323846;

// ── Glyph matrix builder ─────────────────────────────────────────────
//
// Builds a 2D affine matrix for a single glyph following the
// After Effects transform order:
//   T(layout_pos) × T(position) × T(anchor) × R(rotationZ) × Skew × S(scale) × T(-anchor)
//
// Skew axis controls the direction of the skew (0° = X-axis, 90° = Y-axis).

[[nodiscard]] BLMatrix2D build_glyph_matrix(const GlyphInstanceState& g) {
    const double lx = static_cast<double>(g.layout_position.x);
    const double ly = static_cast<double>(g.layout_position.y);
    const double px = static_cast<double>(g.position.x);
    const double py = static_cast<double>(g.position.y);
    // 2.5D extension: depth (Z translation) → uniform scale attenuation.
    const double pz = static_cast<double>(g.position.z);
    const double ax = static_cast<double>(g.anchor.x);
    const double ay = static_cast<double>(g.anchor.y);

    // Combined uniform scale: scale.z multiplies both X and Y.  Negative
    // scale.z flips the glyph (mirror); 0 collapses it.
    const double sz_raw_x = static_cast<double>(g.scale.x) * static_cast<double>(g.scale.z);
    const double sz_raw_y = static_cast<double>(g.scale.y) * static_cast<double>(g.scale.z);

    // Rotation Z in radians (degrees → radians)
    const double rz_rad = static_cast<double>(g.rotation.z) * (kPi / 180.0);
    const double cos_a = std::cos(rz_rad);
    const double sin_a = std::sin(rz_rad);

    // 2.5D rotations: emulate 3D orientation in a 2D rasterizer via
    // pure-affine shears.  rotation.y rotates around the Y axis (horizontal
    // hinge): glyph edges tilt left/right, simulated as horizontal shear
    // proportional to vertical position.  rotation.x rotates around the X
    // axis (vertical hinge): simulated as vertical shear proportional to
    // horizontal position.  These are not true perspective but match the
    // visual feel of classic 2.5D systems.
    const double ry_rad = static_cast<double>(g.rotation.y) * (kPi / 180.0);
    const double rx_rad = static_cast<double>(g.rotation.x) * (kPi / 180.0);
    // Clamp angle magnitude to |pi/2 - 0.01| so tan() doesn't blow up; tan
    // moves from -inf to +inf across +/-90° so any input within +/-89.95°
    // gives a usable finite shear.
    const double ry_clamped = std::clamp(ry_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
    const double rx_clamped = std::clamp(rx_rad, -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
    const double horiz_shear = std::tan(ry_clamped);
    const double vert_shear  = std::tan(rx_clamped);

    // Depth-based perspective: positive z = further away = smaller glyph
    // (friendly: AE convention "Z negative = closer").  Use a simple
    // attenuation model: depth_factor = 1 / (1 + |z| * 0.001).  At
    // z=0 the glyph stays full-size; at z=1000 the glyph is ~50%.
    const double depth_factor = (std::isfinite(pz) && pz > 0.0)
        ? 1.0 / (1.0 + pz * 0.001)
        : 1.0;

    // Skew (existing 2D shear framing)
    const double skew_rad = static_cast<double>(g.skew) * (kPi / 180.0);
    const double skew_axis_rad = static_cast<double>(g.skew_axis) * (kPi / 180.0);
    const double tan_skew = std::tan(skew_rad);
    const double cos_axis = std::cos(skew_axis_rad);
    const double sin_axis = std::sin(skew_axis_rad);

    // Build the matrix step by step, starting with identity.
    BLMatrix2D m;
    m.reset();

    // 1. Translate to layout position
    m.translate(lx, ly);

    // 1.5. Apply baseline shift (animator-driven nudges per AE convention):
    //      positive pixels = drop below baseline (subscript),
    //      negative pixels = raise above baseline (superscript).
    //      Applied BEFORE the position offset so that .position() stacks
    //      on top of the baseline-shifted glyph rather than replacing it.
    m.translate(0.0, static_cast<double>(g.baseline_shift));

    // 2. Translate by animated position (x, y)
    m.translate(px, py);

    // 3. Apply depth-based uniform scaling for positive z.
    //    Negative z = "closer to camera" — no shrinkage.
    if (std::abs(depth_factor - 1.0) > 1e-9) {
        m.scale(depth_factor, depth_factor);
    }

    // 4. Translate to anchor
    m.translate(ax, ay);

    // 5. Compose: scale → 2.5D shears (horiz/vert) → skew → rotation Z
    BLMatrix2D srt;
    srt.reset(sz_raw_x, 0.0, 0.0, sz_raw_y, 0.0, 0.0);   // start with scale

    // 5a. 2.5D shears — apply on top of scale.
    //     horiz_shear: x' = x + horiz_shear * y  (rotation.y)
    //     vert_shear:  y' = y + vert_shear  * x  (rotation.x)
    if (std::abs(horiz_shear) > 1e-9) {
        BLMatrix2D sh;
        sh.reset(1.0, horiz_shear, 0.0, 1.0, 0.0, 0.0);
        srt.transform(sh);
    }
    if (std::abs(vert_shear) > 1e-9) {
        BLMatrix2D sh;
        sh.reset(1.0, 0.0, vert_shear, 1.0, 0.0, 0.0);
        srt.transform(sh);
    }

    // 5b. Skew along axis (existing) — R(+axis) × SkewX × R(-axis) then
    //     re-apply X/Y scale so we don't lose the 2.5D shears.
    if (std::abs(skew_rad) > 1e-9) {
        BLMatrix2D rot_to_axis;
        rot_to_axis.reset(cos_axis, sin_axis, -sin_axis, cos_axis, 0.0, 0.0);

        BLMatrix2D rot_from_axis;
        rot_from_axis.reset(cos_axis, -sin_axis, sin_axis, cos_axis, 0.0, 0.0);

        BLMatrix2D skew_x;
        skew_x.reset(1.0, 0.0, tan_skew, 1.0, 0.0, 0.0);

        BLMatrix2D skew_composed = rot_to_axis;
        skew_composed.transform(skew_x);
        skew_composed.transform(rot_from_axis);
        // Re-apply scale after skew compose so the 2.5D shears are preserved.
        BLMatrix2D scale_mat;
        scale_mat.reset(sz_raw_x, 0.0, 0.0, sz_raw_y, 0.0, 0.0);
        skew_composed.transform(scale_mat);
        srt.transform(skew_composed);
    }

    // 5c. Rotation Z applied last (post all scale/shear/skew).
    BLMatrix2D rot;
    rot.reset(cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0);
    BLMatrix2D composed = rot;
    composed.transform(srt);
    m.transform(composed);

    // 6. Translate back from anchor
    m.translate(-ax, -ay);

    return m;
}

} // namespace chronon3d::renderer
