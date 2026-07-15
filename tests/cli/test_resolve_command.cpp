// test_resolve_command.cpp — Phase 1d / Increment F
// ───────────────────────────────────────────────────────────────────────────
//
// Unit tests for `chronon3d_cli resolve` subcommand. Verifies:
//   #1 — PASS: typed descriptor with valid props + metadata → emits
//        {resolved: true, props: {...}, metadata: {...}, status: "PASS"}
//   #2 — FAIL: PropsCodec validate gate rejects → emits
//        {resolved: false, error: "PropsError", ...}
//   #3 — FAIL: unknown composition → emits error JSON.
//   #4 — FAIL: malformed inline `--props-json` → error JSON.
//   #5 — FAIL: --props-file + --props-json mutually exclusive.
//   #6 — PASS: empty input → canonical defaults via prepare_props closure.
//
// Pure-Logic tests; no rendering. The test target `chronon3d_introspection_tests`
// is gated by `CHRONON3D_BUILD_CLI_DEV`. AGENTS.md Cat-3: zero new public
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
    const std::string tmp_path = "/tmp/chronon3d_resolve_test_"
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

CompositionRegistry make_typed_resolve_registry() {
    CompositionRegistry registry;
    // Attach codec (schema + decode) so `descriptor.schema` is populated —
    // props_to_json() walks descriptor.schema.fields to emit the resolved
    // props JSON. Without codec, descriptor.schema is std::nullopt and
    // props emit as empty object (cat-3 cat-5 `RegressionLock #1`: test #1
    // expects title+duration present).
    PropsCodec<NewsProps> codec;
    codec.schema = PropsSchema{{
        PropField{"title",    PropType::String,  false,
                  "Headline text",     std::optional<std::string>{"Breaking News"}},
        PropField{"duration", PropType::Integer, false,
                  "Frame count",       std::optional<std::string>{"150"}},
    }};
    codec.decode = [](const ValueMap& vals, const NewsProps& defs)
                      -> Result<NewsProps, PropsError> {
        NewsProps p = defs;
        if (vals.contains("title"))    p.title = vals.get_string("title");
        if (vals.contains("duration")) p.duration_frames = vals.get_int("duration", 0);
        return p;
    };
    registry.add(TypedCompositionDescriptor<NewsProps>{
        .id = "ResolvePass",
        .category = "test",
        .defaults = NewsProps{"Breaking News", 150},
        .validate = [](const NewsProps& p) -> std::optional<std::string> {
            if (p.duration_frames < 0) return std::string{"duration must be >= 0"};
            return std::nullopt;
        },
        .resolve_metadata = [](const NewsProps& p) {
            return CompositionMetadata{1920, 1080, FrameRate{30, 1}, Frame{p.duration_frames}};
        },
        .factory = [](const NewsProps&) {
            return composition({.name = "ResolvePass",
                                .width = 1920, .height = 1080,
                                .duration = Frame{150}},
                               [](const FrameContext&) {});
        },
        .codec = std::move(codec),
    }.to_descriptor());
    return registry;
}

} // anonymous namespace

// #1 — PASS: emit resolved=JSON with all expected fields.
TEST_CASE("resolve #1: valid props emit resolved=true with props+metadata (exit 0)") {
    auto registry = make_typed_resolve_registry();
    ResolveArgs args;
    args.comp_id    = "ResolvePass";
    args.props_json = R"({"title":"Custom Title","duration":"300"})";

    const auto r = capture_stdout_int([&]() {
        return command_resolve(registry, args);
    });

    CHECK(r.exit_code == 0);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["resolved"] == true);
    CHECK(js["composition_id"] == "ResolvePass");
    CHECK(js["status"] == "PASS");

    // Props emission: user-provided values over defaults.
    REQUIRE(js.contains("props"));
    REQUIRE(js["props"].is_object());
    CHECK(js["props"]["title"]    == "Custom Title");
    CHECK(js["props"]["duration"] == "300");

    // Metadata emission: canonical resolution from prepare_props closure.
    REQUIRE(js.contains("metadata"));
    CHECK(js["metadata"]["width"]  == 1920);
    CHECK(js["metadata"]["height"] == 1080);
    REQUIRE(js["metadata"]["fps"].is_object());
    CHECK(js["metadata"]["fps"]["numerator"]   == 30);
    CHECK(js["metadata"]["fps"]["denominator"] == 1);
    CHECK(js["metadata"]["duration"] == 300);
}

// #2 — FAIL: PropsCodec validate gate rejects.
TEST_CASE("resolve #2: invalid props emit resolved=false (exit 1)") {
    auto registry = make_typed_resolve_registry();
    ResolveArgs args;
    args.comp_id    = "ResolvePass";
    args.props_json = R"({"duration":"-1"})";

    const auto r = capture_stdout_int([&]() {
        return command_resolve(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["resolved"] == false);
    CHECK(js["error"] == "PropsError");
    CHECK(js["reason"] == "InvalidFormat");
    CHECK(js["status"] == "FAIL");
}

// #3 — FAIL: unknown composition.
TEST_CASE("resolve #3: unknown composition emits resolved=false (exit 1)") {
    CompositionRegistry registry;
    ResolveArgs args;
    args.comp_id = "DoesNotExist";

    const auto r = capture_stdout_int([&]() {
        return command_resolve(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["resolved"] == false);
    CHECK(js["error"] == "PropsError");
    CHECK(js["status"] == "FAIL");
}

// #4 — FAIL: malformed inline JSON.
TEST_CASE("resolve #4: malformed --props-json exits 1") {
    auto registry = make_typed_resolve_registry();
    ResolveArgs args;
    args.comp_id    = "ResolvePass";
    args.props_json = "{ not valid }";

    const auto r = capture_stdout_int([&]() {
        return command_resolve(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "invalid_props_input");
    CHECK(js["status"] == "FAIL");
}

// #5 — FAIL: --props-file + --props-json mutually exclusive.
TEST_CASE("resolve #5: --props-file + --props-json mutually exclusive (exit 1)") {
    auto registry = make_typed_resolve_registry();
    ResolveArgs args;
    args.comp_id    = "ResolvePass";
    args.props_file = "/tmp/some_path.json";
    args.props_json = R"({"title":"ignored"})";

    const auto r = capture_stdout_int([&]() {
        return command_resolve(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "invalid_props_input");
    CHECK(js["status"] == "FAIL");
    REQUIRE(js.contains("message"));
    CHECK(js["message"].get<std::string>().find("mutually exclusive") != std::string::npos);
}

// #6 — PASS: empty input (no flags) → canonical defaults via prepare_props.
TEST_CASE("resolve #6: empty input emits canonical defaults (exit 0)") {
    auto registry = make_typed_resolve_registry();
    ResolveArgs args;
    args.comp_id = "ResolvePass";
    // No --props-file, no --props-json: empty input → defaults.

    const auto r = capture_stdout_int([&]() {
        return command_resolve(registry, args);
    });

    CHECK(r.exit_code == 0);
    REQUIRE_FALSE(r.output.empty());
    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["resolved"] == true);
    CHECK(js["composition_id"] == "ResolvePass");
    CHECK(js["status"] == "PASS");
    // The schema is std::nullopt for this registry's descriptor (no codec
    // schema explicitly attached), so props emit as empty object.
    REQUIRE(js.contains("props"));
    CHECK(js["props"].is_object());
    // Metadata is resolved via resolve_metadata closure with defaults
    // (NewsProps{150}) → duration=150.
    REQUIRE(js.contains("metadata"));
    CHECK(js["metadata"]["duration"] == 150);
}
