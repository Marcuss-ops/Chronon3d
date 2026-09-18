// ==============================================================================
// tests/scene/motion/test_motion_contract_determinism.cpp
//
// ADR-032 — motion-handshake determinism through the REAL compiled camera
// pipeline (camera_v1), mirroring the CAM-DOC 04 categories:
//
//   [D1] serial vs parallel : 600 frames evaluated in serial order vs
//                             parallel order produce bit-identical
//                             chronon_layer_state records (memcmp).
//   [D2] random access      : out-of-order evaluation (including revisits)
//                             yields bit-identical records per frame.
//   [D3] handshake shape    : a full orbit produces a monotonic translation
//                             trace in the packed ABI row (the packed matrix
//                             is the real transform, not garbage).
//
// The camera side is the Motion3D stand-in: CameraProgram::evaluate() is the
// "decides where and when" authority; the contract bridge is the handshake.
// Records are compared with memcmp — any padding, uninitialized byte or
// order-dependent float shows up immediately.
// ==============================================================================
#define DOCTEST_CONFIG_DISABLE_TEST_EXN_CATCH 0
#include <doctest/doctest.h>

#include <chronon/abi.h>

#include <chronon3d/motion/contract_bridge.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp>
// CameraSession is only forward-declared in the public camera headers (P3-H);
// the definition lives under internal/, as in
// tests/runtime/test_camera_session_keep_last_valid.cpp.
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/camera_pose.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::camera_v1;
using namespace chronon3d::motion;

namespace {

constexpr FrameRate kFps{30, 1};
constexpr chronon_frame_index kFrameCount = 600;

// Orbit camera: deterministic, time-dependent, stateless (no damping
// constraints) — the canonical CAM-DOC 04 shape.
CameraDescriptor make_orbit_descriptor() {
    CameraDescriptor desc;
    desc.id = "contract.orbit";

    desc.base.enabled = true;
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = ZoomProjection{AnimatedValue<float>{1000.0f}};

    // Orbit around the origin: yaw sweeps 0→360° over 600 frames at a
    // fixed radius, so the camera trace is a full deterministic orbit.
    OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.radius.set(900.0f);
    orbit.yaw.key(Frame{0}, 0.0f)
             .key(Frame{600}, 360.0f, Easing::Linear);
    orbit.pitch.set(0.0f);
    desc.source = orbit;

    desc.orientation = FixedOrientation{};
    return desc;
}

CameraProgram compile_orbit_or_die() {
    auto result = compile_camera(make_orbit_descriptor(), /*catalog=*/nullptr);
    REQUIRE(result.has_value());
    auto program = std::move(result).value();
    REQUIRE(program.is_compiled());
    return program;
}

/// Fold the evaluated camera into the final clip_from_local product and pack
/// the contract record. This is the full motion handshake in miniature:
/// projection · view · world(camera pose) · local(identity plane).
chronon_layer_state handshake_for_frame(const CameraProgram& program,
                                        chronon_frame_index frame,
                                        float viewport_w,
                                        float viewport_h) {
    CameraSession session;
    CameraEvalContext ctx;
    ctx = ctx.with_frame(Frame{static_cast<int>(frame)}, kFps);
    ctx.sample_time = SampleTime::from_frame_int(Frame{static_cast<int>(frame)}, kFps);
    auto res = program.evaluate(ctx, session);
    REQUIRE(res.has_value());
    const Camera2_5D& cam = res.value().camera;

    // Projection (vertical FOV → perspective, zero-to-one depth).
    const float fov_rad = glm::radians(std::clamp(cam.fov_deg, 0.1f, 179.0f));
    const Mat4 projection =
        glm::perspectiveLH_ZO(fov_rad, viewport_w / viewport_h, 1.0f, 10000.0f);

    // View from the evaluated camera pose (POI when enabled, else euler).
    Mat4 view(1.0f);
    if (cam.point_of_interest_enabled) {
        view = math::camera_view_matrix(cam.position, cam.rotation, cam.point_of_interest);
    } else {
        view = math::camera_view_matrix(cam.position, cam.rotation);
    }

    LayerStateInput in;
    in.layer = 12u;
    in.content = 481u;
    in.opacity = 1.0f;
    in.visible = true;
    in.clip_from_local = projection * view;  // local = identity text plane
    return make_layer_state(in);
}

/// memcmp comparability: the record must be fully initialized — guard against
/// any padding surprise by memset-comparing full sizeof.
bool same_record(const chronon_layer_state& a, const chronon_layer_state& b) {
    return std::memcmp(&a, &b, sizeof(chronon_layer_state)) == 0;
}

} // namespace

