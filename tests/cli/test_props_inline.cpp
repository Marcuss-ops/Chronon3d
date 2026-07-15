// test_props_inline.cpp — Phase 1c / Increment B
// ───────────────────────────────────────────────────────────────────────────
//
// Unit tests for the canonical CLI helpers in
// `apps/chronon3d_cli/utils/common/props_inline.hpp`:
//
// load_props_inline (5 TEST_CASEs):
//   #1 — PASS: scalar string/bool/number values populated into ValueMap.
//   #2 — FAIL: nested object value rejected.
//   #3 — FAIL: array value rejected.
//   #4 — FAIL: null value rejected.
//   #5 — FAIL: invalid JSON syntax rejected.
//
// load_props_input (4 TEST_CASEs):
//   #6 — FAIL: --props-file + --props-json mutually exclusive.
//   #7 — PASS: only --props-file → delegates to load_props_file.
//   #8 — PASS: only --props-json → delegates to load_props_inline.
//   #9 — PASS: both empty → ok=true + empty props (defaults path).
//
// Filesystem-backed test (#7) writes a tmp JSON file under /tmp strictly
// for the duration of the test.  AGENTS.md Cat-3 anti-dup regression:
// the test guards the canonical helper contract (3 callsites: render,
// validate, resolve) — see `tools/check_doc_sync.sh` for the gate.

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/utils/common/props_file.hpp>
#include <apps/chronon3d_cli/utils/common/props_inline.hpp>

#include <chronon3d/timeline/composition_props.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace chronon3d::cli;

namespace {

/// Write a JSON object to a tmp file and return its path.  Caller is
/// responsible for the cleanup-after-test invariant (tmp auto-cleaned
/// by OS, but the test does NOT leave /tmp dumps in steady-state via
/// the RAII guard-cleanup in test #7).
std::string write_tmp_json_file(const std::string& json_text) {
    static int counter = 0;
    const std::string path = "/tmp/chronon3d_props_inline_test_"
                           + std::to_string(++counter) + ".json";
    std::ofstream out(path);
    out << json_text;
    out.close();
    return path;
}

} // anonymous namespace

// #1 — load_props_inline: scalar string/bool/number → ok=true with populated props.
TEST_CASE("props_inline #1: scalar string/bool/number populated (ok=true)") {
    const auto r = load_props_inline(
        R"({"title":"Breaking News","duration":300,"featured":true})");
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.keys.size() == 3);
    CHECK(r.props.values.get_string("title")     == "Breaking News");
    CHECK(r.props.values.get_string("duration")  == "300");  // nlohmann dump format
    CHECK(r.props.values.get_string("featured")  == "true");
}

// #2 — load_props_inline: nested object rejected.
TEST_CASE("props_inline #2: nested object value rejected (ok=false)") {
    const auto r = load_props_inline(
        R"({"meta":{"nested":true},"title":"ok"})");
    REQUIRE_FALSE(r.ok);
    CHECK(r.props.values.contains("title") == false);  // rollback on failure
    CHECK(r.error.find("nested") != std::string::npos
          || r.error.find("scalar") != std::string::npos);
}

// #3 — load_props_inline: array value rejected.
TEST_CASE("props_inline #3: array value rejected (ok=false)") {
    const auto r = load_props_inline(
        R"({"tags":["breaking","news"],"title":"ok"})");
    REQUIRE_FALSE(r.ok);
    CHECK(r.error.find("array") != std::string::npos
          || r.error.find("scalar") != std::string::npos);
}

// #4 — load_props_inline: null value rejected.
TEST_CASE("props_inline #4: null value rejected (ok=false)") {
    const auto r = load_props_inline(
        R"({"author":null,"title":"ok"})");
    REQUIRE_FALSE(r.ok);
    CHECK(r.error.find("scalar") != std::string::npos);
}

// #5 — load_props_inline: invalid JSON syntax rejected.
TEST_CASE("props_inline #5: malformed JSON rejected (ok=false)") {
    const auto r = load_props_inline("{ this is not valid JSON");
    REQUIRE_FALSE(r.ok);
    CHECK(r.error.find("Invalid") != std::string::npos
          || r.error.find("JSON") != std::string::npos);
}

// #6 — load_props_input: --props-file + --props-json mutually exclusive.
TEST_CASE("props_input #6: both flags set → mutual-exclusion error") {
    const auto r = load_props_input("/tmp/some.json",
                                    R"({"title":"ignored"})");
    REQUIRE_FALSE(r.ok);
    CHECK(r.error.find("mutually exclusive") != std::string::npos);
}

// #7 — load_props_input: only file → delegates to load_props_file.
TEST_CASE("props_input #7: only file set → delegates to load_props_file") {
    const auto path = write_tmp_json_file(R"({"title":"FromFile","duration":60})");
    const auto r = load_props_input(path, /*json*/ "");
    REQUIRE(r.ok);
    CHECK(r.props.values.get_string("title")    == "FromFile");
    CHECK(r.props.values.get_string("duration") == "60");
    // project_root populated from file parent_path.
    CHECK(r.props.project_root == std::filesystem::path(path).parent_path());
    std::remove(path.c_str());  // cleanup-after-test
}

// #8 — load_props_input: only json → delegates to load_props_inline.
TEST_CASE("props_input #8: only json set → delegates to load_props_inline") {
    const auto r = load_props_input(/*file*/ "",
                                    R"({"title":"FromInline"})");
    REQUIRE(r.ok);
    CHECK(r.props.values.get_string("title") == "FromInline");
    CHECK(r.props.project_root.empty());  // inline JSON has no parent dir
}

// #9 — load_props_input: both empty → ok=true + empty defaults.
TEST_CASE("props_input #9: both empty → ok=true with empty props (defaults)") {
    const auto r = load_props_input(/*file*/ "", /*json*/ "");
    REQUIRE(r.ok);
    CHECK(r.props.values.size() == 0);  // zero keys
    CHECK(r.keys.size() == 0);
}
