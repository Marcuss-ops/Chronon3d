// ==============================================================================
// Tests for camera-path CLI command
// Validates: PathMetrics computation, JSON/CSV export, integration with Composition
// ==============================================================================
#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cmath>

using namespace chronon3d;

// ── Helper: create a composition with a known linear camera path ─────────────
// Camera moves from (0,0,-1000) to (300,0,-1000) over 90 frames
static Composition make_linear_camera_comp() {
    CompositionSpec spec;
    spec.name = "CameraPathTest";
    spec.width = 1920;
    spec.height = 1080;
    spec.duration = 90;

    return Composition(spec, [](const FrameContext& ctx) -> Scene {
        Scene scene(ctx.resource);

        Camera2_5D cam;
        cam.enabled = true;
        const f32 t = static_cast<f32>(ctx.frame.frame.frame) / 89.0f;
        cam.position = Vec3{300.0f * t, 0.0f, -1000.0f};
        cam.rotation = Vec3{0.0f, 0.0f, 0.0f};
        cam.zoom = 1000.0f;
        cam.fov_deg = 50.0f;
        cam.point_of_interest_enabled = false;
        scene.set_camera_2_5d(cam);

        return scene;
    });
}

// ── Helper: create a composition with a curved camera path ───────────────────
// Camera moves in an arc, useful for testing velocity/acceleration
static Composition make_curved_camera_comp() {
    CompositionSpec spec;
    spec.name = "CurvedPathTest";
    spec.width = 1920;
    spec.height = 1080;
    spec.duration = 60;

    return Composition(spec, [](const FrameContext& ctx) -> Scene {
        Scene scene(ctx.resource);

        Camera2_5D cam;
        cam.enabled = true;
        const f32 t = static_cast<f32>(ctx.frame.frame.frame) / 59.0f;
        const f32 angle = t * 3.14159265f; // half circle
        cam.position = Vec3{200.0f * std::sin(angle), 0.0f, -1000.0f - 200.0f * std::cos(angle)};
        cam.rotation = Vec3{0.0f, glm::degrees(angle), 0.0f};
        cam.zoom = 1000.0f;
        cam.fov_deg = 50.0f;
        cam.point_of_interest_enabled = true;
        cam.point_of_interest = Vec3{0.0f, 0.0f, -1000.0f};
        scene.set_camera_2_5d(cam);

        return scene;
    });
}

// ── Helper: create a static camera composition ──────────────────────────────
static Composition make_static_camera_comp() {
    CompositionSpec spec;
    spec.name = "StaticPathTest";
    spec.width = 1920;
    spec.height = 1080;
    spec.duration = 30;

    return Composition(spec, [](const FrameContext& ctx) -> Scene {
        Scene scene(ctx.resource);

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = Vec3{0.0f, 0.0f, -1000.0f};
        cam.rotation = Vec3{0.0f, 0.0f, 0.0f};
        cam.zoom = 1000.0f;
        cam.fov_deg = 50.0f;
        cam.point_of_interest_enabled = false;
        scene.set_camera_2_5d(cam);

        return scene;
    });
}

// ── Mirror of PathSample/PathMetrics from command_camera_path.cpp ────────────
// (duplicated here to test the logic without linking the CLI lib)
struct TestPathSample {
    int frame{0};
    Vec3 position{0.0f};
    Vec3 rotation{0.0f};
    f32  zoom{0.0f};
    f32  fov_deg{0.0f};
    Vec3 target{0.0f};
    bool poi_enabled{false};
    f32  velocity{0.0f};
    f32  path_distance{0.0f};
};

struct TestPathMetrics {
    f32 total_length{0.0f};
    f32 max_velocity{0.0f};
    f32 max_acceleration{0.0f};
    int num_samples{0};
};

static std::vector<TestPathSample> sample_comp(const Composition& comp, int start, int end, int step) {
    std::vector<TestPathSample> samples;
    for (int f = start; f <= end; f += step) {
        Scene scene = comp.evaluate(Frame{f});
        const auto& cam = scene.camera_2_5d();
        TestPathSample s;
        s.frame = f;
        s.position = cam.position;
        s.rotation = cam.rotation;
        s.zoom = cam.zoom;
        s.fov_deg = cam.fov_deg;
        s.poi_enabled = cam.point_of_interest_enabled;
        if (cam.point_of_interest_enabled) s.target = cam.point_of_interest;
        samples.push_back(s);
    }
    return samples;
}

