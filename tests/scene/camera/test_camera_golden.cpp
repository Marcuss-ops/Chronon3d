// ═══════════════════════════════════════════════════════════════════════════
// tests/scene/camera/test_camera_golden.cpp — Golden Camera Suite
//
// End-to-end regression tests for the compiled camera path:
//   CameraDescriptor → compile_camera() → CameraProgram → evaluate()
//
// Each (source, orientation, frame) combination produces a deterministic
// snapshot: position, rotation, zoom/FOV.  A 64-bit FNV-1a hash of the
// snapshot bytes is compared against a golden reference.  Sentinel workflow
// mirrors tests/text/test_text_preset_visual.cpp:
//
//   Reference initialised to kUncapturedSentinel (0xDEADBEEFDEADBEEFULL).
//   First run emits "unset; first hash to capture: <hash>" for each snapshot.
//   Copy each <hash> into the corresponding kRefCam* constant. Re-run to
//   engage the gate.
//
// Coverage (10 configurations × 4 frames = 40 snapshots):
//
//   §1  Static + FixedOrientation
//   §2  Static + LookAtPoint
//   §3  Static + OrientAlongPath (safe no-op with no POI)
//   §4  PoseTracks + FixedOrientation
//   §5  PoseTracks + LookAtPoint
//   §6  PoseTracks + OrientAlongPath
//   §7  Orbit + FixedOrientation
//   §8  Orbit + LookAtPoint
//   §9  Trajectory + FixedOrientation
//   §10 Trajectory + OrientAlongPath
//   §11 Orbit + OrientAlongPath (no POI from orbit = safe no-op)
//   §12 Trajectory + LookAtPoint
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// ── Sentinel helpers ──────────────────────────────────────────────────────
constexpr std::uint64_t kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL;

inline bool is_captured(std::uint64_t r) noexcept {
    return r != kUncapturedSentinel;
}

// ── FNV-1a snapshot hasher ────────────────────────────────────────────────
inline std::uint64_t hash_camera_snapshot(const Camera2_5D& cam) {
    const std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t h = 0xcbf29ce484222325ULL;

    auto mix_bytes = [&](const void* data, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= kPrime; }
    };
    auto mix_f32 = [&](float v) { mix_bytes(&v, sizeof(v)); };
    auto mix_bool = [&](bool b) { std::uint8_t x = b ? 1u : 0u; mix_bytes(&x, 1); };

    mix_f32(cam.position.x);
    mix_f32(cam.position.y);
    mix_f32(cam.position.z);
    mix_f32(cam.rotation.x);
    mix_f32(cam.rotation.y);
    mix_f32(cam.rotation.z);
    mix_f32(cam.zoom);
    mix_f32(cam.fov_deg);
    mix_bool(cam.point_of_interest_enabled);
    mix_f32(cam.point_of_interest.x);
    mix_f32(cam.point_of_interest.y);
    mix_f32(cam.point_of_interest.z);

    return h;
}

// ── Sentinel gate macro ───────────────────────────────────────────────────
#define CAMERA_GOLDEN_GATE(short_label, kref, cam_expr)                       \
    do {                                                                       \
        auto gate_cam = (cam_expr);                                             \
        auto gate_hash = hash_camera_snapshot(gate_cam);                       \
        if (is_captured(kref)) {                                                \
            REQUIRE(gate_hash == kref);                                         \
        } else {                                                                \
            MESSAGE("VR/Camera/" << short_label                                 \
                    << " unset; first hash to capture: " << gate_hash);        \
        }                                                                       \
    } while (0)

// ── Frame constants ───────────────────────────────────────────────────────
constexpr FrameRate kGoldenFps{30, 1};
constexpr Frame kF000{0};
constexpr Frame kF015{15};
constexpr Frame kF030{30};
constexpr Frame kF045{45};

// ── Helpers ───────────────────────────────────────────────────────────────
Camera2_5D eval_at(Frame f, const CameraDescriptor& desc, CameraSession& sess) {
    auto result = compile_camera(desc, /*catalog=*/nullptr);
    REQUIRE(result.has_value());
    auto prog = std::move(result).value();
    REQUIRE(prog.is_compiled());

    CameraEvalContext ctx;
    ctx.frame = f;
    ctx.sample_time = SampleTime::from_frame_int(f, kGoldenFps);
    auto res = prog.evaluate(ctx, sess);
    REQUIRE(res.ok);
    return res.camera;
}

