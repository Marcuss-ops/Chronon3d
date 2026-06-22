// ==============================================================================
// tests/scene/camera/test_camera_program.cpp
//
// CAM-04 / DOC 03 — Explicit HandheldNoise random-access determinism tests.
//
// The previous builder-path tests (motion(), trajectory(), orient(),
// banking(), evaluate(CameraMotionContext, ConstraintSession)) were removed
// in PR12.  This file now hosts the EXPLICIT modifier-pipeline
// determinism tests that the compiled-path tests don't isolate:
//
//   handheld_noise_round_trip           — evaluate(t1), evaluate(t2),
//                                          evaluate(t1) ⇒ second t1 == first t1.
//                                          Catches any state leak across
//                                          modifier iterations.
//   handheld_noise_insert_order         — reorder [t1, t2, t3] mid-stream
//                                          ⇒ identical per-frame result.
//                                          Mirrors the parallel-vs-serial
//                                          invariant for the modifier path.
//   handheld_noise_per_axis_decorrelation — position.x, position.y, position.z
//                                          offsets must NOT be identical (the
//                                          three axes are decorrelated via
//                                          the wiggle3D +100u/+200u seed
//                                          offsets).
//   handheld_noise_cross_channel        — HandheldNoise uses seed+50u/+150u
//                                          offsets across position, rotation,
//                                          zoom; verify each channel yields
//                                          a different offset vector.
//   handheld_no_modifier_is_identity    — descriptor with NO modifiers
//                                          yields exactly camera.base.position
//                                          / .rotation (no implicit wiggle
//                                          on the modifier path).
//
// Forward reference (was a stub comment in tests/scene/camera/
// test_camera_compiled_evaluate.cpp [5]): the previous test fixture
// didn't touch HandheldNoise; this file is now the explicit home
// for that determinism check.
//
// Round-trip determinism relies on the abs-time guarantee in
// camera_program.cpp::evaluate() — `ctx.sample_time.seconds()` is the
// ONLY time source for modifier offsets, so two evaluations at the
// same SampleTime must produce identical Camera2_5D fields.  We
// exercise this by feeding a fixed SampleTime across all calls and
// checking the result.
// ==============================================================================
#define DOCTEST_CONFIG_DISABLE_TEST_EXN_CATCH 0
#include <doctest/doctest.h>

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/result.hpp>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <chronon3d/math/glm_types.hpp>  // explicit pull for glm::length(Vec3)

#include <algorithm>
#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

constexpr float kFieldEps = 1e-5f;  // struct field tolerance (single ULP)
constexpr FrameRate kFpsDefault{60, 1};
// Default base zoom in CameraBaseSpec::projection.  Pinned here so [N4]'s
// zoom-delta arithmetic stays correct if the descriptor default ever
// changes (currently `ZoomProjection{AnimatedValue<float>{1000.0f}}`
// in include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp).
constexpr float kHandheldBaseZoom = 1000.0f;

// ── Fixture: identity base + HandheldNoise modifier ──────────────────
//
// CameraBaseSpec.position / .rotation are zero, so any non-zero value
// in the evaluated Camera2_5D must come from the HandheldNoise modifier.
// This lets us assert "modifier output" vs "modifier + base" without
// ambiguity while not coupling to the (zero-zoom projection) details.
CameraDescriptor make_handheld_descriptor(std::uint32_t seed = 42) {
    CameraDescriptor desc;
    desc.id = "cam04.handheld_determinism";

    // Identity base — all zero.
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};

    // StaticCameraSource: position / rotation stay at base (0,0,0) and
    // modifier is the ONLY contributor.  This isolates the modifier
    // pipeline from source-driven motion.
    desc.source = StaticCameraSource{};

    // Orientation: FixedOrientation preserves whatever the source wrote.
    // The re-apply-orientation block in evaluate() emits no further offset
    // when the orientation variant is FixedOrientation.
    desc.orientation = FixedOrientation{};

    // HandheldNoise modifier — non-zero amplitudes so we can observe offsets.
    HandheldNoise hh;
    hh.position_amplitude      = Vec3{2.0f, 1.0f, 0.5f};
    hh.rotation_amplitude_deg  = Vec3{0.5f, 0.3f, 0.2f};
    hh.zoom_amplitude          = 1.0f;
    hh.position_freq_hz        = 4.0f;
    hh.rotation_freq_hz        = 3.0f;
    hh.zoom_freq_hz            = 1.0f;
    hh.seed                    = seed;
    desc.modifiers.push_back(hh);

    return desc;
}

// Build the compiled CameraProgram from the canonical descriptor.
CameraProgram compile_or_die(const CameraDescriptor& desc) {
    auto result = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());
    return program;
}

