// test_validate_command.cpp — Phase 1d / Increment E
// ───────────────────────────────────────────────────────────────────────────
//
// Unit tests for `chronon3d_cli validate` subcommand. Verifies:
//   #1 — PASS: typed descriptor with `PropsCodec::decode` + `validate`:
//        emits `valid=true` (exit 0).
//   #2 — FAIL: PropsCodec returns PropsError on bad input:
//        emits `valid=false`, `error=PropsError`, `reason=InvalidFormat`
//        (exit 1).
//   #3 — FAIL: unknown composition → `valid=false`,
//        `error=PropsError`, `key=""` (exit 1).
//   #4 — FAIL: invalid `--props-json` inline string:
//        emits `error=invalid_props_input` (exit 1).
//
// Pure-Logic tests; no rendering. Tests `chronon3d_introspection_tests`
// gated by `CHRONON3D_BUILD_CLI_DEV`. AGENTS.md Cat-3: zero new public
// SDK API.

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands.hpp>
#include <apps/chronon3d_cli/utils/common/props_file.hpp>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <unistd.h>
#  define CHRONON3D_HAS_POSIX_DUP 1
#endif

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

struct NewsProps {
    std::string title{"default-title"};
    int         duration_frames{150};
};

struct CaptureResult { int exit_code{-1}; std::string output; };

CaptureResult capture_stdout_int(std::function<int()> fn) {
    CaptureResult r;
    static int s_counter = 0;
    const std::string tmp_path = "/tmp/chronon3d_validate_test_"
                               + std::to_string(++s_counter) + ".out";
    std::fflush(stdout);

#if defined(CHRONON3D_HAS_POSIX_DUP)
    const int saved_fd = ::dup(fileno(stdout));
    if (saved_fd < 0) return r;
    struct RedirectGuard {
        int saved_fd;
        std::string tmp_path;
        ~RedirectGuard() {
            std::fflush(stdout);
            ::dup2(saved_fd, fileno(stdout));
            ::close(saved_fd);
            std::remove(tmp_path.c_str());
        }
    } guard{saved_fd, tmp_path};

    if (!std::freopen(tmp_path.c_str(), "w", stdout)) {
        return r;
    }
    r.exit_code = fn();
    std::fflush(stdout);
#else
    r.exit_code = fn();
    std::fflush(stdout);
    std::freopen("/dev/tty", "w", stdout);
#endif

    FILE* f = std::fopen(tmp_path.c_str(), "r");
    if (!f) return r;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), f) != nullptr) {
        r.output += buf;
    }
    std::fclose(f);
    return r;
}

CompositionRegistry make_typed_validate_registry() {
    CompositionRegistry registry;
    registry.add(TypedCompositionDescriptor<NewsProps>{
        .id = "ValidatePass",
        .category = "test",
        .defaults = NewsProps{"Breaking News", 150},
        .validate = [](const NewsProps& p) -> std::optional<std::string> {
            if (p.duration_frames < 0) return std::string{"duration must be >= 0"};
            return std::nullopt;
        },
        .decode = [](const ValueMap& vals, const NewsProps& defs)
                      -> Result<NewsProps, PropsError> {
            NewsProps p = defs;
            if (vals.contains("title"))    p.title = vals.get_string("title");
            if (vals.contains("duration")) p.duration_frames = vals.get_int("duration", 0);
            return p;
        },
        .factory = [](const NewsProps&) {
            return composition({.name = "ValidatePass",
                                .width = 1920, .height = 1080,
                                .duration = Frame{150}},
                               [](const FrameContext&) {});
        }
    }.to_descriptor());
    return registry;
}

} // anonymous namespace

// #1 — PASS: typed descriptor with valid props → `valid=true`, exit 0.
TEST_CASE("validate #1: valid props emit valid=true (exit 0)") {
    auto registry = make_typed_validate_registry();
    ValidateArgs args;
    args.comp_id    = "ValidatePass";
    args.props_json = R"({"title":"Custom Title","duration":"300"})";

    const auto r = capture_stdout_int([&]() {
        return command_validate(registry, args);
    });

    CHECK(r.exit_code == 0);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["valid"] == true);
    CHECK(js["composition_id"] == "ValidatePass");
    CHECK(js["status"] == "PASS");
    CHECK(js["props_keys_count"] == 2);
}

// #2 — FAIL: PropsCodec validate gate rejects negative duration.
TEST_CASE("validate #2: invalid props emit valid=false (exit 1)") {
    auto registry = make_typed_validate_registry();
    ValidateArgs args;
    args.comp_id    = "ValidatePass";
    args.props_json = R"({"duration":"-1"})";

    const auto r = capture_stdout_int([&]() {
        return command_validate(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["valid"] == false);
    CHECK(js["error"] == "PropsError");
    CHECK(js["reason"] == "InvalidFormat");
    CHECK(js["status"] == "FAIL");
}

// #3 — FAIL: unknown composition → `valid=false` (exit 1).
TEST_CASE("validate #3: unknown composition emits valid=false (exit 1)") {
    CompositionRegistry registry;
    ValidateArgs args;
    args.comp_id = "DoesNotExist";

    const auto r = capture_stdout_int([&]() {
        return command_validate(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["valid"] == false);
    CHECK(js["error"] == "PropsError");
    CHECK(js["status"] == "FAIL");
}

// #4 — FAIL: malformed inline `--props-json` → `invalid_props_input`.
TEST_CASE("validate #4: malformed --props-json exits 1") {
    auto registry = make_typed_validate_registry();
    ValidateArgs args;
    args.comp_id    = "ValidatePass";
    args.props_json = "{ this is not valid JSON";

    const auto r = capture_stdout_int([&]() {
        return command_validate(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "invalid_props_input");
    CHECK(js["composition_id"] == "ValidatePass");
    CHECK(js["status"] == "FAIL");
}

// #5 — FAIL: mutual-exclusion of --props-file and --props-json.
TEST_CASE("validate #5: --props-file + --props-json mutually exclusive (exit 1)") {
    auto registry = make_typed_validate_registry();
    ValidateArgs args;
    args.comp_id    = "ValidatePass";
    args.props_file = "/tmp/some_path.json";
    args.props_json = R"({"title":"ignored"})";

    const auto r = capture_stdout_int([&]() {
        return command_validate(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "invalid_props_input");
    CHECK(js["composition_id"] == "ValidatePass");
    CHECK(js["status"] == "FAIL");
    REQUIRE(js.contains("message"));
    const auto& msg = js["message"].get<std::string>();
    CHECK(msg.find("mutually exclusive") != std::string::npos);
}