CameraDescriptor make_base_desc(std::string id_str = "golden.test") {
    CameraDescriptor d;
    d.id = std::move(id_str);
    d.base.enabled = true;
    d.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    d.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    d.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};
    d.orientation = FixedOrientation{};
    return d;
}

// ── 40 sentinel constants (10 configs × 4 frames) ────────────────────────
// §1: Static + Fixed
constexpr std::uint64_t kRefStaticFixed_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticFixed_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticFixed_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticFixed_F045 = kUncapturedSentinel;

// §2: Static + LookAtPoint
constexpr std::uint64_t kRefStaticLookAt_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticLookAt_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticLookAt_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticLookAt_F045 = kUncapturedSentinel;

// §3: Static + OrientAlongPath (no POI → safe no-op)
constexpr std::uint64_t kRefStaticOAP_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticOAP_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticOAP_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefStaticOAP_F045 = kUncapturedSentinel;

// §4: PoseTracks + Fixed
constexpr std::uint64_t kRefPoseFixed_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseFixed_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseFixed_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseFixed_F045 = kUncapturedSentinel;

// §5: PoseTracks + LookAtPoint
constexpr std::uint64_t kRefPoseLookAt_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseLookAt_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseLookAt_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseLookAt_F045 = kUncapturedSentinel;

// §6: PoseTracks + OrientAlongPath
constexpr std::uint64_t kRefPoseOAP_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseOAP_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseOAP_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefPoseOAP_F045 = kUncapturedSentinel;

// §7: Orbit + Fixed
constexpr std::uint64_t kRefOrbitFixed_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitFixed_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitFixed_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitFixed_F045 = kUncapturedSentinel;

// §8: Orbit + LookAtPoint
constexpr std::uint64_t kRefOrbitLookAt_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitLookAt_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitLookAt_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitLookAt_F045 = kUncapturedSentinel;

// §9: Trajectory + Fixed
constexpr std::uint64_t kRefTrajFixed_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajFixed_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajFixed_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajFixed_F045 = kUncapturedSentinel;

// §10: Trajectory + OrientAlongPath
constexpr std::uint64_t kRefTrajOAP_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajOAP_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajOAP_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajOAP_F045 = kUncapturedSentinel;

// §11: Orbit + OrientAlongPath (no POI = safe no-op)
constexpr std::uint64_t kRefOrbitOAP_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitOAP_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitOAP_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefOrbitOAP_F045 = kUncapturedSentinel;

