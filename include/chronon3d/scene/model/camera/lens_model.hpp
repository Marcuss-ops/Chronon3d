#pragma once

// ============================================================================
// lens_model.hpp — Unified LensModel with GateFit and lens presets
//
// A LensModel captures the complete physical lens and sensor setup:
//   - Focal length (mm)
//   - Sensor dimensions (width × height in mm)
//   - F-stop (aperture ratio)
//   - Close focus distance (minimum focus distance in scene units)
//   - Gate fit mode (how the sensor image maps to the viewport)
//
// GateFit controls the mapping from sensor rectangle (e.g. 36×24mm) to the
// viewport rectangle (e.g. 1920×1080px) when their aspect ratios differ:
//   - Fill:    Sensor fills viewport; content may be cropped if ratios differ.
//   - Overscan (Fit): Entire sensor is visible; viewport has letterbox/pillarbox.
//   - Stretch: Sensor image stretches to fill viewport (distorts aspect ratio).
//
// Usage:
//   LensModel lens = LensPresets::arri_35mm();
//   lens.f_stop = 2.8f;
//   f32 hfov_deg = lens.horizontal_fov_deg();
//   f32 focal_px = lens.focal_pixels(1920);
//
// Integration:
//   Camera2_5D now carries a LensModel instead of loose sensor_width/focal_length.
//   The projection pipeline (focal_from_camera) uses gate-fit logic to convert
//   the lens's focal length in mm to the equivalent focal length in pixels.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <cmath>
#include <array>
#include <string>
#include <string_view>

namespace chronon3d {

// ── Gate fit mode ───────────────────────────────────────────────────────────
// Determines how the sensor rectangle maps into the viewport rectangle when
// their aspect ratios differ.
//
// Example with a 3:2 sensor (36×24mm) mapped to a 16:9 viewport (1920×1080):
//
//   Fill (default):
//     Sensor covers the full viewport.  The 3:2 sensor is cropped at top/bottom
//     (letterbox) to match 16:9.  Effective sensor height = 36 * 9/16 = 20.25mm.
//     No black bars.  Widely used in cinema / video mode on still cameras.
//
//   Overscan:
//     Entire sensor is visible inside the viewport.  The 16:9 viewport gets
//     black pillarbox bars on the left/right.  Aspect ratio of the image
//     matches the sensor's native 3:2.  Used when the full negative must be
//     preserved (e.g. anamorphic desqueeze preview).
//
//   Stretch:
//     The sensor image is non-uniformly scaled to fill the viewport, changing
//     the effective pixel aspect ratio.  Rarely used except for certain
//     anamorphic workflows or legacy compatibility.
enum class GateFit {
    Fill,      // Sensor fills viewport (crop if needed)
    Overscan,  // Entire sensor visible (add bars if needed)
    Stretch    // Stretch sensor to viewport (distort aspect ratio)
};

// ── LensModel ───────────────────────────────────────────────────────────────
// Complete description of a physical lens + sensor setup.
//
// All length values are in millimetres except where noted.
// Close focus is in scene units (same unit as camera.position).
struct LensModel {
    // ── Lens parameters ──────────────────────────────────────────────────
    f32 focal_length{50.0f};      // lens focal length in mm (50mm = normal)
    f32 f_stop{2.8f};             // f-number (aperture ratio)
    f32 close_focus{300.0f};      // minimum focus distance in scene units

    // ── Sensor parameters ────────────────────────────────────────────────
    f32 sensor_width{36.0f};      // sensor/film gate width in mm
    f32 sensor_height{24.0f};     // sensor/film gate height in mm (24mm = FF)

    // ── Gate fit ─────────────────────────────────────────────────────────
    GateFit gate_fit{GateFit::Fill};

    // ── Computed helpers ─────────────────────────────────────────────────

    /// Aspect ratio of the physical sensor (width / height).
    [[nodiscard]] f32 sensor_aspect() const {
        return sensor_height > 0.0f ? sensor_width / sensor_height : 1.0f;
    }

