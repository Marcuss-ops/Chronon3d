// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program_compiler.cpp
//
// CameraProgram compiler implementation.
//
// compile_camera() transforms a CameraDescriptor into a compiled CameraProgram
// with no Registry lookups during evaluate().
//
// The compiled CameraProgram stores:
//   - The original CameraDescriptor (for inspection / serialisation)
//   - A resolved CameraMotion shared_ptr (if source was RegisteredMotionRef)
//   - The compiled_ flag
//
// builtin_camera_presets() returns all legacy presets as CameraDescriptors,
// enabling the compiled evaluation path without CameraMotionRegistry.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>

#include <cmath>
#include <utility>

namespace chronon3d::camera_v1 {

bool compile_camera(const CameraDescriptor& descriptor,
                    CameraProgram& out_program,
                    CameraCompileError* out_error,
                    const CameraCatalog* catalog) {
    // ── 1. Store the descriptor ─────────────────────────────────────────
    out_program.descriptor_ = descriptor;

    // ── 2. Resolve source ────────────────────────────────────────────────
    if (auto* ref = std::get_if<RegisteredMotionRef>(&descriptor.source)) {
        // Try the catalog first.
        std::shared_ptr<const CameraMotion> resolved;

        if (catalog) {
            auto* preset_desc = catalog->find_descriptor(ref->id);
            if (preset_desc) {
                // Recursively compile the referenced descriptor.
                // This handles preset -> preset chains.
                CameraProgram recursive;
                CameraCompileError recursive_err;
                if (!compile_camera(*preset_desc, recursive,
                                    &recursive_err, catalog)) {
                    if (out_error) {
                        *out_error = CameraCompileError{
                            CameraCompileError::Kind::MotionNotFound,
                            "motion '" + ref->id + "' catalog entry failed to compile"
                        };
                    }
                    return false;
                }
                // Preserve the resolved source from the recursive compile
                // while keeping the top-level descriptor (id, failure_policy, etc.).
                auto resolved_source = recursive.descriptor_.source;
                out_program = std::move(recursive);
                out_program.descriptor_ = descriptor;
                out_program.descriptor_.source = resolved_source;
                out_program.compiled_ = true;
                return true;
            }
        }

        // Fallback to CameraMotionRegistry (backward compat during migration).
        auto found = CameraMotionRegistry::instance().find(ref->id);
        if (found) {
            out_program.resolved_motion_ = found;
        } else if (!ref->id.empty()) {
            if (out_error) {
                *out_error = CameraCompileError{
                    CameraCompileError::Kind::MotionNotFound,
                    "motion '" + ref->id + "' not found in catalog or registry"
                };
            }
            return false;
        }

        // For non-empty refs, source is now resolved. For empty refs, use base.
        out_program.source_ = RefResolvedSource{};
    } else if (auto* traj = std::get_if<TrajectoryMotion>(&descriptor.source)) {
        if (traj->trajectory && traj->trajectory->size() == 0) {
            if (out_error) {
                *out_error = CameraCompileError{
                    CameraCompileError::Kind::TrajectoryEmpty,
                    "trajectory has zero segments"
                };
            }
            return false;
        }
        // Store the trajectory directly (no registry lookup needed).
        out_program.source_ = TrajectorySource{traj->trajectory};
    } else if (std::holds_alternative<StaticCameraSource>(descriptor.source)) {
        out_program.source_ = StaticCameraSource{};
    } else if (std::holds_alternative<PoseTracksSource>(descriptor.source)) {
        // PoseTracksSource is evaluated directly — no registry needed.
        out_program.source_ = RefResolvedSource{};
    } else if (std::holds_alternative<OrbitMotion>(descriptor.source)) {
        // OrbitMotion is evaluated directly — no registry needed.
        out_program.source_ = RefResolvedSource{};
    }

    // ── 3. Set failure policy ────────────────────────────────────────────
    out_program.failure_policy_ = descriptor.failure_policy;

    // ── 4. Compute time_dependent flag ───────────────────────────────────
    // Conservative: any non-static source is assumed time-dependent.
    // Any modifier also makes the camera time-dependent (e.g. IdleOscillation).
    bool source_is_static = std::holds_alternative<StaticCameraSource>(descriptor.source);
    bool has_modifiers = !descriptor.modifiers.empty();
    out_program.time_dependent_ = !source_is_static || has_modifiers;

    // ── 5. Mark as compiled and return ───────────────────────────────────
    out_program.compiled_ = true;
    return true;
}

// =============================================================================
// builtin_camera_presets() — all 19 legacy presets as CameraDescriptors.
//
// These CameraDescriptors use PoseTracksSource or OrbitMotion directly as
// their source, so compile_camera() can evaluate them without the
// CameraMotionRegistry. The legacy CameraMotion classes are still registered
// via register_camera_motion_presets() / register_camera_rig_motions() for
// backward compat with callers that use CameraMotionRegistry::find().
// =============================================================================

namespace {

constexpr float kDefaultZoom = 1000.0f;

// ── Helper: build a CameraBaseSpec with a zoom projection ─────────────────
CameraBaseSpec base_spec(Vec3 position, float zoom = kDefaultZoom) {
    CameraBaseSpec bs;
    bs.enabled = true;
    bs.position = position;
    bs.rotation = Vec3{0.0f, 0.0f, 0.0f};
    bs.projection = ZoomProjection{AnimatedValue<float>{zoom}};
    return bs;
}

// ── Helper: PoseTracksSource with a single animated Z position ───────────
PoseTracksSource pose_z(float from_z, float to_z,
                         EasingCurve easing = Easing::InOutCubic) {
    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, from_z})
               .key(Frame{90}, Vec3{0.0f, 0.0f, to_z}, easing);
    pts.use_target = false;
    return pts;
}