// §12: Trajectory + LookAtPoint
constexpr std::uint64_t kRefTrajLookAt_F000 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajLookAt_F015 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajLookAt_F030 = kUncapturedSentinel;
constexpr std::uint64_t kRefTrajLookAt_F045 = kUncapturedSentinel;

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// §1 — Static + FixedOrientation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_static_fixed — "
          "StaticCameraSource + FixedOrientation: position/rotation unchanged at all frames") {
    auto desc = make_base_desc("golden.static.fixed");
    desc.source = StaticCameraSource{};
    desc.orientation = FixedOrientation{};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};

    CameraSession session;
    CAMERA_GOLDEN_GATE("StaticFixed_F000", kRefStaticFixed_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("StaticFixed_F015", kRefStaticFixed_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("StaticFixed_F030", kRefStaticFixed_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("StaticFixed_F045", kRefStaticFixed_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §2 — Static + LookAtPoint
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_static_lookat — "
          "StaticCameraSource + LookAtPoint: camera looks at off-axis target, "
          "rotation changes per look-at math") {
    auto desc = make_base_desc("golden.static.lookat");
    desc.source = StaticCameraSource{};
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.orientation = LookAtPoint{Vec3{200.0f, 100.0f, 500.0f}};

    CameraSession session;
    CAMERA_GOLDEN_GATE("StaticLookAt_F000", kRefStaticLookAt_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("StaticLookAt_F015", kRefStaticLookAt_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("StaticLookAt_F030", kRefStaticLookAt_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("StaticLookAt_F045", kRefStaticLookAt_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §3 — Static + OrientAlongPath (no POI → safe no-op)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_static_oap_noop — "
          "StaticCameraSource + OrientAlongPath without POI: preserves base rotation") {
    auto desc = make_base_desc("golden.static.oap");
    desc.source = StaticCameraSource{};
    desc.base.rotation = Vec3{10.0f, 5.0f, 0.0f};
    desc.base.point_of_interest_enabled = false;
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    CameraSession session;
    CAMERA_GOLDEN_GATE("StaticOAP_F000", kRefStaticOAP_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("StaticOAP_F015", kRefStaticOAP_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("StaticOAP_F030", kRefStaticOAP_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("StaticOAP_F045", kRefStaticOAP_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §4 — PoseTracks + FixedOrientation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_pose_tracks_fixed — "
          "PoseTracksSource dolly Z -1500 → -500 over 60f + FixedOrientation") {
    auto desc = make_base_desc("golden.pose.fixed");
    PoseTracksSource pts;
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f}, Easing::Linear);
    pts.rotation.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f}, Easing::Linear)
                .key(Frame{60}, Vec3{0.0f, 15.0f, 0.0f}, Easing::Linear);
    pts.zoom.key(Frame{0}, 1000.0f)
           .key(Frame{60}, 1500.0f, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;
    desc.orientation = FixedOrientation{};

    CameraSession session;
    CAMERA_GOLDEN_GATE("PoseFixed_F000", kRefPoseFixed_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("PoseFixed_F015", kRefPoseFixed_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("PoseFixed_F030", kRefPoseFixed_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("PoseFixed_F045", kRefPoseFixed_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §5 — PoseTracks + LookAtPoint
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_pose_tracks_lookat — "
          "PoseTracksSource dolly + LookAtPoint: rotation from look-at overrides "
          "PoseTracks keys (orientation wins)") {
    auto desc = make_base_desc("golden.pose.lookat");
    PoseTracksSource pts;
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f}, Easing::Linear);
    pts.rotation.key(Frame{0},  Vec3{0.0f, 0.0f, 0.0f}, Easing::Linear)
                .key(Frame{60}, Vec3{0.0f, 15.0f, 0.0f}, Easing::Linear);
    pts.use_target = false;
    desc.source = pts;
    desc.orientation = LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}};

    CameraSession session;
    CAMERA_GOLDEN_GATE("PoseLookAt_F000", kRefPoseLookAt_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("PoseLookAt_F015", kRefPoseLookAt_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("PoseLookAt_F030", kRefPoseLookAt_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("PoseLookAt_F045", kRefPoseLookAt_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §6 — PoseTracks + OrientAlongPath
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_pose_tracks_oap — "
          "PoseTracksSource with POI enabled + OrientAlongPath: camera follows tangent") {
    auto desc = make_base_desc("golden.pose.oap");
    PoseTracksSource pts;
    pts.position.key(Frame{0},  Vec3{0.0f, 0.0f, -1500.0f}, Easing::Linear)
               .key(Frame{60}, Vec3{0.0f, 0.0f, -500.0f}, Easing::Linear);
    pts.target.key(Frame{0},  Vec3{0.0f, 0.0f, -2000.0f}, Easing::Linear)
              .key(Frame{60}, Vec3{0.0f, 0.0f, 0.0f}, Easing::Linear);
    pts.use_target = true;
    desc.source = pts;
    desc.orientation = OrientAlongPath{/*keep_horizon=*/true};

    CameraSession session;
    CAMERA_GOLDEN_GATE("PoseOAP_F000", kRefPoseOAP_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("PoseOAP_F015", kRefPoseOAP_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("PoseOAP_F030", kRefPoseOAP_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("PoseOAP_F045", kRefPoseOAP_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §7 — Orbit + FixedOrientation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_orbit_fixed — "
          "OrbitMotion yaw sweep 0→90 over 60f + FixedOrientation") {
    auto desc = make_base_desc("golden.orbit.fixed");
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.key(Frame{0}, 0.0f, Easing::Linear)
             .key(Frame{60}, 90.0f, Easing::Linear);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);
    desc.source = orbit;
    desc.orientation = FixedOrientation{};

    CameraSession session;
    CAMERA_GOLDEN_GATE("OrbitFixed_F000", kRefOrbitFixed_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("OrbitFixed_F015", kRefOrbitFixed_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("OrbitFixed_F030", kRefOrbitFixed_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("OrbitFixed_F045", kRefOrbitFixed_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §8 — Orbit + LookAtPoint
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_orbit_lookat — "
          "OrbitMotion + LookAtPoint at origin: camera always faces target") {
    auto desc = make_base_desc("golden.orbit.lookat");
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.key(Frame{0}, 0.0f, Easing::Linear)
             .key(Frame{60}, 90.0f, Easing::Linear);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);
    desc.source = orbit;
    desc.orientation = LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}};

    CameraSession session;
    CAMERA_GOLDEN_GATE("OrbitLookAt_F000", kRefOrbitLookAt_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("OrbitLookAt_F015", kRefOrbitLookAt_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("OrbitLookAt_F030", kRefOrbitLookAt_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("OrbitLookAt_F045", kRefOrbitLookAt_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §9 — Trajectory + FixedOrientation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_trajectory_fixed — "
          "TrajectoryMotion Z -1500 → -500 over 90f + FixedOrientation") {
    auto desc = make_base_desc("golden.traj.fixed");
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = FixedOrientation{};

    CameraSession session;
    CAMERA_GOLDEN_GATE("TrajFixed_F000", kRefTrajFixed_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("TrajFixed_F015", kRefTrajFixed_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("TrajFixed_F030", kRefTrajFixed_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("TrajFixed_F045", kRefTrajFixed_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §10 — Trajectory + OrientAlongPath
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_trajectory_oap — "
          "TrajectoryMotion Z -1500 → -500 + OrientAlongPath with keep_horizon: "
          "camera faces forward along path, roll zeroed") {
    auto desc = make_base_desc("golden.traj.oap");
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{-500.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{500.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = OrientAlongPath{/*keep_horizon=*/true};

    CameraSession session;
    CAMERA_GOLDEN_GATE("TrajOAP_F000", kRefTrajOAP_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("TrajOAP_F015", kRefTrajOAP_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("TrajOAP_F030", kRefTrajOAP_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("TrajOAP_F045", kRefTrajOAP_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §11 — Orbit + OrientAlongPath (no POI = safe no-op)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_orbit_oap_noop — "
          "OrbitMotion + OrientAlongPath: no POI from orbit source, rotation preserved") {
    auto desc = make_base_desc("golden.orbit.oap");
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.key(Frame{0}, 0.0f, Easing::Linear)
             .key(Frame{60}, 90.0f, Easing::Linear);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);
    desc.source = orbit;
    desc.orientation = OrientAlongPath{/*keep_horizon=*/false};

    CameraSession session;
    CAMERA_GOLDEN_GATE("OrbitOAP_F000", kRefOrbitOAP_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("OrbitOAP_F015", kRefOrbitOAP_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("OrbitOAP_F030", kRefOrbitOAP_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("OrbitOAP_F045", kRefOrbitOAP_F045,
        eval_at(kF045, desc, session));
}

// ═══════════════════════════════════════════════════════════════════════════
// §12 — Trajectory + LookAtPoint
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_trajectory_lookat — "
          "TrajectoryMotion Z -1500 → -500 + LookAtPoint at origin: position from "
          "trajectory, orientation from look-at") {
    auto desc = make_base_desc("golden.traj.lookat");
    auto traj = CameraTrajectoryBuilder()
                    .move_to(Vec3{0.0f, 0.0f, -1500.0f})
                    .bezier_to(Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, 0.0f},
                               Vec3{0.0f, 0.0f, -500.0f})
                    .duration_frames(90.0f)
                    .build();
    REQUIRE(traj);
    desc.source = TrajectoryMotion{traj, /*use_arc_length=*/true};
    desc.orientation = LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}};

    CameraSession session;
    CAMERA_GOLDEN_GATE("TrajLookAt_F000", kRefTrajLookAt_F000,
        eval_at(kF000, desc, session));
    CAMERA_GOLDEN_GATE("TrajLookAt_F015", kRefTrajLookAt_F015,
        eval_at(kF015, desc, session));
    CAMERA_GOLDEN_GATE("TrajLookAt_F030", kRefTrajLookAt_F030,
        eval_at(kF030, desc, session));
    CAMERA_GOLDEN_GATE("TrajLookAt_F045", kRefTrajLookAt_F045,
        eval_at(kF045, desc, session));
}
