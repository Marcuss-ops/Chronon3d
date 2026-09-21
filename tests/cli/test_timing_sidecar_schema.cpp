// Contract test for the single v2 timing sidecar.

#include <doctest/doctest.h>

#include "commands/video/exporters/pipe_timing_sidecar.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>

TEST_CASE("video timing sidecar emits one v2 document with an inline summary") {
    const auto video = std::filesystem::temp_directory_path()
        / "chronon3d_timing_sidecar_schema_test.mp4";
    const auto sidecar = std::filesystem::path(video.string() + ".timing.json");
    const auto legacy_summary =
        std::filesystem::path(video.string() + ".telemetry-summary.json");
    std::filesystem::remove(sidecar);
    std::filesystem::remove(legacy_summary);

    chronon3d::telemetry::FrameTelemetry frame;
    frame.frame_number = 0;
    frame.duration_ms = 1.0;
    chronon3d::cli::pipe_timing::JobTimings timings;

    chronon3d::cli::write_frame_timing_sidecar(
        video.string(), {frame}, {}, 1.0, 1.0, 0.0, timings, false);

    REQUIRE(std::filesystem::exists(sidecar));
    std::ifstream input(sidecar);
    const auto document = nlohmann::json::parse(input);

    CHECK(document.at("schema") == chronon3d::cli::pipe_timing::kTimingSidecarSchema);
    CHECK(document.at("version") == chronon3d::cli::pipe_timing::kTimingSidecarVersion);
    const std::set<std::string> v2_keys{
        "schema", "version", "video", "wall_time_ms", "render_ms",
        "encode_close_ms", "frames_total", "time_source", "frame_times_ms",
        "summary", "job", "cache", "memory", "exclusive_wall_timeline",
        "phase_attribution", "internal_profiling", "startup_breakdown",
        "prepare_breakdown",
    };
    std::set<std::string> emitted_keys;
    for (const auto& item : document.items()) emitted_keys.insert(item.key());
    CHECK(emitted_keys == v2_keys);
    CHECK_FALSE(std::filesystem::exists(legacy_summary));

    std::filesystem::remove(sidecar);
}
