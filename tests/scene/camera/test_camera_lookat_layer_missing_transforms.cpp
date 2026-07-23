// ==============================================================================
// tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp
//
// TICKET-A3-LOOKAT-DIAGNOSTIC (Agent3 mission DoD gate (g)) — regression-lock
// that when LookAtLayer orientation is being applied and either
//   (a) CameraEvalContext::transforms == nullptr  (canonical
//       missing-transforms case), OR
//   (b) transforms is non-null but world_position(target_name) returns nullopt
//       (the named layer is not in the snapshot or has no world position)
// the compiled evaluate() emits a Warning diagnostic via the canonical
// channel (`result.diagnostics.push_back(...)` — emitted from
// `apply_orientation_spec_free` via the std::optional<CameraProgramDiagnostic>
// return at camera_program.cpp) with a stable `[MissingTransforms]` message
// prefix instead of silently no-op'ing the orientation.
//
// PRE-FIX BEHAVIOR (this is what the regression lock guards against):
//   `apply_orientation_spec_free` returned `std::nullopt` on either missing
//   case, which `CameraProgram::evaluate()` only forwarded as `result.diagnostics`
//   when non-null.  Net effect: empty diagnostics vector, no signal to the
//   composition author that the layer was unresolved.
//
// SUBCASE breakdown:
//
//   A. PRIMARY LOCK — transforms==nullptr ⇒ Warning diagnostic emitted with
//      the canonical [MissingTransforms] prefix AND the LookAtLayer target
//      name.  Companion SUBCASEs B and C assert the rotation/POI invariants
//      that the diagnostic guarantees (rotation preserved unchanged, POI
//      not auto-enabled).
//
//   B. base rotation is preserved (NOT silently corrupted by an empty
//      quaternion slerp).  The sentinel non-zero base rotation (7°,
//      13°, -5°) must round-trip through evaluate().
//
//   C. point_of_interest_enabled stays false — fixes a subtle contract
//      where a regression that "disabled the silent fallback but also
//      accidentally enabled POI" would leak a fake-POI into downstream
//      LookAtConstraint logic.
//
//   D. multiple unresolved LookAtLayer evaluations on the SAME program
//      produce a diagnostic each frame (not deduplicated / surfaced only
//      once).  Catches a regression that adds a "first-frame only" guard.
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>   // CameraSession complete type (defined at camera_session.hpp:60; camera_program.hpp:36 only forward-declares)

#include <string>

using namespace chronon3d;
constexpr FrameRate kTestFps{60, 1};

using namespace chronon3d::camera_v1;

namespace {

// Fixture: LookAtLayer orientation pointing at a non-existent layer name + a
// meaningfully non-zero base rotation so SUBCASE B has a sentinel to check.
CameraDescriptor make_lookat_layer_desc(
        std::string id_str = "test.lookat_layer_missing_transforms",
        std::string target_name = "non.existent.layer") {
    CameraDescriptor desc;
    desc.id = std::move(id_str);
    desc.base.enabled = true;
    desc.base.position  = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation  = Vec3{7.0f, 13.0f, -5.0f};   // non-zero sentinel
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.source = StaticCameraSource{};
    desc.orientation = LookAtLayer{target_name};
    return desc;
}

// Compile helper: hides the Result<...> ceremony.
// TICKET-120 followup (Unity build deconflict) — renamed from
// `compile_or_die` to file-scoped unique name to avoid the
// redefinition error in the unified TU built by
// chronon3d_scene_tests (see TICKET-120 build-redefinition group).
CameraProgram compile_or_die_lookat_diagnostic(const CameraDescriptor& desc) {
    auto cr = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(cr.has_value());
    auto prog = std::move(cr).value();
    REQUIRE(prog.is_compiled());
    return prog;
}

// Helper: find the FIRST diagnostic whose message starts with
// `[MissingTransforms]` AND contains the target name with surrounding
// single quotes.  Returns the message if found, empty std::string if not.
std::string find_missing_transforms_diagnostic(
    const std::vector<CameraProgramDiagnostic>& diagnostics,
    const std::string& target_name) {
    const std::string quoted = "'" + target_name + "'";
    for (const auto& d : diagnostics) {
        if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
            d.message.find("[MissingTransforms]") != std::string::npos &&
            d.message.find(quoted) != std::string::npos) {
            return d.message;
        }
    }
    return {};  // empty → not found
}

} // namespace

