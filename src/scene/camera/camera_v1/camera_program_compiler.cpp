// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program_compiler.cpp
//
// CameraProgram compiler implementation.
//
// TICKET-PHASE-2 (Phase 2 refactor):
//   - compile_camera() is the SOLE point that validates the descriptor,
//     resolves presets, detects cycles, compiles source/orientation/modifier/
//     constraint, builds metadata, calculates the fingerprint, classifies
//     evaluation dependency, and chooses the failure policy.
//   - After every graft (including RegisteredMotionRef resolution), ALL 5
//     metadata fields are re-populated at the END of the function:
//       failure_policy_, time_dependent_, evaluation_dependency_,
//       program_kind_, fingerprint_.
//   - The previous nested `CameraCompileError::Kind` enum (20+ values) is
//     replaced by the top-level `CameraCompileErrorCode` (11 values) defined
//     in camera_program.hpp.
//   - No silent fallback: every RegisteredMotionRef either resolves to a
//     concrete source, or emits a structured compile error.
//   - Non-finite numeric values emit `NonFiniteValue` (the field-specific
//     range error is emitted only after the value is verified finite).
//   - The schema-version constant `kCameraProgramSchemaVersion` is mixed
//     into the fingerprint so pre-Phase-2 fingerprints are auto-invalidated.
//
// CAM-02 cycle detection: the 3-arg compile_camera() overload threads a
// CameraCompileContext (visited_ids + resolution_stack) for circular-
// RegisteredMotionRef chain detection.  The 2-arg public overload is an
// inline forwarder that constructs a fresh context.
//
// FASE 6 — builtin_camera_presets() extracted to camera_builtin_presets.cpp.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>
#include "camera_builtin_presets.hpp"

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
            CameraCompileErrorCode::CircularPresetReference,
            "circular catalog reference: " + path
        };
    }
    ctx.resolution_stack.push_back(descriptor.id);

    CameraProgram program;
    program.descriptor_ = descriptor;

    // ── 1. Resolve source ────────────────────────────────────────────────
    if (auto* ref = std::get_if<RegisteredMotionRef>(&descriptor.source)) {
        if (catalog) {
            auto* preset_desc = catalog->find_descriptor(ref->id);
            if (preset_desc) {
                auto recursive_result =
                    compile_camera(*preset_desc, catalog, ctx);
                if (!recursive_result) {
                    auto inner = recursive_result.error();
                    if (inner.code != CameraCompileErrorCode::CircularPresetReference) {
                        inner.message = "motion '" + std::string{ref->id}
                                      + "' catalog entry failed: "
                                      + inner.message;
                    }
                    leave_scope();
                    return inner;
                }
                auto recursive = std::move(recursive_result).value();
                program.descriptor_.source = recursive.descriptor_.source;
            } else {
                // TICKET-PHASE-2: NO silent fallback.  Either we resolve
                // the ref, or we emit a structured error.
                if (ref->id.empty()) {
                    leave_scope();
                    return CameraCompileError{
                        CameraCompileErrorCode::InvalidDescriptor,
                        "RegisteredMotionRef has empty id — must name a preset in the catalog"
                    };
                }
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::MissingPreset,
                    "motion '" + std::string{ref->id}
                        + "' not found in catalog"
                };
            }
        } else {
            // TICKET-PHASE-2: NO silent fallback.  A RegisteredMotionRef
            // with a non-null descriptor.source AND a null catalog is a
            // configuration error: the ref can never be resolved.
            if (ref->id.empty()) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::InvalidDescriptor,
                    "RegisteredMotionRef has empty id — must name a preset in the catalog"
                };
            }
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::MissingPreset,
                "motion '" + std::string{ref->id}
                    + "' not found in catalog (catalog is null)"
            };
        }
    } else if (auto* traj = std::get_if<TrajectoryMotion>(&descriptor.source)) {
        if (traj->trajectory && traj->trajectory->size() == 0) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::InvalidTrajectory,
                "trajectory has zero segments"
            };
        }
    }

    // ── 2. Descriptor-level validation (STEP 8 + Phase 2 NonFiniteValue) ─
    // ── 2a. Empty ID ─────────────────────────────────────────────────
    if (descriptor.id.empty()) {
        leave_scope();
        return CameraCompileError{
            CameraCompileErrorCode::InvalidDescriptor,
            "descriptor.id is empty — every camera descriptor must have a unique id"
        };
    }

    // ── 2b. Projection validation (FOV / Zoom) ──────────────────────
    {
        // TICKET-PHASE-2: NonFiniteValue pre-check fires BEFORE the
        // field-specific range check.  A NaN/Inf FOV emits NonFiniteValue
        // (not InvalidProjection) so callers can branch on numeric
        // corruption vs. configuration error.
        auto check_fov = [](const AnimatedValue<float>& av) -> std::optional<CameraCompileError> {
            if (av.keyframes().empty()) {
                float v = av.evaluate(Frame{0});
                if (!std::isfinite(v)) {
                    return CameraCompileError{
                        CameraCompileErrorCode::NonFiniteValue,
                        "FOV is not finite (got " + std::to_string(v) + ")"
                    };
                }
                if (v <= 0.0f || v >= 179.0f) {
                    return CameraCompileError{
                        CameraCompileErrorCode::InvalidProjection,
                        "FOV must be positive, and < 179\u00b0 (got " +
                            std::to_string(v) + ")"
                    };
                }
            } else {
                for (const auto& kf : av.keyframes()) {
                    float v = kf.value;
                    if (!std::isfinite(v)) {
                        return CameraCompileError{
                            CameraCompileErrorCode::NonFiniteValue,
                            "FOV keyframe at frame " + std::to_string(kf.frame.integral()) +
                                " is not finite (got " + std::to_string(v) + ")"
                        };
                    }
                    if (v <= 0.0f || v >= 179.0f) {
                        return CameraCompileError{
                            CameraCompileErrorCode::InvalidProjection,
                            "FOV keyframe at frame " + std::to_string(kf.frame.integral()) +
                                " is invalid (" + std::to_string(v) +
                                ") \u2014 must be positive, and < 179\u00b0"
                        };
                    }
                }
            }
            return std::nullopt;
        };
        auto check_zoom = [](const AnimatedValue<float>& av) -> std::optional<CameraCompileError> {
            if (av.keyframes().empty()) {
                float v = av.evaluate(Frame{0});
                if (!std::isfinite(v)) {
                    return CameraCompileError{
                        CameraCompileErrorCode::NonFiniteValue,
                        "zoom is not finite (got " + std::to_string(v) + ")"
                    };
                }
                if (v <= 0.0f) {
                    return CameraCompileError{
                        CameraCompileErrorCode::InvalidProjection,
                        "zoom must be positive (got " + std::to_string(v) + ")"
                    };
                }
            } else {
                for (const auto& kf : av.keyframes()) {
                    float v = kf.value;
                    if (!std::isfinite(v)) {
                        return CameraCompileError{
                            CameraCompileErrorCode::NonFiniteValue,
                            "zoom keyframe at frame " + std::to_string(kf.frame.integral()) +
                                " is not finite (got " + std::to_string(v) + ")"
                        };
                    }
                    if (v <= 0.0f) {
                        return CameraCompileError{
                            CameraCompileErrorCode::InvalidProjection,
                            "zoom keyframe at frame " + std::to_string(kf.frame.integral()) +
                                " is invalid (" + std::to_string(v) +
                                ") \u2014 must be positive"
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

    // ── 2c. Lens validation (TICKET-PHASE-2: NonFiniteValue pre-check) ──
    {
        const auto& lens = descriptor.base.lens;
        // TICKET-PHASE-2: NonFiniteValue fires before the field-specific
        // InvalidLens check.  A NaN/Inf focal_length emits NonFiniteValue
        // (not InvalidLens) so the cache invalidation contract can
        // distinguish numeric corruption from configuration error.
        if (!std::isfinite(lens.focal_length)) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::NonFiniteValue,
                "lens focal_length is not finite (got " +
                    std::to_string(lens.focal_length) + ")"
            };
        }
        if (lens.focal_length <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::InvalidLens,
                "lens focal_length must be positive (got " +
                    std::to_string(lens.focal_length) + ")"
            };
        }
        if (!std::isfinite(lens.sensor_width) || !std::isfinite(lens.sensor_height)) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::NonFiniteValue,
                "lens sensor dimensions are not finite (got " +
                    std::to_string(lens.sensor_width) + "\u00d7" +
                    std::to_string(lens.sensor_height) + ")"
            };
        }
        if (lens.sensor_width <= 0.0f || lens.sensor_height <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::InvalidLens,
                "lens sensor dimensions must be positive (got " +
                    std::to_string(lens.sensor_width) + "\u00d7" +
                    std::to_string(lens.sensor_height) + ")"
            };
        }
        if (!std::isfinite(lens.pixel_aspect)) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::NonFiniteValue,
                "lens pixel_aspect is not finite (got " +
                    std::to_string(lens.pixel_aspect) + ")"
            };
        }
        if (lens.pixel_aspect <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::InvalidLens,
                "lens pixel_aspect must be positive (got " +
                    std::to_string(lens.pixel_aspect) + ")"
            };
        }
        if (!std::isfinite(lens.anamorphic_squeeze)) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::NonFiniteValue,
                "lens anamorphic_squeeze is not finite (got " +
                    std::to_string(lens.anamorphic_squeeze) + ")"
            };
        }
        if (lens.anamorphic_squeeze <= 0.0f) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::InvalidLens,
                "lens anamorphic_squeeze must be positive (got " +
                    std::to_string(lens.anamorphic_squeeze) + ")"
            };
        }
    }

    // ── 2d. Motion blur validation (TICKET-PHASE-2: NonFiniteValue) ──
    {
        const auto& mb = descriptor.base.motion_blur;
        if (is_motion_blur_active(mb)) {
            if (!std::isfinite(static_cast<float>(mb.samples))) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::NonFiniteValue,
                    "motion_blur.samples is not finite (got " +
                        std::to_string(mb.samples) + ")"
                };
            }
            if (mb.samples <= 0) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::InvalidLens,
                    "motion_blur.samples must be positive when motion blur is active (got " +
                        std::to_string(mb.samples) + ")"
                };
            }
            if (!std::isfinite(mb.shutter_angle_deg)) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::NonFiniteValue,
                    "motion_blur.shutter_angle_deg is not finite (got " +
                        std::to_string(mb.shutter_angle_deg) + ")"
                };
            }
            if (mb.shutter_angle_deg <= 0.0f || mb.shutter_angle_deg > 360.0f) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::InvalidLens,
                    "motion_blur.shutter_angle_deg must be in (0, 360] (got " +
                        std::to_string(mb.shutter_angle_deg) + ")"
                };
            }
        }
    }

    // ── 2e. Constraint validation ─────────────────────────────────────
    for (std::size_t i = 0; i < descriptor.constraints.size(); ++i) {
        if (auto* dist = std::get_if<DistanceConstraint>(&descriptor.constraints[i])) {
            if (!std::isfinite(dist->min_distance) || !std::isfinite(dist->max_distance)) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::NonFiniteValue,
                    "constraints[" + std::to_string(i) +
                        "] DistanceConstraint: min_distance or max_distance is not finite"
                };
            }
            if (dist->min_distance > dist->max_distance) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::InvalidConstraint,
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
    if (auto* traj = std::get_if<TrajectoryMotion>(&program.descriptor_.source)) {
        if (!traj->trajectory) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::InvalidTrajectory,
                "TrajectoryMotion source has null trajectory"
            };
        }
        const auto& segs = traj->trajectory->segments();
        const auto n_pts = traj->trajectory->points().size();
        for (std::size_t i = 0; i < segs.size(); ++i) {
            const auto& seg = segs[i];
            // TICKET-120: Hold segments have from_idx == to_idx (same
            // point) — allowed. Other segment kinds must have
            // from_idx < to_idx (forward direction).
            const bool hold_segment = (seg.kind == SegmentKind::Hold);
            if ((!hold_segment && seg.from_idx >= seg.to_idx) ||
                seg.from_idx >= n_pts || seg.to_idx >= n_pts) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::InvalidSegmentIndex,
                    "trajectory segment[" + std::to_string(i) +
                        "] has invalid indices: from_idx=" +
                        std::to_string(seg.from_idx) + " to_idx=" +
                        std::to_string(seg.to_idx) +
                        " (trajectory has " + std::to_string(n_pts) + " points)"
                };
            }
            if (!std::isfinite(seg.duration_frames)) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::NonFiniteValue,
                    "trajectory segment[" + std::to_string(i) +
                        "] has non-finite duration: " +
                        std::to_string(seg.duration_frames)
                };
            }
            if (seg.duration_frames <= 0.0f) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileErrorCode::InvalidTrajectory,
                    "trajectory segment[" + std::to_string(i) +
                        "] has non-positive duration: " +
                        std::to_string(seg.duration_frames)
                };
            }
        }
    }

    // ── 2g. Orientation consistency ───────────────────────────────────
    // TICKET-120: OrientAlongPath works with any source type — when the
    // source has no tangent (e.g. StaticCameraSource), the evaluator
    // falls back through a 4-step chain at runtime:
    //   1. current-frame tangent (TrajectoryMotion / PoseTracks)
    //   2. session.last_tangent (persisted from a prior frame)
    //   3. point-of-interest direction
    //   4. base descriptor rotation
    // The compile-time restriction that required a TrajectoryMotion
    // source was overly restrictive (removed).
    if (auto* lal = std::get_if<LookAtLayer>(&descriptor.orientation)) {
        if (lal->target.empty()) {
            leave_scope();
            return CameraCompileError{
                CameraCompileErrorCode::MissingTarget,
                "LookAtLayer orientation has an empty target string \u2014 "
                "must name a layer for world-position resolution"
            };
        }
    }

    // ── 3-5. TICKET-PHASE-2: set ALL 5 metadata fields AT THE END of
    // the function (after every graft and every validation).  The
    // previous middle-block was removed to enforce the Phase 2
    // "always re-populate after every graft" invariant: no early
    // return may leave stale metadata.
    program.failure_policy_ = program.descriptor_.failure_policy;

    {
        const bool source_is_static =
            std::holds_alternative<StaticCameraSource>(program.descriptor_.source);
        const bool has_modifiers = !program.descriptor_.modifiers.empty();
        program.time_dependent_ = !source_is_static || has_modifiers;
    }

    program.evaluation_dependency_ = CameraEvaluationDependency::Stateless;
    for (const auto& c : program.descriptor_.constraints) {
        if (std::holds_alternative<DampedFollowConstraint>(c)) {
            program.evaluation_dependency_ =
                CameraEvaluationDependency::RequiresHistory;
            break;
        }
    }

    // TICKET-PHASE-2: program_kind_ = resolved source discriminator.
    // Re-populated here AFTER the RegisteredMotionRef graft so the kind
    // reflects the FINAL source, not the declared one.
    program.program_kind_ = [](const CameraSourceSpec& s) -> CameraProgramKind {
        if (std::holds_alternative<StaticCameraSource>(s))   return CameraProgramKind::Static;
        if (std::holds_alternative<PoseTracksSource>(s))     return CameraProgramKind::PoseTracks;
        if (std::holds_alternative<OrbitMotion>(s))          return CameraProgramKind::Orbit;
        if (std::holds_alternative<TrajectoryMotion>(s))     return CameraProgramKind::Trajectory;
        if (std::holds_alternative<CameraMotionParamsSource>(s)) return CameraProgramKind::CameraMotionParams;
        return CameraProgramKind::Ref;  // RegisteredMotionRef (only on failure paths)
    }(program.descriptor_.source);

    // TICKET-PHASE-2: fingerprint_ = FNV-1a hash of the finalised descriptor.
    // Re-populated here AFTER every graft so cache lookups see a hash
    // that reflects the actual final state.  The schema-version sentinel
    // mixed in by compute_camera_descriptor_fingerprint() invalidates
    // pre-Phase-2 fingerprints automatically.
    program.fingerprint_ = compute_camera_descriptor_fingerprint(program.descriptor_);

    // ── 6. Mark as compiled and return ───────────────────────────────────
    program.compiled_ = true;
    leave_scope();
    return program;
}

} // namespace chronon3d::camera_v1
