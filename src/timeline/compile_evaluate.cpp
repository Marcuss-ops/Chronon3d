// ============================================================================
// src/timeline/compile_evaluate.cpp
//
// Bodies for the three free functions declared in
// `<chronon3d/timeline/compile_evaluate.hpp>`:
//
//   * chronon3d::compile_composition(...)
//   * chronon3d::evaluate(...)
//
// The timeline invokes its scene callback and transports the resolved Scene.
// Camera authoring and evaluation belong to the upstream motion system.
// ============================================================================

#include <chronon3d/timeline/compile_evaluate.hpp>

// Bodies-only headers — kept out of the public API to keep surface includes
// transitively minimal.
#include <chronon3d/core/hash/hash_builder.hpp>

#include <exception>

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// Composition fingerprint (file-scope helper).
//   Hash fields in declaration order. XOR is intentionally avoided because it
//   is commutative and makes reordered fields share the same digest. The scene
//   callback's captures are represented by the explicit content fingerprint;
//   std::function itself has no portable capture introspection API.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
std::uint64_t fingerprint_composition(
    const Composition& composition, bool scene_is_frame_invariant = false) {
    auto hash = core::hash::HashBuilder{}
        .add("chronon3d.composition.fingerprint.v2")
        .add(composition.name())
        .add(composition.width())
        .add(composition.height())
        .add(composition.frame_rate().numerator)
        .add(composition.frame_rate().denominator)
        .add(composition.duration())
        .add(composition.scene_content_fingerprint())
        .add(scene_is_frame_invariant);
    return hash.finish();
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// compile_composition()
//
// V2 contract — no mutable state.  Same `(definition, context)` inputs always
// produce a value-equal `CompiledComposition` (same fingerprint).
//
// Construction order:
//   1. Sanity-check CompositionSpec (non-empty name + positive dims).
//   2. Sanity-check SceneFunction presence.
//   3. Capture `definition` into a non-owning ref-counted shared_ptr (the
//      caller retains lifetime ownership).
//   4. Compute a deterministic fingerprint over the CompositionSpec and
//      explicit scene-content identity. The callback object itself is not hashed.
// ─────────────────────────────────────────────────────────────────────────────
Result<CompiledComposition, CompositionCompileError>
compile_composition(const Composition& composition,
                    const CompositionCompileContext& /*context*/) {
    // (1) CompositionSpec sanity.
    if (composition.name().empty() ||
        composition.width() <= 0 ||
        composition.height() <= 0) {
        CompositionCompileError err;
        err.kind    = CompositionCompileError::Kind::EmptyCompositionSpec;
        err.message = "CompositionSpec has empty name or non-positive dimensions";
        return err;
    }

    // (2) SceneFunction presence.
    if (!composition.scene_function()) {
        CompositionCompileError err;
        err.kind    = CompositionCompileError::Kind::NoSceneFunction;
        err.message = "Composition::SceneFunction is null";
        return err;
    }

    CompiledComposition out;

    // (3) Own a snapshot of the canonical Composition.
    out.composition = std::make_shared<const Composition>(composition);
    // (4) Scene execution mode resolution.
    // SceneFunction is a per-frame API.  Static topology is represented by
    // the separate compiled frame-program path; there is no static Scene
    // materialization mode in the canonical Composition adapter.
    out.execution_mode = SceneExecutionMode::DynamicCallback;
    out.template_scene = nullptr;
    out.is_static_topology = false;

    // (5) Deterministic fingerprint of the canonical Composition.
    out.fingerprint = fingerprint_composition(composition);

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// evaluate()
//
// V2 staging — pure.  Threads `frame` into `FrameContext::frame`, calls the
// captured `CompositionDefinition::scene` lambda, catches any exception as a
// `CompositionEvaluateError::SceneBuildFailed`.
//
// Camera ownership: SceneFunction returns resolved camera state as part of
// Scene. Timeline evaluation does not author or reinterpret that state.
// ─────────────────────────────────────────────────────────────────────────────
Result<EvaluatedCompositionFrame, CompositionEvaluateError>
evaluate(const CompiledComposition& compiled,
         const CompositionEvaluateContext& context,
         Frame frame) {
    return evaluate(
        compiled,
        context,
        SampleTime::from_frame_int(frame, context.frame_context.frame_rate()));
}

Result<EvaluatedCompositionFrame, CompositionEvaluateError>
evaluate(const CompiledComposition& compiled,
         const CompositionEvaluateContext& context,
         SampleTime sample_time) {
    if (!compiled.composition) {
        CompositionEvaluateError err;
        err.kind    = CompositionEvaluateError::Kind::NullCompiledComposition;
        err.message = "compiled.composition is null (compile_composition was not invoked)";
        return err;
    }
    const auto& composition = *compiled.composition;

    if (!composition.scene_function()) {
        CompositionEvaluateError err;
        err.kind    = CompositionEvaluateError::Kind::NullSceneFunction;
        err.message = "Composition::SceneFunction is null";
        return err;
    }

    // Thread SampleTime into FrameContext before invoking the scene fn.
    FrameContext fc = context.frame_context.with_local_time(
        sample_time, context.frame_context.duration());

    // Keep the evaluated scene in the slot-local resource.  Default
    // constructing here would make the subsequent Scene move-assignment
    // allocate from the process heap whenever the authored scene uses the
    // frame arena.
    EvaluatedCompositionFrame result(context.frame_context.resource);
    try {
        result.scene = composition.evaluate(fc);
    } catch (const std::exception& e) {
        CompositionEvaluateError err;
        err.kind    = CompositionEvaluateError::Kind::SceneBuildFailed;
        err.message = std::string("scene SceneFunction threw: ") + e.what();
        return err;
    } catch (...) {
        CompositionEvaluateError err;
        err.kind    = CompositionEvaluateError::Kind::SceneBuildFailed;
        err.message = "scene SceneFunction threw an unknown exception type";
        return err;
    }

    return result;
}

} // namespace chronon3d
