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
// Returns Result<CameraProgram, CameraCompileError>.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>
#include <chronon3d/core/types/result.hpp>

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
// Returns Result<CameraProgram, CameraCompileError>:
//   - On success: the compiled CameraProgram (implicitly from CameraProgram&&).
//   - On failure: a CameraCompileError (implicitly from CameraCompileError&&).
//
// Usage:
//   auto result = compile_camera(descriptor, &catalog);
//   if (!result) { return result.error(); }
//   auto program = std::move(result).value();
//
//   // Or with TRY:
//   auto program = CHRONON_TRY(compile_camera(descriptor, &catalog));
//
// The returned program is immutable and thread-safe:
//   - No registry lookups in evaluate()
//   - No mutex acquisitions
//   - No heap allocations (except for trajectory sampling)
//   - No string construction (except for diagnostics)
[[nodiscard]] chronon3d::Result<CameraProgram, CameraCompileError>
compile_camera(const CameraDescriptor& descriptor,
               const CameraCatalog* catalog = nullptr);

} // namespace chronon3d::camera_v1
