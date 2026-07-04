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
//
// CAM-02 additions:
//   - 3-arg compile_camera() with CameraCompileContext supports cycle
//     detection in RegisteredMotionRef chains.
//   - On every entry, descriptor.id is inserted into ctx.visited_ids; the
//     resolution_stack is then pushed.  On every exit (success or error),
//     both are cleaned up.  A duplicate insert produces
//     Kind::CircularCatalogReference with a "a -> b -> a" path string.
//   - Program::evaluation_dependency_ is set from a survey of the
//     descriptor's constraints: DampedFollowConstraint ⇒ RequiresHistory;
//     all other constraint / source / modifier combinations ⇒ Stateless.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>

#include <cmath>
#include <span>
#include <utility>
#include <variant>

namespace chronon3d::camera_v1 {

// CAM-02: 3-arg compile_camera() is the canonical implementation.  The
// 2-arg public overload (declared in camera_program_compiler.hpp) is an
// inline forwarder that constructs a fresh CameraCompileContext.
Result<CameraProgram, CameraCompileError>
compile_camera(const CameraDescriptor& descriptor,
               const CameraCatalog* catalog,
               CameraCompileContext& ctx) {
    // ── CAM-02: cycle detection.  clean-up helper must reach every exit.
    auto leave_scope = [&ctx, &descriptor]() {
        if (!ctx.resolution_stack.empty()) {
            ctx.resolution_stack.pop_back();
        }
        ctx.visited_ids.erase(descriptor.id);
    };

    // If this id is already being resolved, we have a circular chain.
    // Build a diagnostic message showing the resolution path + the
    // back-edge that closes the loop.
    if (!ctx.visited_ids.insert(descriptor.id).second) {
        std::string path;
        for (const auto& s : ctx.resolution_stack) {
            if (!path.empty()) path += " -> ";
            path += std::string{s};
        }
        if (!path.empty()) path += " -> ";
        path += descriptor.id + " (back-edge to id already in resolution)";
        // No push occurred yet but the failed insert is itself a state
        // change to the set; restore by erasing so the parent's
        // leave_scope() call (which will run on return) leaves a clean ctx.
        ctx.visited_ids.erase(descriptor.id);
        return CameraCompileError{
            CameraCompileError::Kind::CircularCatalogReference,
            "circular catalog reference: " + path
        };
    }
    ctx.resolution_stack.push_back(descriptor.id);

    CameraProgram program;
    program.descriptor_ = descriptor;

    // ── 1. Resolve source ────────────────────────────────────────────────
    if (auto* ref = std::get_if<RegisteredMotionRef>(&descriptor.source)) {
        // Try to resolve via the catalog (CameraMotionRegistry is removed).
        if (catalog) {
            auto* preset_desc = catalog->find_descriptor(ref->id);
            if (preset_desc) {
                // Recursively compile the referenced descriptor.  Cycle
                // detection + resolution_stack diagnostics are handled by
                // `ctx`.  Errors propagate back with the inner diagnostic
                // augmented by the wrapping `ref->id`.
                auto recursive_result =
                    compile_camera(*preset_desc, catalog, ctx);
                if (!recursive_result) {
                    auto inner = recursive_result.error();
                    // CAM-02 fix-set: do NOT compound the wrapper layer when
                    // the inner error is already a CircularCatalogReference.
                    // Without this, three recursive layers deep produce
                    // "motion 'A' failed: motion 'A' failed: circular..." which
                    // is hard to read in logs.
                    if (inner.kind != CameraCompileError::Kind::CircularCatalogReference) {
                        inner.message = "motion '" + std::string{ref->id}
                                      + "' catalog entry failed: "
                                      + inner.message;
                    }
                    leave_scope();
                    return inner;
                }
                auto recursive = std::move(recursive_result).value();
                // CAM-03 fix: extract ONLY the resolved concrete source variant.
                // The outer descriptor's failure_policy, time_dependent flag,
                // and evaluation_dependency MUST be recomputed from the outer
                // descriptor in steps 2-5 below — we must NOT inherit them from
                // the referenced preset.  Fall through to steps 2-5.
                program.descriptor_.source = recursive.descriptor_.source;
            } else {
                // Preset not found in catalog.
                if (!ref->id.empty()) {
                    leave_scope();
                    return CameraCompileError{
                        CameraCompileError::Kind::MotionNotFound,
                        "motion '" + std::string{ref->id}
                            + "' not found in catalog"
                    };
                }
            }
        } else {
            // No catalog available.
            if (!ref->id.empty()) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::MotionNotFound,
                    "motion '" + std::string{ref->id}
                        + "' not found in catalog"
                };
            }
        }
        // If ref->id is empty: no-op (fall through to steps 2-5).

