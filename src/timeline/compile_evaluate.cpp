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
#include <chronon3d/text/text_run_shape.hpp>

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
        .add(scene_is_frame_invariant)
        .add(composition.has_default_camera_descriptor());

    if (composition.has_default_camera_descriptor()) {
        hash.add(camera_v1::compute_camera_descriptor_fingerprint(
            composition.default_camera_descriptor()));
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
//   4. If the canonical Composition has a camera descriptor, delegate the camera compile to
//      `compile_camera()`; surface any failure verbatim.
//   5. Compute a deterministic sequential fingerprint over the static
//      CompositionSpec, explicit scene-content identity, and optional camera
//      descriptor. The callback object itself is intentionally not hashed.
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

    // (3) Own a snapshot of the canonical Composition. DTO conversion, when
    //     needed, happens only in the deprecated boundary overload below.
    out.composition = std::make_shared<const Composition>(composition);
    // Compatibility view only; all new consumers use `composition`.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    out.definition = out.composition;
#pragma GCC diagnostic pop

    // (4) Camera compile path (only when a descriptor was supplied).
    // ADL on `camera_v1::CameraDescriptor` finds BOTH an outer wrapper in
    // `chronon3d::` (this TU — returns Result<…, CompositionCompileError>)
    // AND an inline `camera_v1::compile_camera(...)` returning
    // Result<…, CameraCompileError>. Use a QUALIFIED call so only the
    // outer is in the overload set; the second arg is the explicit
    // CompositionCompileContext options carrier.
    if (composition.has_default_camera_descriptor()) {
        auto cam = chronon3d::compile_camera(
            composition.default_camera_descriptor(), CompositionCompileContext{});
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

    // (5) Scene execution mode resolution.
    // Correct-by-default: SceneFunction is a per-frame API (SceneExecutionMode::DynamicCallback).
    // The template scene fast-path is used only when the definition explicitly promises
    // that scene materialization is frame invariant (SceneExecutionMode::StaticScene)
    // or through compiled dynamic topology parameter slots (SceneExecutionMode::StaticTopologySlots).
    if (composition.scene_function()) {
        if (false) {
            out.execution_mode = SceneExecutionMode::StaticScene;
            try {
                const FrameContext fc0 = make_frame_context({
                    .global_time = SampleTime::from_frame(0.0, composition.frame_rate()),
                    .duration = composition.duration(),
                    .width = composition.width(),
                    .height = composition.height(),
                });
                out.static_scene = std::make_shared<const Scene>(composition.evaluate(fc0));
                out.template_scene = out.static_scene;
                out.is_static_topology = true;
            } catch (...) {
                out.execution_mode = SceneExecutionMode::DynamicCallback;
                out.static_scene = nullptr;
                out.template_scene = nullptr;
                out.is_static_topology = false;
            }
        } else {
            out.execution_mode = SceneExecutionMode::DynamicCallback;
            out.static_scene = nullptr;
            out.template_scene = nullptr;
            out.is_static_topology = false;
        }
    }

    // (6) Deterministic fingerprint of the canonical Composition.
    out.fingerprint = fingerprint_composition(composition);

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// compile_composition(Composition)
//
// Compatibility adapter: snapshot the legacy CompositionDefinition into the
// canonical Composition model and delegate to the core compiler.
// ─────────────────────────────────────────────────────────────────────────────
Result<CompiledComposition, CompositionCompileError>
compile_composition(const CompositionDefinition& definition,
                    const CompositionCompileContext& context) {
    Composition composition(definition.composition, definition.scene,
                            definition.scene_content_fingerprint);
    if (definition.camera.has_value()) {
        composition.default_camera_descriptor(*definition.camera);
    }
    return compile_composition(composition, context);
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
        if (compiled.execution_mode == SceneExecutionMode::StaticScene && compiled.static_scene) {
            result.scene = compiled.static_scene->clone();
        } else {
            result.scene = composition.evaluate(fc);
        }
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

    // Consume Camera2_5D from the compiled program. Never consult a legacy
    // composition field. The `redecompose_camera_from_descriptor`
    // helper was REMOVED in P3-F (no mutable state inside Composition).
    // Adapter-only: `CompiledComposition::camera_program` is null when the
    // canonical Composition has no authored camera descriptor.
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