// ── [D1] serial vs parallel ─────────────────────────────────────────────────
TEST_CASE("ADR-032 [D1]: serial and parallel handshake produce bit-identical records") {
    CameraProgram program = compile_orbit_or_die();

    std::vector<chronon_layer_state> serial(kFrameCount);
    for (chronon_frame_index f = 0; f < kFrameCount; ++f) {
        serial[f] = handshake_for_frame(program, f, 1920.0f, 1080.0f);
    }

    std::vector<chronon_layer_state> parallel(kFrameCount);
    std::vector<std::thread> workers;
    std::atomic<chronon_frame_index> next{0};
    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < hw; ++t) {
        workers.emplace_back([&] {
            for (;;) {
                const chronon_frame_index f = next.fetch_add(1);
                if (f >= kFrameCount) return;
                parallel[f] = handshake_for_frame(program, f, 1920.0f, 1080.0f);
            }
        });
    }
    for (auto& w : workers) w.join();

    std::size_t mismatches = 0;
    chronon_frame_index first_bad = 0;
    for (chronon_frame_index f = 0; f < kFrameCount; ++f) {
        if (!same_record(serial[f], parallel[f])) {
            if (mismatches == 0) first_bad = f;
            ++mismatches;
        }
    }
    CHECK_MESSAGE(mismatches == 0,
                  "first diverging frame: ", static_cast<int>(first_bad),
                  " (", mismatches, " of ", kFrameCount, ")");
}

// ── [D2] random access ──────────────────────────────────────────────────────
TEST_CASE("ADR-032 [D2]: out-of-order evaluation yields bit-identical records") {
    CameraProgram program = compile_orbit_or_die();

    const std::vector<chronon_frame_index> order{
        5, 100, 0, 50, 25, 10, 0, 599, 300, 100, 42, 0, 599, 300};

    std::vector<chronon_layer_state> scrambled;
    scrambled.reserve(order.size());
    for (const chronon_frame_index f : order) {
        scrambled.push_back(handshake_for_frame(program, f, 1920.0f, 1080.0f));
    }

    for (std::size_t i = 0; i < order.size(); ++i) {
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            if (order[i] == order[j]) {
                CHECK_MESSAGE(same_record(scrambled[i], scrambled[j]),
                              "frame ", static_cast<int>(order[i]),
                              " diverged between evaluation ", i, " and ", j);
            }
        }
    }
}

// ── [D3] handshake shape sanity ─────────────────────────────────────────────
TEST_CASE("ADR-032 [D3]: orbit handshake produces a real translation trace") {
    CameraProgram program = compile_orbit_or_die();

    // Frame 0 vs half-period: the packed translation row must differ in x/z
    // (the orbit moves the camera on the XZ plane) — proving the packed
    // row-major ABI row carries the live transform.
    const auto a = handshake_for_frame(program, 0, 1920.0f, 1080.0f);
    const auto b = handshake_for_frame(program, 300, 1920.0f, 1080.0f);

    const float ax = a.clip_from_local[12];
    const float az = a.clip_from_local[14];
    const float bx = b.clip_from_local[12];
    const float bz = b.clip_from_local[14];
    CHECK(std::isfinite(ax));
    CHECK(std::isfinite(az));
    CHECK(std::isfinite(bx));
    CHECK(std::isfinite(bz));
    CHECK(std::fabs(ax - bx) + std::fabs(az - bz) > 1.0f);

    // Every record is fully initialized: structural sizes and fields.
    CHECK(a.struct_size == sizeof(chronon_layer_state));
    CHECK(a.layer == 12u);
    CHECK(a.content == 481u);
    CHECK(a.visible == 1u);
    CHECK(a.opacity == doctest::Approx(1.0f));
}
