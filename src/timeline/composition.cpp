// =============================================================================
// src/timeline/composition.cpp
//
// TICKET-034 — Composition canonical default-camera bodies.
//
// Implementation of `Composition::default_camera_descriptor(...)`,
// `Composition::invalidate_default_camera_program()`,
// `Composition::default_camera_program()`, and
// `Composition::redecompose_camera_from_descriptor(...)`.  Lives in src/
// so that `<chronon3d/timeline/composition.hpp>` does NOT transitively
// pull the entire V1 camera subsystem + spdlog into every TU.
//
// Concerns addressed (refinement round):
//   - Reviewer #A (heavy header dep): bodies are out-of-line; header
//     carries only forward declarations.
//   - Reviewer #B (broken recompose fields): `redecompose_camera_from_descriptor`
//     now projects ONLY onto fields that ACTUALLY exist on the slim
//     legacy Camera struct (transform.position + transform.rotation +
//     fov_deg).  Fields without a slot (parent_name / target_name /
//     point_of_interest / DoF / Lens / MotionBlur) are out-of-scope
//     and tracked for TICKET-035.
//   - Reviewer #C (spdlog spam): throttle hash is the descriptor's
//     content-stable FNV-1a fingerprint (the canonical `compute_
//     camera_descriptor_fingerprint` helper) rather than a weak
//     std::hash<std::string> of just the descriptor id, so content
//     changes correctly re-warn exactly once.
// =============================================================================

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d {

// ── default_camera_descriptor — setter, invalidates cache. ───────────────
Composition& Composition::default_camera_descriptor(
    camera_v1::CameraDescriptor descriptor) {
    m_default_camera_desc = std::move(descriptor);
    m_default_program_cache.reset();
    // Reset the warn-throttle so a NEW bad descriptor will log once.
    m_last_warned_descriptor_hash = 0;
    return *this;
}

// ── invalidate_default_camera_program — cache reset. ──────────────────────
bool Composition::invalidate_default_camera_program() {
    const bool was_compiled = (m_default_program_cache != nullptr);
    m_default_program_cache.reset();
    return was_compiled;
}

// ── default_camera_program — lazy compile + cache + spdlog throttle. ─────
//
// On compile failure: warn ONCE per distinct descriptor-hash via
// `m_last_warned_descriptor_hash`.  Reading without a descriptor set
// returns a sentinel empty program so callers can inspect
// `is_compiled()` and `descriptor()` without UB.
const camera_v1::CameraProgram& Composition::default_camera_program() const {
    if (!m_default_program_cache) {
        if (m_default_camera_desc.id.empty()) {
            m_default_program_cache =
                std::make_unique<camera_v1::CameraProgram>();
        } else {
            auto result = camera_v1::compile_camera(
                m_default_camera_desc, /*catalog=*/nullptr);
            if (result) {
                m_default_program_cache =
                    std::make_unique<camera_v1::CameraProgram>(
                        std::move(result).value());
            } else {
                // Throttle spdlog::warn via the descriptor's
                // content-stable FNV-1a fingerprint
                // (compute_camera_descriptor_fingerprint from
                // `<chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>`).
                // Re-setting a descriptor with a different content
                // produces a different fingerprint and re-warns once;
                // the same bad descriptor across many frames stays
                // silent after the first warning.
                const auto fp =
                    camera_v1::compute_camera_descriptor_fingerprint(
                        m_default_camera_desc);
                if (fp != m_last_warned_descriptor_hash) {
                    spdlog::warn(
                        "[composition] TICKET-034 default_camera "
                        "compile_camera() failed: {}; legacy Camera "
                        "field left untouched.",
                        result.error().message);
                    m_last_warned_descriptor_hash = fp;
                }
                m_default_program_cache =
                    std::make_unique<camera_v1::CameraProgram>();
            }
        }
    }
    return *m_default_program_cache;
}

// ── redecompose_camera_from_descriptor — evaluate + copy to legacy Camera. ─
//
// We project the V1 Camera2_5D evaluation result onto the slim legacy
// `Camera camera` field.  Coverage is intentionally limited to the
// fields the legacy `Camera` struct ACTUALLY carries:
//   * Position + rotation  (always — via Camera.transform.*)
//   * fov_deg              (only when the descriptor's projection is
//                           FovProjection AND the evaluated value is
//                           >0; otherwise leave the legacy default)
//
// Not copied (legacy `Camera` struct has no slot):
//   * parent_name / target_name        — name resolution belongs in the
//                                         rig-binding layer (TICKET-035)
//   * point_of_interest + enabled flag  — same; will be re-introduced
//                                         on a `Camera2_5D`-shaped
//                                         field in TICKET-035
//   * DoF / Lens / MotionBlur          — rich renderer input is not
//                                         representable in the slim
//                                         legacy field.
//
// Callers needing those fields should read
// `default_camera_program().descriptor()` or call
// `default_camera_program().evaluate(...)` directly (TICKET-035).
bool Composition::redecompose_camera_from_descriptor(SampleTime time) {
    if (!has_default_camera_descriptor()) {
        return false;
    }
    const auto& program = default_camera_program();
    if (!program.is_compiled() || program.descriptor() == nullptr) {
        return false;
    }
    camera_v1::CameraSession session;
    camera_v1::CameraEvalContext ctx;
    ctx.sample_time = time;
    const auto result = program.evaluate(ctx, session);
    if (!result.ok) {
        // Per-frame diagnostic.  Throttle by result.camera.position
        // hash would be over-engineered; spdlog::info here is fine
        // because the worst case is one log per frame per job, and
        // most jobs evaluate programs that produce warnings as
        // expected (e.g. constraint-stack fallbacks).
        spdlog::info(
            "[composition] TICKET-034 default_camera evaluate() — "
            "{} diagnostic(s); copy proceeding.",
            result.diagnostics.size());
    }
    // Project Camera2_5D onto the slim legacy Camera.  Only fields
    // that exist on the legacy struct are touched (see header comment
    // on `Composition::redecompose_camera_from_descriptor` for the
    // exact coverage map).
    const auto& src = result.camera;
    camera.transform.position = src.position;
    // fov_deg is only copied when the projection variant is FovProjection
    // AND the evaluated value is positive.  ZoomProjection takes the
    // "leave-legacy-default" path because Camera::focal_length is a
    // GETTER (no setter path to reverse-derive fov_deg from zoom).
    if (std::get_if<camera_v1::FovProjection>(
            &program.descriptor()->base.projection) &&
        src.fov_deg > 0.0f) {
        camera.fov_deg = src.fov_deg;
    }
    return true;
}

} // namespace chronon3d
