// tests/install_consumer/main.cpp
//
// ── End-to-end install boundary validation (TICKET-011 final gate) ──
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against the in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// The fact that this file:
//   1. Compiles against `<prefix>/include/chronon3d/...` headers
//   2. Links against `libchronon3d_sdk_impl.a` (transitively via
//      Chronon3D::SDK)
//   3. Runs and emits the [BOUNDARY-OK] marker string
// …proves that the SDK install is consumable by an external project
// downstream.
//
// Wired into CTest via the entry added in the top-level CMakeLists.txt
// (option CHRONON3D_BUILD_INSTALL_CONSUMER_TEST, enabled by the
// linux-ci preset). The orchestrator script
// `tools/install_consumer_test.sh` runs the full configure -> build ->
// install -> consume -> run pipeline against an isolated temp prefix
// and validates the marker.
//
// NOTE: even a minimal main() instantiates `chronon3d::Vec3` and reads
// its members, so the linker is forced to scan libchronon3d_sdk_impl.a
// for the symbol.  Without an actual SDK symbol reference, a static
// archive passes `nm -D` vacuously and the test would not detect a
// corrupt / empty archive.

#include <chronon3d/chronon3d.hpp>

// ── Camera Production V1 public SDK surface ──────────────────────────
// CameraDescriptor → compile_camera() → CameraProgram → evaluate()
// Certifies that the compiled camera path is consumable by external SDK
// consumers (Camera Production V1 gate §9).
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>

#include <cstdio>

int main() {
    // ── §1 — Basic type boundary check ────────────────────────────────
    chronon3d::Vec3 v{1.0f, 2.0f, 3.0f};

    // ── §2 — Compiled camera boundary check (Camera Production V1 §9) ─
    // Build a minimal CameraDescriptor, compile it, and evaluate at frame 0.
    // Exercises the COMPLETE authored-camera SDK surface:
    //   CameraDescriptor → compile_camera() → CameraProgram → evaluate().
    using namespace chronon3d::camera_v1;

    CameraDescriptor cam_desc;
    cam_desc.id = "sdk_consumer_test_cam";
    cam_desc.base.position = chronon3d::Vec3{0.0f, 100.0f, -1500.0f};
    cam_desc.base.point_of_interest = chronon3d::Vec3{0.0f, 0.0f, 0.0f};
    cam_desc.base.point_of_interest_enabled = true;
    cam_desc.source = PoseTracksSource{};
    cam_desc.orientation = OrientAlongPath{.keep_horizon = true};

    auto compile_result = compile_camera(cam_desc, nullptr);
    if (!compile_result) {
        std::fprintf(stderr,
            "[BOUNDARY-FAIL] compile_camera() error: %s\n",
            compile_result.error().message.c_str());
        return 1;
    }

    auto program = std::move(compile_result).value();
    if (!program.is_compiled()) {
        std::fprintf(stderr,
            "[BOUNDARY-FAIL] CameraProgram not compiled\n");
        return 1;
    }

    // Evaluate at frame 0 with a fresh session.
    CameraSession session;
    session.ensure_constraint_states(0);
    auto ctx = CameraEvalContext::at(chronon3d::Frame{0});

    auto result = program.evaluate(ctx, session);
    if (!result.ok) {
        std::fprintf(stderr,
            "[BOUNDARY-FAIL] evaluate() returned !ok; diagnostics=%zu\n",
            result.diagnostics.size());
        return 1;
    }

    // ── Boundary marker ───────────────────────────────────────────────
    // The orchestrator greps the consumer stdout for this exact substring
    // and fails CI if it is absent.
    std::printf("[BOUNDARY-OK] install_consumer linked: "
                "Vec3={%f,%f,%f} "
                "camera_compiled=%d "
                "camera_pos={%f,%f,%f}\n",
                v.x, v.y, v.z,
                program.is_compiled() ? 1 : 0,
                result.camera.position.x,
                result.camera.position.y,
                result.camera.position.z);
    return 0;
}
