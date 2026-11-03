#include <doctest/doctest.h>

#include <apps/chronon3d_cli/daemon/daemon_service_ipc_protocol.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace chronon3d::cli;

namespace {

// One unique file per case: the parser reads the document from disk, so the
// test writes the exact plans it is asserting on instead of a fixture that can
// drift away from the shape under test.
class TempPlan {
public:
    explicit TempPlan(std::string_view body) {
        static std::uint64_t counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("chronon-plan-shape-" + std::to_string(stamp) + "-" +
                 std::to_string(counter++) + ".json");
        std::ofstream file(path_);
        file << body;
    }
    ~TempPlan() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    TempPlan(const TempPlan&) = delete;
    TempPlan& operator=(const TempPlan&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("plan admission shape: a composited overlay plan is one execution domain") {
    // The measured shape of the GPU admission run (color + two text layers on a
    // 1080p canvas). It renders through FullGraph, so a parallel_disjoint hint
    // must not promote it into the shared domain.
    const TempPlan plan(R"({
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "canvas": {"width": 1920, "height": 1080, "fps_num": 24, "fps_den": 1, "duration_frames": 48},
        "layers": [
            {"id": "background", "type": "color", "color": [0, 0, 0, 1]},
            {"id": "title", "type": "text", "text": "CHRONON"},
            {"id": "subtitle", "type": "text", "text": "overlay"}
        ],
        "output": {"path": "out.mp4", "format": "mp4", "codec": "h264"}
    })");

    PlanAdmissionShape shape;
    const auto status = extract_plan_admission_shape(plan.path(), shape);
    CHECK(status == PlanDimensions::Resolved);
    CHECK(shape.width == 1920);
    CHECK(shape.height == 1080);
    CHECK(shape.requires_composition);
}

TEST_CASE("plan admission shape: a single plain video layer stays on the direct lane") {
    const TempPlan plan(R"({
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "canvas": {"width": 1920, "height": 1080, "fps_num": 30, "fps_den": 1, "duration_frames": 16},
        "layers": [
            {"id": "source", "type": "video", "source": "clip.mp4", "size": [1920, 1080],
             "fit": "stretch", "start_frame": 0, "duration_frames": 16}
        ],
        "output": {"path": "out.mp4", "format": "mp4", "codec": "h264"}
    })");

    PlanAdmissionShape shape;
    const auto status = extract_plan_admission_shape(plan.path(), shape);
    CHECK(status == PlanDimensions::Resolved);
    CHECK_FALSE(shape.requires_composition);
}

TEST_CASE("plan admission shape: effects on a single video layer force composition") {
    const TempPlan plan(R"({
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "canvas": {"width": 1920, "height": 1080, "fps_num": 30, "fps_den": 1, "duration_frames": 16},
        "layers": [
            {"id": "source", "type": "video", "source": "clip.mp4",
             "effects": [{"type": "blur", "radius": 8}]}
        ]
    })");

    PlanAdmissionShape shape;
    CHECK(extract_plan_admission_shape(plan.path(), shape) == PlanDimensions::Resolved);
    CHECK(shape.requires_composition);
}

TEST_CASE("plan admission shape: two video layers force composition") {
    const TempPlan plan(R"({
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "canvas": {"width": 1080, "height": 1920, "fps_num": 30, "fps_den": 1, "duration_frames": 16},
        "layers": [
            {"id": "a", "type": "video", "source": "a.mp4"},
            {"id": "b", "type": "video", "source": "b.mp4"}
        ]
    })");

    PlanAdmissionShape shape;
    CHECK(extract_plan_admission_shape(plan.path(), shape) == PlanDimensions::Resolved);
    CHECK(shape.requires_composition);
}

TEST_CASE("plan admission shape: a missing layer list is treated as composition") {
    const TempPlan plan(R"({
        "schema": "chronon.render-plan.v2",
        "version": 2,
        "canvas": {"width": 1920, "height": 1080, "fps_num": 30, "fps_den": 1, "duration_frames": 16}
    })");

    PlanAdmissionShape shape;
    CHECK(extract_plan_admission_shape(plan.path(), shape) == PlanDimensions::Resolved);
    CHECK(shape.requires_composition);
}

TEST_CASE("plan admission shape: an unreadable plan never lowers the admission class") {
    PlanAdmissionShape shape;
    const auto status = extract_plan_admission_shape(
        std::filesystem::path{"/nonexistent/chronon-plan-shape.json"}, shape);
    CHECK(status == PlanDimensions::Unreadable);
    CHECK(shape.requires_composition);
}