// ── Helper: PoseTracksSource for a push (pos Z + tilt) ──────────────────
PoseTracksSource push_tilt(float from_z, float to_z,
                            float from_tilt, float to_tilt,
                            EasingCurve easing = Easing::InOutCubic) {
    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, from_z})
               .key(Frame{90}, Vec3{0.0f, 0.0f, to_z}, easing);
    pts.rotation.key(Frame{0}, Vec3{from_tilt, 0.0f, 0.0f})
                .key(Frame{90}, Vec3{to_tilt, 0.0f, 0.0f}, easing);
    pts.use_target = false;
    return pts;
}

// ── Helper: PoseTracksSource for a pan (X position) ─────────────────────
PoseTracksSource pan_x(float from_x, float to_x, float z = -1000.0f,
                        EasingCurve easing = Easing::InOutCubic) {
    PoseTracksSource pts;
    pts.position.key(Frame{0}, Vec3{from_x, 0.0f, z})
               .key(Frame{90}, Vec3{to_x, 0.0f, z}, easing);
    pts.use_target = true;
    pts.target.set(Vec3{0.0f, 0.0f, 0.0f});
    return pts;
}

// ── Helper: PoseTracksSource for a dolly+rotate (pos Z + rotation) ──────
PoseTracksSource dolly_rotate_pts(float zoom_val) {
    PoseTracksSource pts;
    EasingCurve easing = Easing::InOutCubic;
    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1280.0f})
               .key(Frame{90}, Vec3{0.0f, 0.0f, -820.0f}, easing);
    pts.rotation.key(Frame{0}, Vec3{-2.5f, -16.0f, -2.0f})
                .key(Frame{90}, Vec3{2.5f, 16.0f, 2.0f}, easing);
    pts.use_target = false;
    pts.zoom.set(zoom_val);
    return pts;
}

// ── Helper: PoseTracksSource for a roll reveal ──────────────────────────
PoseTracksSource roll_reveal_pts(float max_roll_deg, float zoom_val) {
    PoseTracksSource pts;
    pts.position.set(Vec3{0.0f, 0.0f, -1000.0f});
    pts.rotation.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f})
                .key(Frame{90}, Vec3{0.0f, 0.0f, max_roll_deg}, Easing::InOutCubic);
    pts.use_target = false;
    pts.zoom.set(zoom_val);
    return pts;
}

// ── Helper: build a NamedCameraPreset ─────────────────────────────────────
NamedCameraPreset make_preset(
    std::string_view id,
    std::string_view category,
    std::string_view description,
    CameraDescriptor desc) {
    return NamedCameraPreset{id, category, description, std::move(desc)};
}

