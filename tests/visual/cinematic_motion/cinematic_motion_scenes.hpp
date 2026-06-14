#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/animation/core/temporal_spatial_curve.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <vector>
#include <memory>

namespace chronon3d::test {

// ── Resolution ──────────────────────────────────────────────────────────────

constexpr int kCinematicWidth = 960;
constexpr int kCinematicHeight = 540;

// ── Test 1: SampleTimeSubframeComb ──────────────────────────────────────────
// 8 markers positioned at sub-frame positions between frame 30 and 31.
// Returns a composition that, rendered at frame 30, shows all 8 markers.

Composition make_subframe_comb_scene();

// ── Test 2: SampleTimeContinuityContactSheet ────────────────────────────────
// Renders 9 separate frames at sub-frame positions and composites them into
// a 3×3 contact sheet.  Requires the renderer for per-frame rendering.

Framebuffer render_continuity_contact_sheet(
    SoftwareRenderer& renderer,
    const Composition& animated_scene,
    double base_frame = 30.0);

// ── Test 3: TemporalCacheParity ─────────────────────────────────────────────
// 4-quadrant: cache off/on (top), version A/B (bottom).

Composition make_cache_parity_scene();

// ── Test 4: BezierHandles3D ─────────────────────────────────────────────────
// Three orthographic views (XY, XZ, YZ) of a CubicBezier3D with handles.

Composition make_bezier_handles_3d_scene();

// ── Test 5: ArcLengthSpacing ────────────────────────────────────────────────
// Upper row: parametric sampling.  Lower row: arc-length sampling.

Composition make_arc_length_spacing_scene();

// ── Test 6: TemporalSpatialCurveSeparation ──────────────────────────────────
// 2×2 grid showing spatial path × temporal curve independence.

Composition make_temporal_spatial_separation_scene();

// ── Test 7: QuaternionShortestPath ──────────────────────────────────────────
// 9 cards showing quaternion rotation from yaw=+179° to yaw=-179°.
// With shortest-path quaternion, the rotation is ~2° instead of ~358°.

Composition make_quaternion_shortest_path_scene();

// ── Test 8: QuaternionPathOrientation ───────────────────────────────────────
// 16 camera frustums along an S-shaped bezier path with forward/up/right axes.

Composition make_quaternion_path_orientation_scene();

// ── Test 9: TwoNodeTargetLock ───────────────────────────────────────────────
// Contact sheet of 5 frames with camera orbiting/dollying around centered target.

Framebuffer render_two_node_target_lock_contact_sheet(
    SoftwareRenderer& renderer);

// ── Test 10: MotionBlurShutter ──────────────────────────────────────────────
// 4-panel comparison: 1 sample, 8s/180°, 16s/180°, 16s/360° shutter.

Framebuffer render_motion_blur_comparison(
    SoftwareRenderer& renderer);

// ── Test 11: ArcLengthCacheInvalidation ─────────────────────────────────────
// Before/after comparison when bezier handle changes (placeholder for PR5 LUT).

Composition make_arc_length_cache_invalidation_before();
Composition make_arc_length_cache_invalidation_after();

// ── General-purpose compositing helpers ─────────────────────────────────────

/// Copy a source framebuffer into a region of a destination framebuffer.
/// No scaling — source must fit exactly.  Returns dest for chaining.
Framebuffer& blit_into(Framebuffer& dest, const Framebuffer& src,
                       int dst_x0, int dst_y0);

/// Composite 9 framebuffers (320×180 each) into a 3×3 contact sheet (960×540).
Framebuffer composite_contact_sheet_3x3(
    const std::vector<std::shared_ptr<Framebuffer>>& panels);

/// Composite 4 framebuffers (480×270 each) into a 2×2 quadrant layout.
Framebuffer composite_quad_2x2(
    const std::vector<std::shared_ptr<Framebuffer>>& quads);

/// Draw a coordinate crosshair at (cx, cy) on the framebuffer.
void draw_crosshair(Framebuffer& fb, int cx, int cy, Color color,
                    int arm_len = 10);

/// Draw a small axis tripod (X=red, Y=green, Z=blue) at given screen position.
void draw_axis_tripod(Framebuffer& fb, int cx, int cy,
                      Vec3 x_dir, Vec3 y_dir, Vec3 z_dir,
                      float scale = 20.0f);

} // namespace chronon3d::test