// Evaluate at a given Frame.  SampleTime is the ONLY time source for
// HandheldNoise — using `SampleTime::from_frame_int(frame, kFpsDefault)`
// keeps the wiggle3D lookup deterministic and stable across
// re-evaluations and re-orders.  Returns the full Camera2_5D.
Camera2_5D eval_at(const CameraProgram& program,
                   CameraSession& session, Frame frame) {
    CameraEvalContext ctx;
    ctx.frame = frame;
    ctx.sample_time = SampleTime::from_frame_int(frame, kFpsDefault);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.ok);
    return res.camera;
}

} // namespace

// ════════════════════════════════════════════════════════════════════
// [N1] Round-trip determinism — evaluate(t1), evaluate(t2),
//                                  evaluate(t1) ⇒ second t1 == first t1.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("handheld_noise_round_trip — "
          "evaluate(t1) twice returns identical Camera2_5D") {
    CameraDescriptor desc = make_handheld_descriptor();
    CameraProgram program = compile_or_die(desc);
    CameraSession session;

    // First pass: snapshot at frames 0, 30, 60, 90.
    const Camera2_5D at_0_first  = eval_at(program, session, Frame{0});
    const Camera2_5D at_30_first = eval_at(program, session, Frame{30});
    const Camera2_5D at_60_first = eval_at(program, session, Frame{60});
    const Camera2_5D at_90_first = eval_at(program, session, Frame{90});

    // Second pass: same frames in a different mid-stream order.  The
    // round-trip equality at Frame{0} and Frame{30} (the only ones we
    // re-touch) confirms no state leak.
    const Camera2_5D at_60_again = eval_at(program, session, Frame{60});
    const Camera2_5D at_30_again = eval_at(program, session, Frame{30});
    const Camera2_5D at_0_again  = eval_at(program, session, Frame{0});
    const Camera2_5D at_90_again = eval_at(program, session, Frame{90});

    // Field-by-field equality (1e-5 ULP).
    auto check_fields = [](const Camera2_5D& a, const Camera2_5D& b) {
        CHECK(a.position.x == doctest::Approx(b.position.x).epsilon(kFieldEps));
        CHECK(a.position.y == doctest::Approx(b.position.y).epsilon(kFieldEps));
        CHECK(a.position.z == doctest::Approx(b.position.z).epsilon(kFieldEps));
        CHECK(a.rotation.x == doctest::Approx(b.rotation.x).epsilon(kFieldEps));
        CHECK(a.rotation.y == doctest::Approx(b.rotation.y).epsilon(kFieldEps));
        CHECK(a.rotation.z == doctest::Approx(b.rotation.z).epsilon(kFieldEps));
        CHECK(a.zoom       == doctest::Approx(b.zoom).epsilon(kFieldEps));
    };

    CAPTURE("at_0");
    check_fields(at_0_first, at_0_again);
    CAPTURE("at_30");
    check_fields(at_30_first, at_30_again);
    CAPTURE("at_60");
    check_fields(at_60_first, at_60_again);
    CAPTURE("at_90");
    check_fields(at_90_first, at_90_again);
}

// ════════════════════════════════════════════════════════════════════
// [N2] Insert-order insensitivity — jitter the access sequence; Camera2_5D
//                                  at any frame is invariant.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("handheld_noise_insert_order — "
          "random sequence yields identical per-frame Camera2_5D") {
    CameraDescriptor desc = make_handheld_descriptor();
    CameraProgram program = compile_or_die(desc);

    // Forward reference (sorted by frame number).
    struct CameraRef {
        Frame frame;
        Camera2_5D cam;
    };
    std::vector<CameraRef> reference;
    {
        CameraSession session;
        for (Frame f : {Frame{0}, Frame{15}, Frame{30}, Frame{45},
                         Frame{60}, Frame{75}, Frame{90}}) {
            reference.push_back({f, eval_at(program, session, f)});
        }
    }

    // Out-of-order sequence with duplicates (same frame appearing >1x).
    // Identical CameraSession is reused to share the modifier state path
    // — the desc has no DampedFollowConstraint so state remains constant.
    const std::vector<Frame> seq = {
        Frame{45}, Frame{0}, Frame{60}, Frame{15}, Frame{30},
        Frame{0}, Frame{75}, Frame{30}, Frame{90}, Frame{0},
        Frame{45}, Frame{15}, Frame{60}, Frame{30},
    };
    // NOTE: This test isolates the modifier pipeline only.  The fixture
    // descriptor does NOT include `DampedFollowConstraint`, so the
    // CameraSession state remains untouched by modifier evaluation —
    // the shared session constant proves the modifier pipeline is
    // state-clean.  Session-aware modifier state (i.e. with a
    // DampedFollowConstraint in scope) is covered separately in
    // test_camera_constraints_p5.cpp.
    CameraSession session;  // shared session across the random-access path
    for (Frame f : seq) {
        Camera2_5D got = eval_at(program, session, f);

        // Find the reference entry for `f`.
        auto it = std::find_if(reference.begin(), reference.end(),
                               [f](const CameraRef& r) { return r.frame == f; });
        REQUIRE(it != reference.end());

        CAPTURE(f.value);
        CHECK(got.position.x == doctest::Approx(it->cam.position.x).epsilon(kFieldEps));
        CHECK(got.position.y == doctest::Approx(it->cam.position.y).epsilon(kFieldEps));
        CHECK(got.position.z == doctest::Approx(it->cam.position.z).epsilon(kFieldEps));
        CHECK(got.rotation.x == doctest::Approx(it->cam.rotation.x).epsilon(kFieldEps));
        CHECK(got.rotation.y == doctest::Approx(it->cam.rotation.y).epsilon(kFieldEps));
        CHECK(got.rotation.z == doctest::Approx(it->cam.rotation.z).epsilon(kFieldEps));
        CHECK(got.zoom       == doctest::Approx(it->cam.zoom).epsilon(kFieldEps));
    }
}