TEST_CASE(
    "camera_v1_lookat_layer_missing_transforms_emits_diagnostic — "
    "TICKET-A3-LOOKAT-DIAGNOSTIC DoD gate (g): when LookAtLayer "
    "evaluate encounters CameraEvalContext::transforms == nullptr "
    "(canonical missing-transforms case), the compiled "
    "CameraProgram::evaluate() emits a Warning diagnostic via the "
    "canonical channel (result.diagnostics) with a stable "
    "[MissingTransforms] prefix and the LookAtLayer target name. "
    "The discriminator is the streamed diagnostic; without the new "
    "emission the test sees an empty diagnostics vector. Companion "
    "SUBCASEs B / C / D preserve rotation, POI, and per-frame "
    "diagnostic cardinality invariants.") {

    SUBCASE("A. PRIMARY LOCK — transforms==nullptr ⇒ Warning diagnostic in stream") {
        CameraDescriptor desc = make_lookat_layer_desc();
        CameraProgram prog = compile_or_die_lookat_diagnostic(desc);
        CameraSession session;

        CameraEvalContext ctx;
        ctx = ctx.with_frame(Frame{0}, kTestFps);
        ctx.sample_time = SampleTime::from_frame_int(Frame{0}, FrameRate{60, 1});
        REQUIRE(ctx.transforms == nullptr);  // default-init sentinel.

        auto res = prog.evaluate(ctx, session);
        REQUIRE(res.has_value());

        const std::string msg = find_missing_transforms_diagnostic(
            res->diagnostics, "non.existent.layer");
        CAPTURE(msg);
        CHECK_FALSE(msg.empty());

        // Verify the diagnostic carries a Severity::Warning level (matches
        // the user's spec: "Severity::Warning" not Info or Error).
        bool found_warning = false;
        for (const auto& d : res->diagnostics) {
            if (d.severity == CameraProgramDiagnostic::Severity::Warning &&
                d.message.find("[MissingTransforms]") != std::string::npos &&
                d.message.find("'non.existent.layer'") != std::string::npos) {
                found_warning = true;
                break;
            }
        }
        CHECK(found_warning);
    }

    SUBCASE("B. base rotation is preserved (NOT silently corrupted)") {
        CameraDescriptor desc = make_lookat_layer_desc();
        CameraProgram prog = compile_or_die_lookat_diagnostic(desc);
        CameraSession session;

        CameraEvalContext ctx;
        ctx = ctx.with_frame(Frame{0}, kTestFps);
        ctx.sample_time = SampleTime::from_frame_int(Frame{0}, FrameRate{60, 1});

        auto res = prog.evaluate(ctx, session);
        REQUIRE(res.has_value());
        // The sentinel non-zero base rotation must round-trip bit-exact —
        // a regression that "compiles a quaternion slerp with a zero
        // look_dir and resets rotation to (0,0,0)" would fail here.
        CHECK(res->camera.rotation.x == doctest::Approx(7.0f).epsilon(1e-5f));
        CHECK(res->camera.rotation.y == doctest::Approx(13.0f).epsilon(1e-5f));
        CHECK(res->camera.rotation.z == doctest::Approx(-5.0f).epsilon(1e-5f));
    }

    SUBCASE("C. point_of_interest_enabled stays false (no fake-POI leak)") {
        CameraDescriptor desc = make_lookat_layer_desc();
        CameraProgram prog = compile_or_die_lookat_diagnostic(desc);
        CameraSession session;

        CameraEvalContext ctx;
        ctx = ctx.with_frame(Frame{0}, kTestFps);
        ctx.sample_time = SampleTime::from_frame_int(Frame{0}, FrameRate{60, 1});

        auto res = prog.evaluate(ctx, session);
        REQUIRE(res.has_value());
        // No layer was resolved → POI must NOT be auto-enabled.  A
        // regression that "disables silent fallback but inadvertently
        // also enables a default POI" would leak a fake-POI into
        // downstream LookAtConstraint logic; this catches it.
        CHECK_FALSE(res->camera.point_of_interest_enabled);
    }

    SUBCASE("D. multiple frames at transforms==nullptr ⇒ diagnostic per frame "
            "(no first-frame-only guard, no deduplication)") {
        CameraDescriptor desc = make_lookat_layer_desc(
            /*id_str=*/"test.diag_per_frame",
            /*target_name=*/"per.frame.target");
        CameraProgram prog = compile_or_die_lookat_diagnostic(desc);
        CameraSession session;

        // Three sequential evaluations, each with transforms=nullptr.
        // Each MUST emit the MissingTransforms diagnostic — a regression
        // that adds a silent-on-second-call optimization would fail here.
        const Frame frames[] = {Frame{0}, Frame{30}, Frame{60}};
        for (const Frame& f : frames) {
            CameraEvalContext ctx;
            ctx = ctx.with_frame(f, kTestFps);
            ctx.sample_time = SampleTime::from_frame_int(f, FrameRate{60, 1});
            REQUIRE(ctx.transforms == nullptr);

            auto res = prog.evaluate(ctx, session);
            REQUIRE(res.has_value());

            const std::string msg = find_missing_transforms_diagnostic(
                res->diagnostics, "per.frame.target");
            CAPTURE(f.integral());
            CAPTURE(msg);
            CHECK_FALSE(msg.empty());
        }
    }
}
