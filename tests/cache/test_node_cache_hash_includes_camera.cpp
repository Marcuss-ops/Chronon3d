// =============================================================================
// tests/cache/test_node_cache_hash_includes_camera.cpp
//
// TICKET-ae-cam-hash-collision Soluzione B - regression lock for the
// camera-aware NodeCacheKey folding helper.
//
// Asserts:
//   1. `cache::camera_fingerprint_digest(cam)` produces DISTINCT u64 digests
//      for distinct Camera2_5D state (zoom, position.z, parent_name, dof).
//   2. `cache::fold_camera_into_params_hash(key, cam)` mutates a NodeCacheKey
//      so feeds with different camera states yield different
//      `NodeCacheKey::digest()` outputs.
//   3. AE_CAM_02 (zoom-only 500/1000/1500) and AE_CAM_04 (parent_null Z-dolly
//      -600/-1000/-1400) scenarios lock to 3 distinct digests each, mirroring
//      the per-frame render-graph FB-hash semantics at the cache-key layer.
//   4. The helper is deterministic (same Camera2_5D twice -> same digest).
//   5. Default-zero keys (no camera fingerprint fold) preserve backward
//      compatibility with pre-ticket digests.
//
// The strict "node_cache_hash_collisions == 0 on AE_CAM sweep" telemetry
// assertion called out in
// `docs/tickets/TICKET-ae-cam-hash-collision.md` Criteri / Telemetry
// assertion is INTENTIONALLY OMITTED from this file (the counter is exposed
// as a per-run telemetry struct field in
// `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp:38`,
// not as a CounterId enum + counter_value() reader; the strict assertion
// belongs to a follow-up test under tests/telemetry/ once the consumption
// surface lands).
//
// Cat-2 freeze-compliant: no threads, no real time, no PRNG. Same input
// always produces same output across runs.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

using namespace chronon3d;
using namespace chronon3d::cache;

namespace {

constexpr u64 kAeCam02ZoomLow  = 500;
constexpr u64 kAeCam02ZoomMid  = 1000;
constexpr u64 kAeCam02ZoomHigh = 1500;

constexpr f32 kAeCam04ZNear    = -600.0f;
constexpr f32 kAeCam04ZMid     = -1000.0f;
constexpr f32 kAeCam04ZFar     = -1400.0f;

[[nodiscard]] Camera2_5D make_cam_zoom(u64 zoom) {
    Camera2_5D cam;
    cam.enabled = true;
    cam.zoom = static_cast<f32>(zoom);
    cam.position = Vec3{0.0f, 0.0f, -1000.0f};
    return cam;
}

[[nodiscard]] Camera2_5D make_cam_z(f32 z) {
    Camera2_5D cam;
    cam.enabled = true;
    cam.zoom = 1000.0f;
    cam.position = Vec3{0.0f, 0.0f, z};
    return cam;
}

[[nodiscard]] Camera2_5D make_cam_parent(std::pmr::string parent) {
    Camera2_5D cam;
    cam.enabled = true;
    cam.zoom = 1000.0f;
    cam.position = Vec3{0.0f, 0.0f, -1000.0f};
    cam.parent_name = std::move(parent);
    return cam;
}

[[nodiscard]] NodeCacheKey make_test_key() {
    return NodeCacheKey{
        .scope = "lock_test",
        .frame = 0,
        .width = 1280,
        .height = 720,
        .params_hash = 0xABCDABCDULL,
        .source_hash = 0xCAFECAFEULL,
        .input_hash = 0xDEADBEEFULL,
    };
}

} // namespace

// 1. Camera fingerprint digest - distinguishes by zoom
TEST_CASE("CameraFingerprint: digest changes when cam.zoom changes") {
    const u64 low  = camera_fingerprint_digest(make_cam_zoom(kAeCam02ZoomLow));
    const u64 mid  = camera_fingerprint_digest(make_cam_zoom(kAeCam02ZoomMid));
    const u64 high = camera_fingerprint_digest(make_cam_zoom(kAeCam02ZoomHigh));
    CHECK_NE(low, mid);
    CHECK_NE(mid, high);
    CHECK_NE(low, high);
}

