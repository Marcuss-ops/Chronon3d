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
//
// CAM-02: cycle detection in RegisteredMotionRef resolution is threaded
// through the compiler via CameraCompileContext (a stack-allocated
// per-call struct carrying a visited-id set + resolution_stack for
// diagnostics).  The 2-arg public overload constructs a fresh
// CameraCompileContext per call so each top-level compile_camera() is
// cycle-clean.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>
#include <chronon3d/core/types/result.hpp>

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace chronon3d::camera_v1 {

// =============================================================================
// CameraCompileError — structured error from compile_camera().
// =============================================================================

struct CameraCompileError {
    enum class Kind : std::uint8_t {
        Unknown = 0,
        MotionNotFound,            // RegisteredMotionRef id not in catalog
        InvalidSource,             // source is empty / misconfigured
        TrajectoryEmpty,           // trajectory has no segments
        TrajectoryDurationZero,    // all segments have zero duration
        ConstraintNotFound,        // named constraint id not in registry
        CircularParent,            // parent hierarchy cycle
        CircularCatalogReference,  // CAM-02: RegisteredMotionRef chain loops back
    };

    Kind        kind{Kind::Unknown};
    std::string message;
};

// =============================================================================
// CameraCompileContext — CAM-02 cycle-detection state.
//
// A stack-allocated, per-compile_camera()-call struct that threads:
//   - visited_ids        : set of descriptor-ids currently being resolved
//                          (used for cycle detection; ids are inserted on
//                          entry and erased on exit so the same id may
//                          correctly be resolved across sibling paths).
//   - resolution_stack   : chronological path of ids in the current
//                          resolution chain, popped on exit; populated
//                          for diagnostic messages on cycle.
//
// The context is passed by reference to the 3-arg compile_camera() overload.
// The 2-arg public overload constructs a fresh context and forwards.
// =============================================================================

struct CameraCompileContext {
    // LIFETIME INVARIANT (CAM-02 fix-set): visited_ids + resolution_stack
    // store string_views that alias the backing std::string of
    // descriptor.id.  compile_camera() runs synchronously and all
    // descriptors in the traversal chain MUST outlive the call.  For
    // lenient-lifetime use, replace visits_ids / resolution_stack with
    // the corresponding std::string containers (clone).
    std::unordered_set<std::string_view> visited_ids;
    std::vector<std::string_view>        resolution_stack;
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
//
// CAM-02 overload: callers that want to detect circular RegisteredMotionRef
// chains may thread a CameraCompileContext through.  The 3-arg overload is
// the canonical implementation; the 2-arg overload is an inline forwarder
// that constructs a fresh context.
[[nodiscard]] chronon3d::Result<CameraProgram, CameraCompileError>
compile_camera(const CameraDescriptor& descriptor,
               const CameraCatalog* catalog,
               CameraCompileContext& ctx);

[[nodiscard]] inline chronon3d::Result<CameraProgram, CameraCompileError>
compile_camera(const CameraDescriptor& descriptor,
               const CameraCatalog* catalog = nullptr) {
    CameraCompileContext ctx;
    return compile_camera(descriptor, catalog, ctx);
}

} // namespace chronon3d::camera_v1