    /// Horizontal field of view in degrees from lens + sensor.
    [[nodiscard]] f32 horizontal_fov_deg() const {
        if (focal_length <= 0.0f) return 0.0f;
        return 2.0f * std::atan(sensor_width * 0.5f / focal_length) * 180.0f / glm::pi<f32>();
    }

    /// Vertical field of view in degrees.
    [[nodiscard]] f32 vertical_fov_deg() const {
        if (focal_length <= 0.0f) return 0.0f;
        return 2.0f * std::atan(sensor_height * 0.5f / focal_length) * 180.0f / glm::pi<f32>();
    }

    /// Aperture diameter in mm (focal_length / f_stop).
    [[nodiscard]] f32 aperture_diameter_mm() const {
        return f_stop > 0.0f ? focal_length / f_stop : 0.0f;
    }

    /// Convert lens focal length to effective focal length in pixels
    /// for a viewport of given width and height.
    ///
    /// Gate-fit logic:
    ///   Fill:    The sensor is scaled up to cover the viewport. The smaller
    ///            dimension of the viewport determines the scale factor.
    ///   Overscan: The sensor is scaled down to fit inside the viewport. The
    ///             larger dimension of the sensor determines the scale factor.
    ///   Stretch:  Independent X/Y scale to fill viewport exactly.
    ///
    /// With Fill (the common case for video/cinema), the focal length in pixels
    /// is derived from the viewport width and the sensor's horizontal coverage
    /// after cropping to the viewport aspect ratio.
    [[nodiscard]] f32 focal_pixels(f32 viewport_width, f32 viewport_height) const {
        if (focal_length <= 0.0f || viewport_width <= 0.0f || viewport_height <= 0.0f)
            return 1000.0f;

        const f32 viewport_aspect = viewport_width / viewport_height;
        const f32 sensor_aspect_ratio = sensor_aspect();

        f32 effective_sensor_fraction;  // fraction of sensor width used

        switch (gate_fit) {
            case GateFit::Fill: {
                // Sensor is cropped to match viewport aspect ratio.
                // If sensor is wider (3:2 > 16:9), we crop top/bottom.
                // If viewport is wider (16:9 > 3:2), we crop left/right.
                if (sensor_aspect_ratio > viewport_aspect) {
                    // Sensor wider than viewport: use sensor_height * viewport_aspect as effective width
                    effective_sensor_fraction = viewport_aspect / sensor_aspect_ratio;
                } else {
                    // Viewport wider than sensor: sensor width fills viewport
                    effective_sensor_fraction = 1.0f;
                }
                break;
            }
            case GateFit::Overscan: {
                // Entire sensor visible inside viewport. The horizontal FOV is
                // determined by the full sensor width.  Pillarbox/letterbox bars
                // are purely visual and don't affect the projection.
                effective_sensor_fraction = 1.0f;
                break;
            }
            case GateFit::Stretch: {
                // Use sensor width as-is (viewport distorts aspect ratio)
                effective_sensor_fraction = 1.0f;
                break;
            }
            default:
                effective_sensor_fraction = 1.0f;
        }

        // Focal length in pixels: scale from sensor-space to viewport-space.
        // focal_px = focal_mm * (viewport_width / (sensor_width * effective_sensor_fraction))
        const f32 effective_sensor_width = sensor_width * effective_sensor_fraction;
        if (effective_sensor_width <= 0.0f) return 1000.0f;
        return focal_length * (viewport_width / effective_sensor_width);
    }
};

// ── LensPresets ─────────────────────────────────────────────────────────────
// Factory functions for common real-world camera lenses + sensor combos.
//
// Each preset returns a LensModel with typical focal length, max aperture,
// close focus, and the native sensor size for that camera system.
namespace LensPresets {

/// ARRI Alexa 35 —  Super 35 sensor with typical cinema prime.
/// Native sensor: 27.03 × 14.25 mm (Open Gate 4.6K).
inline LensModel arri_35mm() {
    LensModel l;
    l.focal_length   = 50.0f;
    l.f_stop         = 2.8f;
    l.close_focus    = 450.0f;
    l.sensor_width   = 27.03f;
    l.sensor_height  = 14.25f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

/// RED Helium 8K — S35 sensor, typical for documentary / commercial work.
inline LensModel red_helium_50mm() {
    LensModel l;
    l.focal_length   = 50.0f;
    l.f_stop         = 2.8f;
    l.close_focus    = 450.0f;
    l.sensor_width   = 29.90f;
    l.sensor_height  = 15.77f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

/// Full-frame 35mm stills camera (36×24mm) with a 50mm normal lens.
inline LensModel full_frame_50mm() {
    LensModel l;
    l.focal_length   = 50.0f;
    l.f_stop         = 1.8f;
    l.close_focus    = 450.0f;
    l.sensor_width   = 36.0f;
    l.sensor_height  = 24.0f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

/// Full-frame 35mm with 35mm wide-angle lens.
inline LensModel full_frame_35mm() {
    LensModel l = full_frame_50mm();
    l.focal_length  = 35.0f;
    l.f_stop        = 1.4f;
    l.close_focus   = 300.0f;
    return l;
}

/// Full-frame 35mm with 85mm portrait lens.
inline LensModel full_frame_85mm() {
    LensModel l = full_frame_50mm();
    l.focal_length  = 85.0f;
    l.f_stop        = 1.4f;
    l.close_focus   = 850.0f;
    return l;
}

/// Full-frame 35mm with 24mm ultra-wide lens.
inline LensModel full_frame_24mm() {
    LensModel l = full_frame_50mm();
    l.focal_length  = 24.0f;
    l.f_stop        = 1.4f;
    l.close_focus   = 250.0f;
    return l;
}

/// Full-frame 35mm with 135mm telephoto lens.
inline LensModel full_frame_135mm() {
    LensModel l = full_frame_50mm();
    l.focal_length  = 135.0f;
    l.f_stop        = 2.0f;
    l.close_focus   = 1200.0f;
    return l;
}

/// Anamorphic — 2× squeeze on 35mm film gate (21.95 × 18.59mm).
/// Desqueezed FOV is 2× wider than spherical equivalent.
inline LensModel anamorphic_50mm() {
    LensModel l;
    l.focal_length   = 50.0f;
    l.f_stop         = 2.8f;
    l.close_focus    = 600.0f;
    l.sensor_width   = 21.95f;   // 4-perf 35mm anamorphic gate
    l.sensor_height  = 18.59f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

/// Super 16mm film — 12.52 × 7.41mm, popular for indie/documentary.
inline LensModel super16_12mm() {
    LensModel l;
    l.focal_length   = 12.0f;
    l.f_stop         = 1.8f;
    l.close_focus    = 150.0f;
    l.sensor_width   = 12.52f;
    l.sensor_height  = 7.41f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

/// Micro Four Thirds — 17.3 × 13.0mm with 25mm normal lens.
inline LensModel mft_25mm() {
    LensModel l;
    l.focal_length   = 25.0f;
    l.f_stop         = 1.7f;
    l.close_focus    = 250.0f;
    l.sensor_width   = 17.3f;
    l.sensor_height  = 13.0f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

/// IMAX — 70mm horizontal (~52.63 × 22.06mm for IMAX 15/70).
inline LensModel imax_50mm() {
    LensModel l;
    l.focal_length   = 50.0f;
    l.f_stop         = 4.0f;
    l.close_focus    = 1000.0f;
    l.sensor_width   = 52.63f;
    l.sensor_height  = 22.06f;
    l.gate_fit       = GateFit::Fill;
    return l;
}

} // namespace LensPresets

} // namespace chronon3d
