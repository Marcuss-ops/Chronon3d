// ============================================================================
// src/timeline/compile_evaluate.cpp
//
// Bodies for the three free functions declared in
// `<chronon3d/timeline/compile_evaluate.hpp>`:
//
//   * chronon3d::compile_camera(...)
//   * chronon3d::compile_composition(...)
//   * chronon3d::evaluate(...)
//
// This translation unit pays the heavyweight `camera_v1/camera_program_compiler`
// + spdlog cost ONCE per binary, instead of once per scheduling TU that
// includes the public header.  See the header docstring for the design rule
// "compile separated from evaluate; no mutable state inside Composition".
// ============================================================================

#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>

// Bodies-only headers — kept out of the public API to keep surface includes
// transitively minimal.
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
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
std::uint64_t fingerprint_composition_definition(
    const CompositionDefinition& definition) {
    auto hash = core::hash::HashBuilder{}
        .add("chronon3d.composition.fingerprint.v2")
        .add(definition.composition.name)
        .add(definition.composition.width)
        .add(definition.composition.height)
        .add(definition.composition.frame_rate.numerator)
        .add(definition.composition.frame_rate.denominator)
        .add(definition.composition.duration)
        .add(definition.scene_content_fingerprint)
        .add(definition.camera.has_value());

    if (definition.camera.has_value()) {
        hash.add(camera_v1::compute_camera_descriptor_fingerprint(
            *definition.camera));
    }
    return hash.finish();
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// compile_camera()
//
// V2 staging — delegate to the canonical `camera_v1::compile_camera()` and
// remap any failure to a structured `CompositionCompileError::CameraFailure`.
// The context is currently an empty options carrier; camera_v1 cycle-detection
// state is allocated fresh per call and compilation has no wall-clock input.
// ─────────────────────────────────────────────────────────────────────────────
Result<camera_v1::CameraProgram, CompositionCompileError>
compile_camera(const camera_v1::CameraDescriptor& descriptor,
               const CompositionCompileContext& /*context*/) {
    camera_v1::CameraCompileContext camera_ctx{};
    auto inner = camera_v1::compile_camera(
        descriptor,
        /*catalog=*/nullptr,
        camera_ctx);

    if (!inner.has_value()) {
        CompositionCompileError err;
        err.kind    = CompositionCompileError::Kind::CameraFailure;
        err.message = "camera_v1::compile_camera() returned an error";
        return err;  // implicit Result<...> ctor from E&& (one of the two valid paths)
    }
    return std::move(inner).value();  // implicit Result<...> ctor from T&&
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_composition()
//
// V2 contract — no mutable state.  Same `(definition, context)` inputs always
// produce a value-equal `CompiledComposition` (same fingerprint, same
// camera-program storage).
//
// Construction order:
//   1. Sanity-check CompositionSpec (non-empty name + positive dims).
//   2. Sanity-check SceneFunction presence.
//   3. Capture `definition` into a non-owning ref-counted shared_ptr (the
//      caller retains lifetime ownership).
//   4. If `definition.camera` is set, delegate the camera compile to
//      `compile_camera()`; surface any failure verbatim.
//   5. Compute a deterministic sequential fingerprint over the static
//      CompositionSpec, explicit scene-content identity, and optional camera
//      descriptor. The callback object itself is intentionally not hashed.
// ─────────────────────────────────────────────────────────────────────────────
Result<CompiledComposition, CompositionCompileError>
compile_composition(const CompositionDefinition& definition,
                    const CompositionCompileContext& /*context*/) {
    // (1) CompositionSpec sanity.
    if (definition.composition.name.empty() ||
        definition.composition.width  <= 0 ||
        definition.composition.height <= 0) {
        CompositionCompileError err;
        err.kind    = CompositionCompileError::Kind::EmptyCompositionSpec;
        err.message = "CompositionSpec has empty name or non-positive dimensions";
        return err;
    }

    // (2) SceneFunction presence.
    if (!definition.scene) {
        CompositionCompileError err;
        err.kind    = CompositionCompileError::Kind::NoSceneFunction;
        err.message = "CompositionDefinition::scene is null";
        return err;
    }

    CompiledComposition out;

    // (3) OWNING deep copy of the definition — P1 #11.
    //     CompiledComposition now owns its own heap copy via make_shared,
    //     so it survives destruction of the caller's original definition.
    //     CompositionDefinition is copyable: CompositionSpec (POD + strings),
    //     SceneFunction (std::function with state-safe captures), and
    //     optional<CameraDescriptor> (copyable).
    out.definition = std::make_shared<const CompositionDefinition>(definition);

    // (4) Camera compile path (only when a descriptor was supplied).
    // ADL on `camera_v1::CameraDescriptor` finds BOTH an outer wrapper in
    // `chronon3d::` (this TU — returns Result<…, CompositionCompileError>)
    // AND an inline `camera_v1::compile_camera(...)` returning
    // Result<…, CameraCompileError>. Use a QUALIFIED call so only the
    // outer is in the overload set; the second arg is the explicit
    // CompositionCompileContext options carrier.
    if (definition.camera.has_value()) {
        auto cam = chronon3d::compile_camera(
            *definition.camera, CompositionCompileContext{});
        if (!cam.has_value()) {
            return std::move(cam).error();
        }
        // Adopt-storage-then-const-borrow: the camera program lives in a
        // keeping shared_ptr<CameraProgram>; the public field is a
        // shared_ptr<const CameraProgram> aliasing the same object.
        auto          keep   = std::make_shared<camera_v1::CameraProgram>(
            std::move(cam).value());
        out.camera_program = std::shared_ptr<const camera_v1::CameraProgram>(
            keep, keep.get());
    }

    // (5) Deterministic per-field fingerprint — P1 #11. Keep the helper
    // reusable so compatibility adapters can replace storage without making
    // the fingerprint describe a different immutable definition.
    out.fingerprint = fingerprint_composition_definition(definition);

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_composition(Composition)
//
// Compatibility adapter — snapshot the legacy object into the explicit V2
// input, then delegate to the canonical definition compiler. Existing
// Composition callers remain source-compatible while new code can consume
// the returned CompiledComposition.
// ─────────────────────────────────────────────────────────────────────────────
Result<CompiledComposition, CompositionCompileError>
compile_composition(const Composition& composition,
                    const CompositionCompileContext& context) {
    CompositionDefinition definition;
    definition.composition.name = composition.name();
    definition.composition.width = composition.width();
    definition.composition.height = composition.height();
    definition.composition.frame_rate = composition.frame_rate();
    definition.composition.duration = composition.duration();

    if (!composition.has_scene_function()) {
        CompositionCompileError err;
        err.kind = CompositionCompileError::Kind::NoSceneFunction;
        err.message = "Composition::SceneFunction is null";
        return err;
    }

    auto scene = composition.scene_function_snapshot();
    definition.scene = [scene = std::move(scene)](const FrameContext& frame_context) {
        return Composition::evaluate_scene_function(scene, frame_context);
    };
    definition.scene_content_fingerprint = composition.scene_content_fingerprint();

    // Snapshot the sole authoring-time camera input. Compilation owns the
    // resulting CameraProgram in CompiledComposition.
    if (composition.has_default_camera_descriptor()) {
        definition.camera = composition.default_camera_descriptor();
    }
    return compile_composition(definition, context);
}

// ─────────────────────────────────────────────────────────────────────────────
// evaluate()
//
// V2 staging — pure.  Threads `frame` into `FrameContext::frame`, calls the
// captured `CompositionDefinition::scene` lambda, catches any exception as a
// `CompositionEvaluateError::SceneBuildFailed`.
//
// Camera2_5D resolution (P3-F):
//   * Reads Camera2_5D from the compiled camera program
//     (`CompiledComposition::camera_program->evaluate(...)`).
//     The legacy `redecompose_camera_from_descriptor` helper that
//     inverse-project-a-program-onto-the-legacy-field has been REMOVED
//     in P3-F alongside the mutable camera cache inside Composition.
//     The V2 pipeline evaluated here is the canonical consume path.
//   * A `[[deprecated]]` warning + deprecation guarantee is in place on
//     the legacy field; future render-path consumers MUST consume the
//     `EvaluatedCompositionFrame::camera` produced here.
//   * When a composition was compiled WITHOUT a camera descriptor
//     (`!definition.camera.has_value()` ⇒ `compiled.camera_program` is
//     null), we leave `result.camera == std::nullopt` — mirroring the
//     legacy "Composition has no descriptor" contract.
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
    if (!compiled.definition) {
        CompositionEvaluateError err;
        err.kind    = CompositionEvaluateError::Kind::NullCompiledComposition;
        err.message = "compiled.definition is null (compile_composition was not invoked)";
        return err;
    }
    const auto& def = *compiled.definition;

    if (!def.scene) {
        CompositionEvaluateError err;
        err.kind    = CompositionEvaluateError::Kind::NullSceneFunction;
        err.message = "CompositionDefinition::scene is null";
        return err;
    }

    // Thread SampleTime into FrameContext before invoking the scene fn.
    FrameContext fc = context.frame_context.with_local_time(
        sample_time, context.frame_context.duration());

    EvaluatedCompositionFrame result;
    try {
        result.scene = def.scene(fc);
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

    // P3-F: consume Camera2_5D from the compiled program.  Never use a
    // legacy composition field.  The `redecompose_camera_from_descriptor`
    // helper was REMOVED in P3-F (no mutable state inside Composition).
    // Adapter-only: `CompiledComposition::camera_program` is null when the
    // caller supplied `CompositionDefinition` without a CameraDescriptor.
    if (compiled.camera_program && compiled.camera_program->is_compiled()) {
        camera_v1::CameraSession session;
        camera_v1::CameraEvalContext cam_ctx;
        cam_ctx.sample_time = sample_time;
        const auto cam_result =
            compiled.camera_program->evaluate(cam_ctx, session);
        if (cam_result.has_value()) {
            result.camera = cam_result->camera;
        }
        // Diagnostics on the CameraProgram::evaluate() result are not
        // surfaced to the V2 evaluate() return channel — they're
        // implementation-detail (constraint stack fallback, etc.) and the
        // legacy Composition logs equivalent info via spdlog::info on
        // every frame.  Out of scope for this staging commit.
    }

    return result;
}

} // namespace chronon3d