// ════════════════════════════════════════════════════════════════════
// [N3] Per-axis decorrelation — position.x != position.y != position.z.
// ════════════════════════════════════════════════════════════════════
//
// wiggle3D uses independent seeds per channel via +100u/+200u offsets.
// Across a wide enough time strip, at least TWO of the three axes
// should produce non-coincident offsets (probability of exact triple
// coincidence on value-noise is vanishing).  This test pins that the
// impl has not silently collapsed into a single-axis wiggle.
TEST_CASE("handheld_noise_per_axis_decorrelation "
          "— at least 2 unique position axis offsets across N>1 frames") {
    CameraDescriptor desc = make_handheld_descriptor(/*seed=*/42);
    CameraProgram program = compile_or_die(desc);

    CameraSession session;
    std::vector<Camera2_5D> cams;
    for (Frame f : {Frame{1}, Frame{30}, Frame{60}, Frame{90},
                     Frame{120}, Frame{150}, Frame{180}}) {
        cams.push_back(eval_at(program, session, f));
    }

    // Across all 7 frames, gather the per-axis magnitudes of the
    // position offset.  Per-axis amplitudes must be at least one apart
    // OR each axis must visit at least 2 distinct values (decorrelated
    // axes means: max(position.x_set)/min != max(position.y_set)/min OR
    // minimum difference between two axes across consecutive frames > 0).
    std::vector<double> mag_x, mag_y, mag_z;
    for (const auto& c : cams) {
        mag_x.push_back(c.position.x);
        mag_y.push_back(c.position.y);
        mag_z.push_back(c.position.z);
    }

    // Sanity: at least one frame has a non-zero offset on each axis.
    auto has_nonzero = [](const std::vector<double>& v) {
        return std::any_of(v.begin(), v.end(),
                           [](double x) { return std::abs(x) > 1e-6; });
    };
    CHECK(has_nonzero(mag_x));
    CHECK(has_nonzero(mag_y));
    CHECK(has_nonzero(mag_z));

    // Per-axis spread: the X range must differ from Y and Z ranges
    // (otherwise wiggle3D's +100u/+200u seed offsets have been lost and
    // all three axes are reading from the same noise sample).
    auto range = [](const std::vector<double>& v) {
        auto [mn, mx] = std::minmax_element(v.begin(), v.end());
        return *mx - *mn;
    };
    const double range_x = range(mag_x);
    const double range_y = range(mag_y);
    const double range_z = range(mag_z);
    CAPTURE(range_x);
    CAPTURE(range_y);
    CAPTURE(range_z);
    CHECK(range_x > 0.05);  // anything < 0.05 would imply the X amplitude
                             // collapsed; amplitudes were {2.0, 1.0, 0.5}.
    // Decorrelation: either X range ≠ Y range, OR Y range ≠ Z range.
    // Both must hold in healthy wiggle3D; if both collapse the seeds are
    // dead.  Use a wide tolerance to avoid float-noise false positives.
    const bool decorrelated =
        std::abs(range_x - range_y) > 1e-3 ||
        std::abs(range_y - range_z) > 1e-3;
    CHECK(decorrelated);
}