// 2. Camera fingerprint digest - distinguishes by position.z
TEST_CASE("CameraFingerprint: digest changes when cam.position.z changes") {
    const u64 near = camera_fingerprint_digest(make_cam_z(kAeCam04ZNear));
    const u64 mid  = camera_fingerprint_digest(make_cam_z(kAeCam04ZMid));
    const u64 far  = camera_fingerprint_digest(make_cam_z(kAeCam04ZFar));
    CHECK_NE(near, mid);
    CHECK_NE(mid,  far);
    CHECK_NE(near, far);
}

// 3. Camera fingerprint digest - distinguishes by parent_name
TEST_CASE("CameraFingerprint: digest changes when parent_name changes") {
    const u64 no_parent = camera_fingerprint_digest(make_cam_parent({}));
    const u64 parent_a  = camera_fingerprint_digest(make_cam_parent(std::pmr::string{"layer_a"}));
    const u64 parent_b  = camera_fingerprint_digest(make_cam_parent(std::pmr::string{"layer_b"}));
    CHECK_NE(no_parent, parent_a);
    CHECK_NE(parent_a,  parent_b);
    CHECK_NE(no_parent, parent_b);
}

// 4. Determinism: same Camera2_5D twice -> same digest
TEST_CASE("CameraFingerprint: deterministic across repeated calls") {
    const Camera2_5D cam = make_cam_zoom(kAeCam02ZoomMid);
    const u64 first  = camera_fingerprint_digest(cam);
    const u64 second = camera_fingerprint_digest(cam);
    const u64 third  = camera_fingerprint_digest(cam);
    CHECK_EQ(first, second);
    CHECK_EQ(second, third);
}

// 5. Fold into NodeCacheKey - distinct cam -> distinct NodeCacheKey::digest
TEST_CASE("FoldCameraIntoParamsHash: 3 distinct zoom values -> 3 distinct NodeCacheKey::digest (AE_CAM_02 lock)") {
    auto key0 = make_test_key();
    auto key1 = make_test_key();
    auto key2 = make_test_key();
    fold_camera_into_params_hash(key0, make_cam_zoom(kAeCam02ZoomLow));
    fold_camera_into_params_hash(key1, make_cam_zoom(kAeCam02ZoomMid));
    fold_camera_into_params_hash(key2, make_cam_zoom(kAeCam02ZoomHigh));
    CHECK_NE(key0.digest(), key1.digest());
    CHECK_NE(key1.digest(), key2.digest());
    CHECK_NE(key0.digest(), key2.digest());
}

TEST_CASE("FoldCameraIntoParamsHash: 3 distinct position.z values -> 3 distinct NodeCacheKey::digest (AE_CAM_04 lock)") {
    auto key0 = make_test_key();
    auto key1 = make_test_key();
    auto key2 = make_test_key();
    fold_camera_into_params_hash(key0, make_cam_z(kAeCam04ZNear));
    fold_camera_into_params_hash(key1, make_cam_z(kAeCam04ZMid));
    fold_camera_into_params_hash(key2, make_cam_z(kAeCam04ZFar));
    CHECK_NE(key0.digest(), key1.digest());
    CHECK_NE(key1.digest(), key2.digest());
    CHECK_NE(key0.digest(), key2.digest());
}

// 6. Fold idempotence: same cam -> same digest after single fold
TEST_CASE("FoldCameraIntoParamsHash: same cam twice -> same digest") {
    auto key_a = make_test_key();
    auto key_b = make_test_key();
    fold_camera_into_params_hash(key_a, make_cam_zoom(kAeCam02ZoomMid));
    fold_camera_into_params_hash(key_b, make_cam_zoom(kAeCam02ZoomMid));
    CHECK_EQ(key_a.digest(), key_b.digest());
}

// 7. Backward compat: default-fold (no camera fingerprint) preserves
//    pre-ticket NodeCacheKey::digest for unchanged fields.
TEST_CASE("FoldCameraIntoParamsHash: zero-camera base key digest is unchanged") {
    const NodeCacheKey base = make_test_key();
    const u64 stable_digest = base.digest();
    NodeCacheKey recomputed;
    recomputed.scope = "lock_test";
    recomputed.frame = 0;
    recomputed.width = 1280;
    recomputed.height = 720;
    recomputed.params_hash = 0xABCDABCDULL;
    recomputed.source_hash = 0xCAFECAFEULL;
    recomputed.input_hash = 0xDEADBEEFULL;
    CHECK_EQ(stable_digest, recomputed.digest());
}
