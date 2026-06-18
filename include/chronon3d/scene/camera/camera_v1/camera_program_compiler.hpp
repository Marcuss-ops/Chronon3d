#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp
//
// CameraProgram compiler — compiles a CameraDescriptor into an immutable
// CameraProgram that can be evaluated without registry lookups.
//
// The compiler:
//   1. Resolves RegisteredMotionRef through the CameraCatalog
//   2. Validates the source (durations, keyframes, trajectory bounds)
//   3. Pre-computes arc-length tables for trajectories
//   4. Assigns constraint state slots
//   5. Flags whether the camera is time-dependent (for caching)
//   6. Produces a compiled CameraProgram with zero registry lookup in evaluate()
//
// Returns failure info via CameraCompileError (output-parameter pattern).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>

#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

// =============================================================================
// CameraCompileError — structured error from compile_camera().
// =============================================================================

struct CameraCompileError {
    enum class Kind : std::uint8_t {
        Unknown = 0,
        MotionNotFound,       // RegisteredMotionRef id not in catalog
        InvalidSource,        // source is empty / misconfigured
        TrajectoryEmpty,      // trajectory has no segments
        TrajectoryDurationZero, // all segments have zero duration
        ConstraintNotFound,   // named constraint id not in registry
        CircularParent,       // parent hierarchy cycle
    };

    Kind        kind{Kind::Unknown};
    std::string message;
};

// =============================================================================
// compile_camera() — compile a CameraDescriptor into a CameraProgram.
//
// Returns true on success.  On failure sets *out_error (if non-null) and
// returns false.  Uses output-parameter pattern because std::expected is
// not available in C++20.
// =============================================================================

/// Compile a CameraDescriptor into a ready-to-evaluate CameraProgram.
///
/// @param descriptor  The authoring-time camera description.
/// @param out_program [out] Receives the compiled CameraProgram on success.
/// @param out_error   [out] Receives the error info on failure (optional).
/// @param catalog     Optional catalog for resolving RegisteredMotionRef.
///                    If null / empty, a RegisteredMotionRef will fail to compile.
///
/// @return true on success, false on failure (with *out_error populated).
///
/// The returned program is immutable and thread-safe:
///   - No registry lookups in evaluate()
///   - No mutex acquisitions
///   - No heap allocations (except for trajectory sampling)
///   - No string construction (except for diagnostics)
bool compile_camera(const CameraDescriptor& descriptor,
                    CameraProgram& out_program,
                    CameraCompileError* out_error = nullptr,
                    const CameraCatalog* catalog = nullptr);

} // namespace chronon3d::camera_v1
