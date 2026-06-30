#pragma once

// ============================================================================
// include/chronon3d/timeline/compile_evaluate.hpp
//
// P3-D (V0.2 timeline staging) — explicit `compile_composition()`,
// `compile_camera()`, and `evaluate()` free functions in `namespace chronon3d`.
//
// DESIGN RULE:  Compile is separated from evaluate.  Neither function injects
// mutable state into `chronon3d::Composition` or `CompositionDefinition`.  Same
// `(definition, compile_context)` inputs always produce a value-equal
// `CompiledComposition`; same `(compiled, evaluate_context, frame)` inputs
// always produce a value-equal `EvaluatedCompositionFrame`.
//
// IMMUTABILITY CONTRACT:  `compile_camera()` produces a
// `camera_v1::CameraProgram` that is treated as immutable downstream.  The
// downstream `CompiledComposition::camera_program` is intentionally typed
// `shared_ptr<const camera_v1::CameraProgram>` (see
// `<chronon3d/timeline/compiled_composition.hpp>`) so the contract holds at
// the type level: no syntactic mutation is possible.
//
// Sits NEXT TO the legacy `chronon3d::Composition` class — does NOT replace or
// migrate it.  V2 promotion closes the duplication in a later PR.
//
// Bodies live out-of-line in `src/timeline/compile_evaluate.cpp` so the heavy
// `camera_v1/camera_program_compiler.hpp + spdlog` translation-unit cost is
// paid ONCE per binary, not once per scheduling TU that includes this header.
// ============================================================================

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::CompositionCompileContext
//   Per-call compile context threaded into compile_composition() and compile_camera().
//   Held by value; non-owning.
// ─────────────────────────────────────────────────────────────────────────────
struct CompositionCompileContext {
    // Wall-clock at the time the compile_composition() call started.
    // Used purely for diagnostics; the compile_camera() inner call does NOT
    // depend on it (camera_v1 cycle-detection state is allocated fresh per
    // compile_camera() invocation).
    std::chrono::system_clock::time_point compiled_at =
        std::chrono::system_clock::now();
};

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::CompositionCompileError — POD error type returned by
// compile_composition() and compile_camera().
// ─────────────────────────────────────────────────────────────────────────────
struct CompositionCompileError {
    enum class Kind : std::uint8_t {
        EmptyCompositionSpec = 0,
        NoSceneFunction      = 1,
        CameraFailure        = 2,
        NotImplementedYet    = 3,
        Internal             = 4,
    };
    Kind        kind = Kind::Internal;
    std::string message{};
};

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::CompositionEvaluateContext
//   Per-call evaluation context threaded into evaluate().  Bundles a
//   FrameContext so the scene function can materialise a Scene without the
//   evaluate() signature becoming a moving target (future anti-duplication
//   rules forbid adding tertiary lookup args to the public signature).
// ─────────────────────────────────────────────────────────────────────────────
struct CompositionEvaluateContext {
    FrameContext frame_context{};
};

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::CompositionEvaluateError — POD error type returned by evaluate().
// ─────────────────────────────────────────────────────────────────────────────
struct CompositionEvaluateError {
    enum class Kind : std::uint8_t {
        NullCompiledComposition = 0,
        NullDefinition           = 1,
        NullSceneFunction        = 2,
        SceneBuildFailed         = 3,
        NotImplementedYet        = 4,
        Internal                 = 5,
    };
    Kind        kind = Kind::Internal;
    std::string message{};
};

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::compile_camera()  —  canonical camera compile path.
//
//   Returns the immutable `camera_v1::CameraProgram` on success.  On failure
//   returns a `CompositionCompileError` tagged `CameraFailure` (the camera_v1
//   inner error payload is condensed to a single message in this staging pass;
//   the V2 promotion tier will carry the structured
//   `camera_v1::CameraCompileError` alongside the composition error so callers
//   can dispatch on `Kind::MotionNotFound`, `Kind::CircularCatalogReference`,
//   etc.).
// ─────────────────────────────────────────────────────────────────────────────
Result<camera_v1::CameraProgram, CompositionCompileError>
compile_camera(const camera_v1::CameraDescriptor& descriptor,
               const CompositionCompileContext& context);

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::compile_composition()  —  canonical V2 pipeline entrypoint.
//
//   Pure: identical `(definition, compile_context)` inputs always produce
//   equivalent `CompiledComposition` values (same fingerprint, same camera
//   program).  The caller retains lifetime ownership of `definition`; this
//   function holds it via a non-owning no-deleter `shared_ptr<const ...>`
//   so downstream `evaluate()` calls can dereference it cheaply.
// ─────────────────────────────────────────────────────────────────────────────
Result<CompiledComposition, CompositionCompileError>
compile_composition(const CompositionDefinition& definition,
                    const CompositionCompileContext& context);

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::evaluate()  —  canonical V2 per-frame evaluation entrypoint.
//
//   Pure: identical `(compiled, evaluate_context, frame)` inputs always produce
//   equivalent `EvaluatedCompositionFrame` values.  `frame` threads into
//   `FrameContext::frame` before invoking the captured `SceneFunction`.
//
// OUT OF SCOPE for this commit (future V2 PR):  Camera2_5D resolution by
// forwarding through `CameraProgram::evaluate()`.  The current implementation
// sets `EvaluatedCompositionFrame::camera = std::nullopt` because the camera
// evaluation pipeline is still wired through the legacy `Composition` path.
// ─────────────────────────────────────────────────────────────────────────────
Result<EvaluatedCompositionFrame, CompositionEvaluateError>
evaluate(const CompiledComposition& compiled,
         const CompositionEvaluateContext& context,
         Frame frame);

} // namespace chronon3d