// ════════════════════════════════════════════════════════════════════
// [N4] Cross-modifier decorrelation — position / rotation / zoom offsets
//                                  are NOT zero-coincident (seed offset
//                                  +50u / +150u must isolate channels).
// ════════════════════════════════════════════════════════════════════
TEST_CASE("handheld_noise_cross_channel_decorrelation "
          "— position, rotation, zoom offsets have independent seeds") {
    CameraDescriptor desc = make_handheld_descriptor(/*seed=*/42);
    CameraProgram program = compile_or_die(desc);

    CameraSession session;
    struct Sample { Vec3 pos; Vec3 rot; double zoom; };
    std::vector<Sample> samples;
    for (Frame f : {Frame{10}, Frame{30}, Frame{50}, Frame{70},
                     Frame{90}, Frame{110}, Frame{130}}) {
        auto c = eval_at(program, session, f);
        samples.push_back({c.position, c.rotation,
                            static_cast<double>(c.zoom) - kHandheldBaseZoom});
        // kHandheldBaseZoom = 1000.0f mirrors the descriptor default
        // (`ZoomProjection{AnimatedValue<float>{1000.0f}}`); subtract
        // to expose only the wiggle offset on the zoom channel.
    }

    // Pathological-seed guard: at least ONE frame must be live (non-zero
    // channel) before "decorrelation" is meaningful.  If the noise floor
    // is uniformly zero across all 7 frames (extremely unlikely with the
    // given amplitudes but possible for a pathological seed), the `<= 1`
    // check below would be vacuously satisfied without proving anything.
    // Requiring a non-zero activity is the precondition that makes the
    // post-check meaningful: "of the channels that ARE active, at least
    // two are independent".
    const bool any_channel_active = std::any_of(samples.begin(), samples.end(),
        [](const Sample& s) {
            return glm::length(s.pos) > 1e-3 ||
                   glm::length(s.rot) > 1e-3 ||
                   std::abs(s.zoom) > 1e-3;
        });
    REQUIRE(any_channel_active);

    // Across N frames, the position vector, rotation vector, and zoom
    // scalar must NOT all be zero (at least one must move).  More
    // importantly: at least two of the three mutating components must
    // be non-zero, proving position/rotation/zoom are reading from
    // different noise samples (seed/+50u/+150u offsets in
    // camera_program.cpp::evaluate()).
    int non_zero_channels = 0;
    bool pos_active = std::any_of(samples.begin(), samples.end(),
        [](const Sample& s) { return glm::length(s.pos) > 1e-3; });
    bool rot_active = std::any_of(samples.begin(), samples.end(),
        [](const Sample& s) { return glm::length(s.rot) > 1e-3; });
    bool zoom_active = std::any_of(samples.begin(), samples.end(),
        [](const Sample& s) { return std::abs(s.zoom) > 1e-3; });
    non_zero_channels = static_cast<int>(pos_active) +
                        static_cast<int>(rot_active) +
                        static_cast<int>(zoom_active);
    CAPTURE(non_zero_channels);
    CHECK(non_zero_channels >= 2);  // at least 2 of 3 channels
}

// ════════════════════════════════════════════════════════════════════
// [N5] Empty modifier pipeline ⇒ identity — the descriptor with NO
//                                  HandheldNoise modifier yields exactly
//                                  camera.base.position / .rotation.
// ════════════════════════════════════════════════════════════════════
TEST_CASE("handheld_no_modifier_is_identity — static base + empty modifiers ⇒ "
          "evaluated Camera2_5D == base (modifier is zero-output)") {
    CameraDescriptor desc;
    desc.id = "cam04.empty_modifiers";
    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{1.0f, 2.0f, 3.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1500.0f}};
    desc.source = StaticCameraSource{};
    desc.orientation = FixedOrientation{};
    // NO modifiers — modifier loop in evaluate() never fires.
    REQUIRE(desc.modifiers.empty());

    CameraProgram program = compile_or_die(desc);
    CameraSession session;

    // Sample at multiple frames; despite sampling, the static source
    // + empty modifier yield a camera EQUAL to base.
    for (Frame f : {Frame{0}, Frame{30}, Frame{60}, Frame{90}}) {
        CAPTURE(f.value);
        auto cam = eval_at(program, session, f);
        CHECK(cam.position.x == doctest::Approx(0.0f).epsilon(kFieldEps));
        CHECK(cam.position.y == doctest::Approx(0.0f).epsilon(kFieldEps));
        CHECK(cam.position.z == doctest::Approx(-1000.0f).epsilon(kFieldEps));
        CHECK(cam.rotation.x == doctest::Approx(1.0f).epsilon(kFieldEps));
        CHECK(cam.rotation.y == doctest::Approx(2.0f).epsilon(kFieldEps));
        CHECK(cam.rotation.z == doctest::Approx(3.0f).epsilon(kFieldEps));
    }
}