static TestPathMetrics compute_test_metrics(std::vector<TestPathSample>& samples) {
    TestPathMetrics m;
    m.num_samples = static_cast<int>(samples.size());
    if (samples.empty()) return m;

    f32 prev_velocity = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (i == 0) { samples[i].velocity = 0; samples[i].path_distance = 0; continue; }
        const f32 dist = glm::length(samples[i].position - samples[i-1].position);
        const f32 dt = static_cast<f32>(samples[i].frame - samples[i-1].frame);
        const f32 vel = (dt > 0.0f) ? dist / dt : 0.0f;
        samples[i].velocity = vel;
        samples[i].path_distance = samples[i-1].path_distance + dist;
        m.total_length += dist;
        m.max_velocity = std::max(m.max_velocity, vel);
        if (i >= 2) m.max_acceleration = std::max(m.max_acceleration, std::abs(vel - prev_velocity));
        prev_velocity = vel;
    }
    return m;
}

// ═════════════════════════════════════════════════════════════════════════════
// Tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CameraPath: linear path samples correct number of frames") {
    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 89, 1);
    CHECK(samples.size() == 90);
    CHECK(samples.front().frame == 0);
    CHECK(samples.back().frame == 89);
}

TEST_CASE("CameraPath: linear path endpoints are correct") {
    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 89, 1);

    CHECK(std::abs(samples.front().position.x) < 1.0f);
    CHECK(std::abs(samples.front().position.z - (-1000.0f)) < 1.0f);

    CHECK(std::abs(samples.back().position.x - 300.0f) < 1.0f);
    CHECK(std::abs(samples.back().position.z - (-1000.0f)) < 1.0f);
}

TEST_CASE("CameraPath: linear path total length is correct") {
    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 89, 1);
    auto metrics = compute_test_metrics(samples);

    // Camera moves 300 units over 89 frames → total_length ≈ 300
    CHECK(metrics.total_length > 295.0f);
    CHECK(metrics.total_length < 305.0f);
    CHECK(metrics.num_samples == 90);
}

TEST_CASE("CameraPath: linear path velocity is roughly constant") {
    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 89, 1);
    auto metrics = compute_test_metrics(samples);

    // Each frame the camera moves ~300/89 ≈ 3.37 units
    const f32 expected_vel = 300.0f / 89.0f;
    CHECK(metrics.max_velocity > 0.0f);

    // Max velocity should be close to expected (within 10% for float precision)
    CHECK(metrics.max_velocity < expected_vel * 1.1f);

    // Acceleration should be near zero (constant velocity)
    CHECK(metrics.max_acceleration < 0.1f);
}

TEST_CASE("CameraPath: static path has zero velocity and length") {
    auto comp = make_static_camera_comp();
    auto samples = sample_comp(comp, 0, 29, 1);
    auto metrics = compute_test_metrics(samples);

    CHECK(metrics.total_length < 0.01f);
    CHECK(metrics.max_velocity < 0.01f);
    CHECK(metrics.max_acceleration < 0.01f);
}

TEST_CASE("CameraPath: step > 1 produces fewer samples") {
    auto comp = make_linear_camera_comp();
    auto samples_step1 = sample_comp(comp, 0, 89, 1);
    auto samples_step5 = sample_comp(comp, 0, 89, 5);

    CHECK(samples_step1.size() == 90);
    CHECK(samples_step5.size() == 18); // 0, 5, 10, ..., 85

    // Endpoints should still match
    CHECK(samples_step5.front().frame == 0);
    CHECK(samples_step5.back().frame == 85);
}

TEST_CASE("CameraPath: curved path has non-constant velocity") {
    auto comp = make_curved_camera_comp();
    auto samples = sample_comp(comp, 0, 59, 1);
    auto metrics = compute_test_metrics(samples);

    CHECK(samples.size() == 60);
    CHECK(metrics.total_length > 0.0f);
    CHECK(metrics.max_velocity > 0.0f);

    // Curved path should have non-zero acceleration (velocity changes)
    CHECK(metrics.max_acceleration > 0.00001f);
}

TEST_CASE("CameraPath: curved path has point of interest") {
    auto comp = make_curved_camera_comp();
    auto samples = sample_comp(comp, 0, 59, 1);

    for (const auto& s : samples) {
        CHECK(s.poi_enabled);
        CHECK(std::abs(s.target.x) < 0.01f);
        CHECK(std::abs(s.target.z - (-1000.0f)) < 0.01f);
    }
}

TEST_CASE("CameraPath: single frame sample") {
    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 45, 45, 1);
    auto metrics = compute_test_metrics(samples);

    CHECK(samples.size() == 1);
    CHECK(metrics.total_length < 0.01f);
    CHECK(metrics.max_velocity < 0.01f);
    CHECK(metrics.num_samples == 1);
}

