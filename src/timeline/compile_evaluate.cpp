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

// Bodies-only headers — kept out of the public API to keep surface includes
// transitively minimal.
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>

#include <exception>

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// FNV-1a 64-bit fingerprint (file-scope helpers).
//   Deterministic across runs (no std::hash<std::string> platform variation).
//   Mirrors the convention used by Composition::compute_camera_descriptor_fingerprint
//   (chronon3d/scene/camera/camera_v1/camera_descriptor.hpp).
// ─────────────────────────────────────────────────────────────────────────────
namespace {
constexpr std::uint64_t kFnv1aOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1aPrime  = 1099511628211ULL;

std::uint64_t fnv1a64(const void* data, std::size_t n) noexcept {
    std::uint64_t h = kFnv1aOffset;
    const auto* p   = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kFnv1aPrime;
    }
    return h;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// compile_camera()
//
// V2 staging — delegate to the canonical `camera_v1::compile_camera()` and
// remap any failure to a structured `CompositionCompileError::CameraFailure`.
// `CompositionCompileContext::compiled_at` is intentionally NOT consulted at
// this stage: the camera_v1 cycle-detection state is unrelated to the
// wall-clock timestamp and is allocated fresh per call.
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
//   5. Compute a deterministic FNV-1a 64-bit fingerprint over the static
//      (CompositionSpec bytes + optional CameraDescriptor bytes +
//       SceneFunction target_type() address-as-pointer-as-bytes for
//       cheap content-stable hashing without per-std::function demangling).
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
    // Result<…, CameraCompileError>.  Use a QUALIFIED call so only the
    // outer is in the overload set; the second arg is the required
    // CompositionCompileContext payload (currently unused by the adapter).
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

    // (5) Deterministic per-field fingerprint — P1 #11.
    //     Replaces the previous raw-memory fnv1a64(&struct, sizeof) which
    //     hashed padding bytes, std::string internal pointers, and STL
    //     layout details — all non-portable across compilers/platforms.
    //     Now each field is hashed individually; std::strings via their
    //     content bytes, POD fields via their value bytes, and the
    //     CameraDescriptor via its existing canonical fingerprint function.
    std::uint64_t h = kFnv1aOffset;

    // CompositionSpec fields — hash strings by content, not by pointer.
    h ^= fnv1a64(definition.composition.name.data(), definition.composition.name.size());
    h ^= fnv1a64(&definition.composition.width,  sizeof(i32));
    h ^= fnv1a64(&definition.composition.height, sizeof(i32));
    h ^= fnv1a64(&definition.composition.frame_rate.numerator,   sizeof(i32));
    h ^= fnv1a64(&definition.composition.frame_rate.denominator, sizeof(i32));
    h ^= fnv1a64(&definition.composition.duration, sizeof(Frame));
    h ^= fnv1a64(definition.composition.assets_root.data(), definition.composition.assets_root.size());

    // Camera descriptor — use the canonical per-field fingerprint function.
    if (definition.camera.has_value()) {
        h ^= camera_v1::compute_camera_descriptor_fingerprint(*definition.camera);
    }

    // Scene function type identity — hash via type_info::hash_code(),
    // which is deterministic per ABI and independent of addresses.
    if (definition.scene) {
        const auto& target_type = definition.scene.target_type();
        if (target_type != typeid(void)) {
            h ^= static_cast<std::uint64_t>(target_type.hash_code());
        }
    }
    out.fingerprint = h;

    return out;
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
//     (`CompiledComposition::camera_program->evaluate(...)`).  NEVER from
//     `Composition::camera` (the legacy field, freshly `[[deprecated]]`).
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

    // Thread Frame into FrameContext::frame before invoking the scene fn.
    FrameContext fc  = context.frame_context;
    fc.frame         = frame;
    fc.local_frame   = frame;

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

    // P3-F: consume Camera2_5D from the compiled program.  NEVER from
    // `Composition::camera`.  The `redecompose_camera_from_descriptor`
    // helper was REMOVED in P3-F (no mutable state inside Composition).
    // Adapter-only: `CompiledComposition::camera_program` is null when the
    // caller supplied `CompositionDefinition` without a CameraDescriptor.
    if (compiled.camera_program && compiled.camera_program->is_compiled()) {
        camera_v1::CameraSession session;
        camera_v1::CameraEvalContext cam_ctx;
        cam_ctx.sample_time = SampleTime::from_frame(
            static_cast<double>(frame),
            static_cast<FrameRate>(context.frame_context.frame_rate));
        const auto cam_result =
            compiled.camera_program->evaluate(cam_ctx, session);
        if (cam_result.ok) {
            result.camera = cam_result.camera;
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
