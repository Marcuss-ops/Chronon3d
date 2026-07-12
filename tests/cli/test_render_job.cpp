// SPDX-License-Identifier: MIT
//
// tests/cli/test_render_job.cpp
//
// TICKET-MUSK-TEST-3 — render-job JSON adapter contract test.
//
// Locks the parse_render_job_json(...) contract:
//   * Valid job.json (required-only) → all defaults documented in CHANGELOG Test #3.
//   * Valid job.json (all fields)    → every optional field propagates as expected.
//   * Missing one of (comp_id|frames|output) → std::nullopt.
//   * Non-existent path                → std::nullopt.
//   * Malformed JSON                   → std::nullopt.
//   * still_args derived from render_args (thumbnail path).
//   * looks_like_job_json sanity: extension + openable.
//
// Link target: chronon3d_cli_tests via tests/cli_tests.cmake.

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "utils/job/render_job_json.hpp"

using namespace chronon3d::cli;

namespace {

// RAII temp JSON file: writes `contents` to a unique path on construct,
// removes on destruct. doctest does not provide a tmpdir helper so we
// roll a simple one that uses std::filesystem::temp_directory_path().
struct TmpJson {
    std::filesystem::path path;

    explicit TmpJson(const std::string& contents) {
        auto base = std::filesystem::temp_directory_path();
        auto name = std::string("chronon3d_test_render_job_") +
                    std::to_string(static_cast<long>(::getpid())) + "_" +
                    std::to_string(reinterpret_cast<uintptr_t>(this)) + ".json";
        path = base / name;
        std::ofstream out(path);
        out << contents;
    }

    ~TmpJson() {
        std::error_code ec;
        std::filesystem::remove(path, ec);  // best-effort
    }
};

} // namespace

TEST_CASE("render_job_json: parses valid job.json (required-only, defaults applied)") {
    TmpJson f(R"({
        "comp_id": "CompositionName",
        "frames": "0-90",
        "output": "/tmp/out.png"
    })");
    auto parsed = parse_render_job_json(f.path.string());
    REQUIRE(parsed.has_value());
    CHECK(parsed->render_args.comp_id == "CompositionName");
    CHECK(parsed->render_args.frames  == "0-90");
    CHECK(parsed->render_args.output  == "/tmp/out.png");

    // documented defaults — per AGENTS.md §regole "no stime percentuali":
    // these are EXPLICIT defaults, not estimated values.
    CHECK(parsed->render_args.log_level == "info");
    CHECK(parsed->render_args.pipeline.diagnostic == false);
    CHECK(parsed->render_args.pipeline.quality.motion_blur == false);
    CHECK(parsed->render_args.pipeline.quality.ssaa == doctest::Approx(1.0));
    CHECK(parsed->render_args.report == true);   // default true per CHANGELOG
    CHECK(parsed->want_thumbnail     == true);
    CHECK(parsed->want_subtitles     == false);
}

TEST_CASE("render_job_json: parses valid job.json (all fields explicit)") {
    TmpJson f(R"({
        "comp_id": "CinematicA",
        "frames": "0-90x5",
        "output": "/tmp/cinematicA.mp4",
        "log_level": "debug",
        "diagnostic": true,
        "motion_blur": true,
        "ssaa": 2.0,
        "report": false,
        "thumbnail": false,
        "subtitles": true
    })");
    auto parsed = parse_render_job_json(f.path.string());
    REQUIRE(parsed.has_value());
    CHECK(parsed->render_args.log_level == "debug");
    CHECK(parsed->render_args.pipeline.diagnostic == true);
    CHECK(parsed->render_args.pipeline.quality.motion_blur == true);
    CHECK(parsed->render_args.pipeline.quality.ssaa == doctest::Approx(2.0));
    CHECK(parsed->render_args.report == false);
    CHECK(parsed->want_thumbnail == false);
    CHECK(parsed->want_subtitles == true);
}

TEST_CASE("render_job_json: missing required field returns nullopt") {
    {
        TmpJson f(R"({"frames": "0-90", "output": "/tmp/out.png"})");
        CHECK_FALSE(parse_render_job_json(f.path.string()).has_value());
    }
    {
        TmpJson f(R"({"comp_id": "X", "output": "/tmp/out.png"})");
        CHECK_FALSE(parse_render_job_json(f.path.string()).has_value());
    }
    {
        TmpJson f(R"({"comp_id": "X", "frames": "0-90"})");
        CHECK_FALSE(parse_render_job_json(f.path.string()).has_value());
    }
}

TEST_CASE("render_job_json: bad path returns nullopt") {
    CHECK_FALSE(parse_render_job_json("/nonexistent/job.json").has_value());
}

TEST_CASE("render_job_json: malformed JSON returns nullopt") {
    TmpJson f(R"(not-json-at-all)");
    CHECK_FALSE(parse_render_job_json(f.path.string()).has_value());
}

TEST_CASE("render_job_json: non-object top-level returns nullopt") {
    TmpJson f(R"([1, 2, 3])");
    CHECK_FALSE(parse_render_job_json(f.path.string()).has_value());
}

TEST_CASE("render_job_json: still_args derived from render_args (thumbnail)") {
    TmpJson f(R"({
        "comp_id": "CompositionName",
        "frames": "10-90",
        "output": "/tmp/out.mp4",
        "thumbnail": true
    })");
    auto parsed = parse_render_job_json(f.path.string());
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->want_thumbnail);
    CHECK(parsed->still_args.comp_id == "CompositionName");
    CHECK(parsed->still_args.frame   == 10);     // first frame of range
    CHECK(parsed->still_args.output.find("thumbnail") != std::string::npos);
    CHECK(parsed->still_args.skip_preflight == true);
}

TEST_CASE("render_job_json: looks_like_job_json — extension + openable") {
    // non-existent .json path
    CHECK_FALSE(looks_like_job_json("/definitely/not/here.json"));

    // existing valid file
    TmpJson f("{}");
    CHECK(looks_like_job_json(f.path.string()));

    // wrong extension
    CHECK_FALSE(looks_like_job_json("foo.txt"));
    CHECK_FALSE(looks_like_job_json("foo"));
    CHECK_FALSE(looks_like_job_json("foo.JSON"));  // case-insensitive .json
}

TEST_CASE("render_job_json: log_level default is 'info' (CLI convention)") {
    TmpJson f(R"({
        "comp_id": "CompositionName",
        "frames": "0",
        "output": "/tmp/out.png"
    })");
    auto parsed = parse_render_job_json(f.path.string());
    REQUIRE(parsed.has_value());
    // RenderArgs.log_level default; assert it propagates through the adapter.
    CHECK(parsed->render_args.log_level == "info");
    CHECK(parsed->still_args.log_level  == "info");
}