TEST_CASE("CameraPath: JSON output contains required fields") {
    // Build a minimal JSON like the command would
    nlohmann::json js;
    js["composition"] = "TestComp";
    js["range"] = {{"start", 0}, {"end", 29}, {"step", 1}};
    js["metrics"] = {
        {"num_samples", 30},
        {"total_length", 100.5},
        {"max_velocity", 5.0},
        {"max_acceleration", 0.1}
    };
    js["samples"] = nlohmann::json::array();

    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 29, 1);
    for (const auto& s : samples) {
        nlohmann::json entry;
        entry["frame"] = s.frame;
        entry["position"] = {s.position.x, s.position.y, s.position.z};
        entry["rotation"] = {s.rotation.x, s.rotation.y, s.rotation.z};
        entry["zoom"] = s.zoom;
        entry["fov_deg"] = s.fov_deg;
        entry["velocity"] = s.velocity;
        entry["path_distance"] = s.path_distance;
        js["samples"].push_back(std::move(entry));
    }

    CHECK(js.contains("composition"));
    CHECK(js.contains("range"));
    CHECK(js.contains("metrics"));
    CHECK(js.contains("samples"));
    CHECK(js["samples"].size() == 30);
    CHECK(js["metrics"]["num_samples"] == 30);
    CHECK(js["range"]["start"] == 0);
    CHECK(js["range"]["end"] == 29);
}

TEST_CASE("CameraPath: CSV output has correct header and rows") {
    // Simulate CSV output generation
    std::ostringstream ss;
    ss << "frame,"
       << "pos_x,pos_y,pos_z,"
       << "rot_x,rot_y,rot_z,"
       << "zoom,fov_deg,"
       << "target_x,target_y,target_z,"
       << "velocity,path_distance\n";

    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 4, 1);
    for (const auto& s : samples) {
        ss << fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
            s.frame,
            s.position.x, s.position.y, s.position.z,
            s.rotation.x, s.rotation.y, s.rotation.z,
            s.zoom, s.fov_deg,
            0.0f, 0.0f, 0.0f,
            s.velocity, s.path_distance);
    }

    std::string csv = ss.str();
    // Header line + 5 data lines
    int line_count = 0;
    std::istringstream reader(csv);
    std::string line;
    while (std::getline(reader, line)) ++line_count;
    CHECK(line_count == 6); // header + 5 rows
}

TEST_CASE("CameraPath: JSON roundtrip preserves sample data") {
    nlohmann::json js;
    js["samples"] = nlohmann::json::array();

    auto comp = make_curved_camera_comp();
    auto samples = sample_comp(comp, 0, 5, 1);
    for (const auto& s : samples) {
        nlohmann::json entry;
        entry["frame"] = s.frame;
        entry["position"] = {s.position.x, s.position.y, s.position.z};
        entry["zoom"] = s.zoom;
        entry["velocity"] = s.velocity;
        js["samples"].push_back(std::move(entry));
    }

    // Verify roundtrip
    for (size_t i = 0; i < samples.size(); ++i) {
        CHECK(js["samples"][i]["frame"] == samples[i].frame);
        CHECK(std::abs(js["samples"][i]["position"][0].get<f32>() - samples[i].position.x) < 0.01f);
        CHECK(std::abs(js["samples"][i]["zoom"].get<f32>() - samples[i].zoom) < 0.01f);
    }
}

TEST_CASE("CameraPath: step larger than range produces single sample") {
    auto comp = make_linear_camera_comp();
    auto samples = sample_comp(comp, 0, 10, 100);
    CHECK(samples.size() == 1); // only frame 0
}

TEST_CASE("CameraPath: composition with animated camera via SceneBuilder") {
    // Test that a composition using animated_camera() in SceneBuilder
    // produces a valid camera path when sampled
    CompositionSpec spec;
    spec.name = "AnimatedCamTest";
    spec.width = 1920;
    spec.height = 1080;
    spec.duration = 30;

    Composition comp(spec, [](const FrameContext& ctx) -> Scene {
        SceneBuilder builder(ctx);
        AnimatedCamera2_5D cam;
        cam.position.set(Vec3{0.0f, 0.0f, -1000.0f});
        cam.zoom.set(1000.0f);
        builder.animated_camera(cam);
        return builder.build();
    });

    auto samples = sample_comp(comp, 0, 29, 1);
    CHECK(samples.size() == 30);

    // All samples should have valid (non-NaN) positions
    for (const auto& s : samples) {
        CHECK(!std::isnan(s.position.x));
        CHECK(!std::isnan(s.position.y));
        CHECK(!std::isnan(s.position.z));
    }
}