        // source_ member removed in PR12 — the descriptor source is used
        // directly by evaluate_compiled_source() without a separate member.
    } else if (auto* traj = std::get_if<TrajectoryMotion>(&descriptor.source)) {
        if (traj->trajectory && traj->trajectory->size() == 0) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::TrajectoryEmpty,
                "trajectory has zero segments"
            };
        }
        // Trajectory is evaluated directly via evaluate_compiled_source().
    }

    // ── 2. Descriptor-level validation (STEP 8) ────────────────────────
    //
    // Validate the descriptor BEFORE setting compiled state.  Checks are
    // ordered roughly by severity: structural (empty id) → projection
    // (FOV, zoom) → lens (focal, sensor) → motion blur → constraints →
    // trajectory → orientation consistency.
    //
    // Animated values are checked by iterating their keyframe array
    // (not by evaluating at every frame).  Static (no-keyframe) values
    // are checked at their default.

    // ── 2a. Empty ID ─────────────────────────────────────────────────
    if (descriptor.id.empty()) {
        leave_scope();
        return CameraCompileError{
            CameraCompileError::Kind::EmptyId,
            "descriptor.id is empty — every camera descriptor must have a unique id"
        };
    }

    // ── 2b. Projection validation ────────────────────────────────────
    // Check the ProjectionSpec variant for FOV / Zoom sanity.
    // Keyframe values are validated directly; static defaults are checked
    // when there are no keyframes.
    {
        auto check_fov = [](const AnimatedValue<float>& av) -> std::optional<CameraCompileError> {
            if (av.keyframes().empty()) {
                // Static default: evaluate at frame 0.
                float v = av.evaluate(Frame{0});
                if (!std::isfinite(v) || v <= 0.0f || v >= 179.0f) {
                    return CameraCompileError{
                        CameraCompileError::Kind::InvalidFov,
                        "FOV must be finite, positive, and < 179° (got " +
                            std::to_string(v) + ")"
                    };
                }
            } else {
                for (const auto& kf : av.keyframes()) {
                    float v = kf.value;
                    if (!std::isfinite(v) || v <= 0.0f || v >= 179.0f) {
                        return CameraCompileError{
                            CameraCompileError::Kind::InvalidFov,
                            "FOV keyframe at frame " + std::to_string(kf.frame.value) +
                                " is invalid (" + std::to_string(v) +
                                ") — must be finite, positive, and < 179°"
                        };
                    }
                }
            }
            return std::nullopt;
        };
        auto check_zoom = [](const AnimatedValue<float>& av) -> std::optional<CameraCompileError> {
            if (av.keyframes().empty()) {
                float v = av.evaluate(Frame{0});
                if (!std::isfinite(v) || v <= 0.0f) {
                    return CameraCompileError{
                        CameraCompileError::Kind::InvalidZoom,
                        "zoom must be finite and positive (got " +
                            std::to_string(v) + ")"
                    };
                }
            } else {
                for (const auto& kf : av.keyframes()) {
                    float v = kf.value;
                    if (!std::isfinite(v) || v <= 0.0f) {
                        return CameraCompileError{
                            CameraCompileError::Kind::InvalidZoom,
                            "zoom keyframe at frame " + std::to_string(kf.frame.value) +
                                " is invalid (" + std::to_string(v) +
                                ") — must be finite and positive"
                        };
                    }
                }
            }
            return std::nullopt;
        };

        if (auto* fov = std::get_if<FovProjection>(&descriptor.base.projection)) {
            if (auto err = check_fov(fov->fov_deg)) {
                leave_scope();
                return *err;
            }
        } else if (auto* zoom = std::get_if<ZoomProjection>(&descriptor.base.projection)) {
            if (auto err = check_zoom(zoom->zoom)) {
                leave_scope();
                return *err;
            }
        }
    }

    // ── 2c. Lens validation ───────────────────────────────────────────
    {
        const auto& lens = descriptor.base.lens;
        if (lens.focal_length <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::InvalidFocalLength,
                "lens focal_length must be positive (got " +
                    std::to_string(lens.focal_length) + ")"
            };
        }
        if (lens.sensor_width <= 0.0f || lens.sensor_height <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::InvalidSensorDimensions,
                "lens sensor dimensions must be positive (got " +
                    std::to_string(lens.sensor_width) + "×" +
                    std::to_string(lens.sensor_height) + ")"
            };
        }
        if (lens.pixel_aspect <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::InvalidPixelAspect,
                "lens pixel_aspect must be positive (got " +
                    std::to_string(lens.pixel_aspect) + ")"
            };
        }
        if (lens.anamorphic_squeeze <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::InvalidAnamorphicSqueeze,
                "lens anamorphic_squeeze must be positive (got " +
                    std::to_string(lens.anamorphic_squeeze) + ")"
            };
        }
    }

    // ── 2d. Motion blur validation ────────────────────────────────────
    {
        const auto& mb = descriptor.base.motion_blur;
        if (is_motion_blur_active(mb)) {
            if (mb.samples <= 0) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::InvalidMotionBlurSamples,
                    "motion_blur.samples must be positive when motion blur is active (got " +
                        std::to_string(mb.samples) + ")"
                };
            }
            if (mb.shutter_angle_deg <= 0.0f || mb.shutter_angle_deg > 360.0f) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::InvalidShutterAngle,
                    "motion_blur.shutter_angle_deg must be in (0, 360] (got " +
                        std::to_string(mb.shutter_angle_deg) + ")"
                };
            }
        }
    }

    // ── 2e. Constraint validation ─────────────────────────────────────
    for (std::size_t i = 0; i < descriptor.constraints.size(); ++i) {
        if (auto* dist = std::get_if<DistanceConstraint>(&descriptor.constraints[i])) {
            if (dist->min_distance > dist->max_distance) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::InvalidConstraintRange,
                    "constraints[" + std::to_string(i) +
                        "] DistanceConstraint: min_distance (" +
                        std::to_string(dist->min_distance) +
                        ") > max_distance (" +
                        std::to_string(dist->max_distance) + ")"
                };
            }
        }
    }

    // ── 2f. Trajectory validation (supplements existing checks) ──────
    // Use the RESOLVED source (program.descriptor_.source), not the
    // original descriptor.source, so that RegisteredMotionRef→TrajectoryMotion
    // chains are validated after resolution.
    if (auto* traj = std::get_if<TrajectoryMotion>(&program.descriptor_.source)) {
        if (!traj->trajectory) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::TrajectoryNull,
                "TrajectoryMotion source has null trajectory"
            };
        }
        // Validate individual segments.
        const auto& segs = traj->trajectory->segments();
        const auto n_pts = traj->trajectory->points().size();
        for (std::size_t i = 0; i < segs.size(); ++i) {
            const auto& seg = segs[i];
            if (seg.from_idx >= seg.to_idx ||
                seg.from_idx >= n_pts || seg.to_idx >= n_pts) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::InvalidSegmentIndex,
                    "trajectory segment[" + std::to_string(i) +
                        "] has invalid indices: from_idx=" +
                        std::to_string(seg.from_idx) + " to_idx=" +
                        std::to_string(seg.to_idx) +
                        " (trajectory has " + std::to_string(n_pts) + " points)"
                };
            }
            if (seg.duration_frames <= 0.0f) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::InvalidSegmentDuration,
                    "trajectory segment[" + std::to_string(i) +
                        "] has non-positive duration: " +
                        std::to_string(seg.duration_frames)
                };
            }
        }
    }

    // ── 2g. Orientation consistency ───────────────────────────────────
    // Use the RESOLVED source so RegisteredMotionRef chains are checked
    // against the concrete variant (not the still-unresolved ref).
    if (std::holds_alternative<OrientAlongPath>(descriptor.orientation)) {
        if (!std::holds_alternative<TrajectoryMotion>(program.descriptor_.source)) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::OrientAlongPathWithoutTrajectory,
                "OrientAlongPath orientation requires a TrajectoryMotion source, "
                "but the descriptor source is not TrajectoryMotion"
            };
        }
    }
    if (auto* lal = std::get_if<LookAtLayer>(&descriptor.orientation)) {
        if (lal->target.empty()) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::LookAtLayerWithoutTarget,
                "LookAtLayer orientation has an empty target string — "
                "must name a layer for world-position resolution"
            };
        }
    }

    // ── 3. Set failure policy ────────────────────────────────────────────
    program.failure_policy_ = descriptor.failure_policy;

    // ── 3. Compute time_dependent flag ───────────────────────────────────
    // Conservative: any non-static source is assumed time-dependent.
    // Any modifier also makes the camera time-dependent (e.g. IdleOscillation).
    // CAM-03: use the RESOLVED source (program.descriptor_.source), not the
    // original descriptor's source (which may still be RegisteredMotionRef
    // after step 1 resolved it to a concrete variant).
    bool source_is_static =
        std::holds_alternative<StaticCameraSource>(program.descriptor_.source);
    bool has_modifiers = !descriptor.modifiers.empty();
    program.time_dependent_ = !source_is_static || has_modifiers;

    // ── 4. CAM-02 — compute evaluation_dependency metadata ─────────────
    // Default heuristic: a program is STATELESS unless an explicit
    // stateful-constraint marker forces RequiresHistory.  All pure
    // sources (StaticCameraSource / PoseTracksSource / OrbitMotion /
    // TrajectoryMotion) and pure modifiers (IdleOscillation,
    // HandheldNoise) are Stateless — their evaluation is a pure function
    // of (descriptor, CameraEvalContext).  All non-stateful constraint
    // types (LookAt, KeepHorizon, Distance, RotationLimit) are Stateless.
    program.evaluation_dependency_ = CameraEvaluationDependency::Stateless;

    // TICKET-A3-DAMPED-HISTORY (Agent3 mission DoD gate (b)) —
    // FORCE-override: DampedFollowConstraint is the SOLE constraint that
    // maintains per-frame EMA state across calls (ConstraintState with
    // previous_camera + previous_velocity + previous_time + has_previous).
    // Identity-preserving evaluation with the SAME CameraSession is
    // required for deterministic state accumulation, which is why this
    // constraint type forces RequiresHistory regardless of any other
    // indicator (source variant, modifier list, other constraint types).
    //
    // Force-override pattern: default is Stateless, then monotonic
    // promotion to RequiresHistory if any constraint is a
    // DampedFollowConstraint.  The break-on-first-match keeps the loop
    // O(1) once a DampedFollow is encountered (the assignment is the
    // actual codepath; subsequent iterations are no-ops).
    for (const auto& c : descriptor.constraints) {
        if (std::holds_alternative<DampedFollowConstraint>(c)) {
            program.evaluation_dependency_ =
                CameraEvaluationDependency::RequiresHistory;
            break;
        }
    }

    // ── 5. Mark as compiled and return ───────────────────────────────────
    program.compiled_ = true;
    leave_scope();
    return program;
}

// =============================================================================
// builtin_camera_presets() — all 19 legacy presets as CameraDescriptors.
//
// These CameraDescriptors use PoseTracksSource or OrbitMotion directly as
// their source, so compile_camera() can evaluate them without any registry.
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