// ── Helper: CameraDescriptor with a PoseTracksSource ──────────────────────
CameraDescriptor desc_with_pose(
    std::string id_str,
    CameraBaseSpec base,
    PoseTracksSource source,
    OrientationSpec orient = FixedOrientation{}) {
    CameraDescriptor desc;
    desc.id = std::move(id_str);
    desc.base = std::move(base);
    desc.source = std::move(source);
    desc.orientation = std::move(orient);
    return desc;
}

// ── Helper: CameraDescriptor with an OrbitMotion source ───────────────────
CameraDescriptor desc_with_orbit(
    std::string id_str,
    CameraBaseSpec base,
    OrbitMotion source,
    OrientationSpec orient = FixedOrientation{}) {
    CameraDescriptor desc;
    desc.id = std::move(id_str);
    desc.base = std::move(base);
    desc.source = std::move(source);
    desc.orientation = std::move(orient);
    return desc;
}

} // anonymous namespace

std::span<const NamedCameraPreset> builtin_camera_presets() {
    // ── 1. camera_motion parametric presets (5) ─────────────────────────
    static const NamedCameraPreset kPresets[] = {

        // ── camera.dolly ──────────────────────────────────────────────
        make_preset("camera.dolly", "dolly",
            "Dolly in/out with smoothstep easing",
            desc_with_pose(
                "camera.dolly",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                pose_z(-1200.0f, -800.0f, Easing::InOutCubic))),

        // ── camera.pan ────────────────────────────────────────────────
        make_preset("camera.pan", "pan",
            "Pan left-to-right with smoothstep easing",
            desc_with_pose(
                "camera.pan",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                pan_x(-120.0f, 120.0f, -1000.0f, Easing::InOutCubic),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.tilt_roll ──────────────────────────────────────────
        make_preset("camera.tilt_roll", "tilt-roll",
            "Static camera with tilt, pan, and roll",
            []() {
                CameraDescriptor desc;
                desc.id = "camera.tilt_roll";
                desc.base = base_spec(Vec3{0.0f, 0.0f, -1000.0f});
                desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
                desc.source = StaticCameraSource{};
                return desc;
            }()),

        // ── camera.orbit ──────────────────────────────────────────────
        make_preset("camera.orbit", "orbit",
            "Orbit around Y axis with smoothstep easing",
            desc_with_orbit(
                "camera.orbit",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                []() {
                    OrbitMotion om;
                    om.target.set(Vec3{0.0f, 0.0f, 0.0f});
                    om.yaw.key(Frame{0}, -20.0f)
                         .key(Frame{90}, 20.0f, Easing::InOutCubic);
                    om.radius.set(180.0f);
                    om.pitch.set(0.0f);
                    return om;
                }(),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.push_in_tilt ───────────────────────────────────────
        make_preset("camera.push_in_tilt", "push-in",
            "Push forward while tilting",
            desc_with_pose(
                "camera.push_in_tilt",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                push_tilt(-1200.0f, -850.0f, -4.0f, 4.0f, Easing::InOutCubic))),

        // ── 2. camera_motion convenience presets (7) ───────────────────
        // Each convenience preset bakes specific param defaults.

        // ── camera.dolly_in ───────────────────────────────────────────
        make_preset("camera.dolly_in", "dolly",
            "Camera dollies in from -1200 to -800",
            desc_with_pose(
                "camera.dolly_in",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                pose_z(-1200.0f, -800.0f, Easing::InOutCubic))),

        // ── camera.dolly_out ──────────────────────────────────────────
        make_preset("camera.dolly_out", "dolly",
            "Camera dollies out from -800 to -1200",
            desc_with_pose(
                "camera.dolly_out",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                pose_z(-800.0f, -1200.0f, Easing::InOutCubic))),

        // ── camera.parallax_sweep ─────────────────────────────────────
        make_preset("camera.parallax_sweep", "pan",
            "Left-to-right pan with parallax effect",
            desc_with_pose(
                "camera.parallax_sweep",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                pan_x(-120.0f, 120.0f, -1000.0f, Easing::InOutCubic),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.orbit_small ────────────────────────────────────────
        make_preset("camera.orbit_small", "orbit",
            "Tight orbit: ±15° around Y axis",
            desc_with_orbit(
                "camera.orbit_small",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                []() {
                    OrbitMotion om;
                    om.target.set(Vec3{0.0f, 0.0f, 0.0f});
                    om.yaw.key(Frame{0}, -15.0f)
                         .key(Frame{90}, 15.0f, Easing::InOutCubic);
                    om.radius.set(80.0f);
                    return om;
                }(),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.dramatic_push ──────────────────────────────────────
        make_preset("camera.dramatic_push", "push-in",
            "Dramatic push-in: -1300→-760 with -5°→+5° tilt",
            desc_with_pose(
                "camera.dramatic_push",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                push_tilt(-1300.0f, -760.0f, -5.0f, 5.0f, Easing::InOutCubic))),

        // ── camera.dolly_rotate ───────────────────────────────────────
        make_preset("camera.dolly_rotate", "dolly",
            "Dolly forward while yawing/rolling",
            desc_with_pose(
                "camera.dolly_rotate",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                dolly_rotate_pts(1000.0f))),

        // ── camera.roll_reveal ────────────────────────────────────────
        make_preset("camera.roll_reveal", "roll",
            "Subtle roll: 0° → 8° counterclockwise",
            desc_with_pose(
                "camera.roll_reveal",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}),
                roll_reveal_pts(8.0f, 1000.0f))),

        // ── 3. camera_rig presets (7) ──────────────────────────────────

        // ── camera.rig.hero_push_in ───────────────────────────────────
        make_preset("camera.rig.hero_push_in", "push-in",
            "Hero push-in: -1200→-750 with tilt/yaw",
            desc_with_pose(
                "camera.rig.hero_push_in",
                base_spec(Vec3{0.0f, 0.0f, -1200.0f}, 1000.0f),
                []() {
                    PoseTracksSource pts;
                    EasingCurve easing = EasingCurve::cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f);
                    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1200.0f})
                               .key(Frame{90}, Vec3{0.0f, 0.0f, -750.0f}, easing);
                    pts.rotation.key(Frame{0}, Vec3{-4.0f, 0.0f, 0.0f})
                                .key(Frame{90}, Vec3{2.0f, 3.0f, 0.0f}, easing);
                    pts.use_target = false;
                    pts.zoom.set(1000.0f);
                    return pts;
                }())),

        // ── camera.rig.orbit_yaw ──────────────────────────────────────
        make_preset("camera.rig.orbit_yaw", "orbit",
            "Arc orbit around target with yaw",
            desc_with_orbit(
                "camera.rig.orbit_yaw",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}, 1000.0f),
                []() {
                    OrbitMotion om;
                    om.target.set(Vec3{0.0f, 0.0f, 0.0f});
                    EasingCurve easing = Easing::InOutCubic;
                    om.yaw.key(Frame{0}, -25.0f)
                         .key(Frame{120}, 25.0f, easing);
                    om.radius.set(300.0f);
                    om.track.set(Vec3{0.0f, 40.0f, -1000.0f});
                    om.pitch.set(-5.0f);
                    return om;
                }(),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.rig.parallax_pan ───────────────────────────────────
        make_preset("camera.rig.parallax_pan", "pan",
            "Parallax pan: -180→+180 with target lock",
            desc_with_pose(
                "camera.rig.parallax_pan",
                base_spec(Vec3{-180.0f, 0.0f, -1000.0f}, 1000.0f),
                []() {
                    PoseTracksSource pts;
                    EasingCurve easing = Easing::InOutSine;
                    pts.position.key(Frame{0}, Vec3{-180.0f, 0.0f, -1000.0f})
                               .key(Frame{90}, Vec3{180.0f, 0.0f, -1000.0f}, easing);
                    pts.use_target = true;
                    pts.target.set(Vec3{0.0f, 0.0f, 0.0f});
                    pts.zoom.set(1000.0f);
                    return pts;
                }(),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.rig.dolly_zoom ─────────────────────────────────────
        make_preset("camera.rig.dolly_zoom", "dolly-zoom",
            "Dolly forward while zooming out (Hitchcock effect)",
            desc_with_pose(
                "camera.rig.dolly_zoom",
                base_spec(Vec3{0.0f, 0.0f, -1200.0f}, 1200.0f),
                []() {
                    PoseTracksSource pts;
                    EasingCurve easing = Easing::InOutCubic;
                    pts.position.key(Frame{0}, Vec3{0.0f, 0.0f, -1200.0f})
                               .key(Frame{90}, Vec3{0.0f, 0.0f, -600.0f}, easing);
                    // Zoom in tandem: from_zoom→to_zoom
                    pts.zoom.key(Frame{0}, 1200.0f)
                             .key(Frame{90}, 600.0f, easing);
                    pts.use_target = true;
                    pts.target.set(Vec3{0.0f, 0.0f, 0.0f});
                    return pts;
                }(),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.rig.focus_pull ─────────────────────────────────────
        make_preset("camera.rig.focus_pull", "focus",
            "Focus pull: static camera, focus shifts from 0→500",
            desc_with_pose(
                "camera.rig.focus_pull",
                []() {
                    CameraBaseSpec bs;
                    bs.enabled = true;
                    bs.position = Vec3{0.0f, 0.0f, -1000.0f};
                    bs.rotation = Vec3{0.0f, 0.0f, 0.0f};
                    bs.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
                    bs.dof.enabled = true;
                    bs.dof.aperture = 0.03f;
                    bs.dof.max_blur = 24.0f;
                    // focus_distance is animated via PoseTracksSource.focus_distance
                    // keyframes below; the base value is the start of the pull.
                    bs.dof.focus_distance = 0.0f;
                    return bs;
                }(),
                []() {
                    PoseTracksSource pts;
                    EasingCurve easing = Easing::InOutCubic;
                    pts.position.set(Vec3{0.0f, 0.0f, -1000.0f});
                    pts.use_target = false;
                    pts.zoom.set(1000.0f);
                    // Animated DOF: pull focus from 0 → 500 over 90 frames.
                    pts.focus_distance.key(Frame{0}, 0.0f)
                                      .key(Frame{90}, 500.0f, easing);
                    return pts;
                }())),

        // ── camera.rig.low_angle_reveal ───────────────────────────────
        make_preset("camera.rig.low_angle_reveal", "reveal",
            "Low-to-high reveal with tilt correction",
            desc_with_pose(
                "camera.rig.low_angle_reveal",
                base_spec(Vec3{0.0f, -180.0f, -1100.0f}, 1000.0f),
                []() {
                    PoseTracksSource pts;
                    EasingCurve easing = EasingCurve::cubic_bezier(0.33f, 1.0f, 0.68f, 1.0f);
                    pts.position.key(Frame{0}, Vec3{0.0f, -180.0f, -1100.0f})
                               .key(Frame{90}, Vec3{0.0f, 40.0f, -800.0f}, easing);
                    pts.rotation.key(Frame{0}, Vec3{25.0f, 0.0f, 0.0f})
                                .key(Frame{90}, Vec3{0.0f, 0.0f, 0.0f}, easing);
                    pts.use_target = true;
                    pts.target.set(Vec3{0.0f, 0.0f, 0.0f});
                    pts.zoom.set(1000.0f);
                    return pts;
                }(),
                LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}})),

        // ── camera.rig.subtle_float ───────────────────────────────────
        make_preset("camera.rig.subtle_float", "float",
            "Subtle floating camera with sinusoidal drift",
            desc_with_pose(
                "camera.rig.subtle_float",
                base_spec(Vec3{0.0f, 0.0f, -1000.0f}, 1000.0f),
                []() {
                    PoseTracksSource pts;
                    // Sample the sinusoidal float at 12 keyframes over 300 frames
                    // to approximate the continuous oscillation.
                    constexpr int kSamples = 12;
                    constexpr int kDuration = 300;
                    for (int i = 0; i <= kSamples; ++i) {
                        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
                        const float phase = t * static_cast<float>(kDuration);
                        const Frame f{static_cast<i32>(std::round(phase))};
                        const Vec3 pos{
                            15.0f * std::sin(phase * 0.3f),
                            8.0f  * std::cos(phase * 0.2f),
                            -1000.0f + 20.0f * std::sin(phase * 0.15f + 1.0f)
                        };
                        pts.position.key(f, pos);
                    }
                    pts.use_target = false;
                    pts.zoom.set(1000.0f);
                    return pts;
                }())),
    };

    return kPresets;
}

} // namespace chronon3d::camera_v1
