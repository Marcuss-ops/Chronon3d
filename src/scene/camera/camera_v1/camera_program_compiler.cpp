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
//   - The compiled_ flag
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
//
// FASE 6 — builtin_camera_presets() extracted to camera_builtin_presets.cpp.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
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
            CameraCompileError::Kind::CircularCatalogReference,
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
                    if (inner.kind != CameraCompileError::Kind::CircularCatalogReference) {
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
            if (!ref->id.empty()) {
                leave_scope();
                return CameraCompileError{
                    CameraCompileError::Kind::MotionNotFound,
                    "motion '" + std::string{ref->id}
                        + "' not found in catalog"
                };
            }
        }
    } else if (auto* traj = std::get_if<TrajectoryMotion>(&descriptor.source)) {
        if (traj->trajectory && traj->trajectory->size() == 0) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::TrajectoryEmpty,
                "trajectory has zero segments"
            };
        }
    }

    // ── 2. Descriptor-level validation (STEP 8) ────────────────────────
    // ── 2a. Empty ID ─────────────────────────────────────────────────
    if (descriptor.id.empty()) {
        leave_scope();
        return CameraCompileError{
            CameraCompileError::Kind::EmptyId,
            "descriptor.id is empty — every camera descriptor must have a unique id"
        };
    }

    // ── 2b. Projection validation ────────────────────────────────────
    {
        auto check_fov = [](const AnimatedValue<float>& av) -> std::optional<CameraCompileError> {
            if (av.keyframes().empty()) {
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
    if (auto* traj = std::get_if<TrajectoryMotion>(&program.descriptor_.source)) {
        if (!traj->trajectory) {
            leave_scope();
            return CameraCompileError{
                CameraCompileError::Kind::TrajectoryNull,
                "TrajectoryMotion source has null trajectory"
            };
        }
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
    bool source_is_static =
        std::holds_alternative<StaticCameraSource>(program.descriptor_.source);
    bool has_modifiers = !descriptor.modifiers.empty();
    program.time_dependent_ = !source_is_static || has_modifiers;

    // ── 4. CAM-02 — compute evaluation_dependency metadata ─────────────
    program.evaluation_dependency_ = CameraEvaluationDependency::Stateless;

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

} // namespace chronon3d::camera_v1
