// test_example_props_command.cpp — Phase 1d / Increment D
// ───────────────────────────────────────────────────────────────────────────
//
// Unit tests for `chronon3d_cli example-props` subcommand. Verifies:
//   #1 — Typed descriptor with codec schema + defaults: emits JSON with
//        canonical default ValueMap (exit 0).
//   #2 — Legacy descriptor without schema: exits 1 + error JSON
//        (`no_canonical_example`).
//   #3 — Unknown composition: exits 1 + error JSON
//        (`composition_not_found`).
//
// Pure-Logic tests; no rendering. The test target `chronon3d_introspection_tests`
// is gated by `CHRONON3D_BUILD_CLI_DEV`. AGENTS.md Cat-3: zero new public
// SDK API (the subcommand wraps the canonical `CompositionRegistry::descriptor_of`
// accessor + walks `descriptor.schema.fields` for canonical defaults).

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#  include <unistd.h>   // dup, dup2, close
#  define CHRONON3D_HAS_POSIX_DUP 1
#endif

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

struct CaptureResult { int exit_code{-1}; std::string output; };

CaptureResult capture_stdout_int(std::function<int()> fn) {
    CaptureResult r;
    static int s_counter = 0;
    const std::string tmp_path = "/tmp/chronon3d_example_props_test_"
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

CompositionRegistry make_typed_registry_with_codec_defaults() {
    CompositionRegistry registry;
    CompositionDescriptor desc;
    desc.id       = "TestExamplePropsOK";
    desc.category = "test";
    desc.width    = 1920;
    desc.height   = 1080;
    desc.fps      = FrameRate{30, 1};
    desc.duration = Frame{60};
    desc.schema   = PropsSchema{{
        PropField{"title",    PropType::String,  false,
                  "Headline text",      std::optional<std::string>{"Breaking News"}},
        PropField{"duration", PropType::Integer, false,
                  "Number of frames",   std::optional<std::string>{"60"}},
        PropField{"palette",  PropType::Enum,    false,
                  "Color palette",      std::optional<std::string>{"default"},
                  std::vector<std::string>{"default", "warm", "cool"}},
        PropField{"author",   PropType::String,  true,
                  "Author name",        std::nullopt},
    }};
    desc.factory = [](const CompositionProps&) {
        return composition({.name = "TestExamplePropsOK",
                            .width = 1920, .height = 1080,
                            .duration = Frame{60}},
                           [](const FrameContext&) {});
    };
    registry.add(std::move(desc));
    return registry;
}

CompositionRegistry make_legacy_registry_no_codec() {
    CompositionRegistry registry;
    CompositionDescriptor desc;
    desc.id       = "TestLegacyNoCodec";
    desc.category = "legacy";
    desc.width    = 1280;
    desc.height   = 720;
    desc.fps      = FrameRate{24, 1};
    desc.duration = Frame{120};
    // NO desc.schema; canonical example is not introspectable.
    desc.factory = [](const CompositionProps&) {
        return composition({.name = "TestLegacyNoCodec",
                            .width = 1280, .height = 720,
                            .duration = Frame{120}},
                           [](const FrameContext&) {});
    };
    registry.add(std::move(desc));
    return registry;
}

} // anonymous namespace

// #1 — Typed descriptor with codec defaults: emits canonical JSON.
TEST_CASE("example-props #1: typed descriptor emits canonical default JSON") {
    auto registry = make_typed_registry_with_codec_defaults();
    ExamplePropsArgs args;
    args.comp_id = "TestExamplePropsOK";

    const auto r = capture_stdout_int([&]() {
        return command_example_props(registry, args);
    });

    CHECK(r.exit_code == 0);
    REQUIRE_FALSE(r.output.empty());

    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["composition_id"] == "TestExamplePropsOK");
    REQUIRE(js.contains("props"));
    REQUIRE(js["props"].is_object());

    const auto& p = js["props"];
    CHECK(p["title"]    == "Breaking News");
    CHECK(p["duration"] == "60");
    CHECK(p["palette"]  == "default");
    // Required field without a default emits empty string marker.
    CHECK(p["author"]   == "");
    // Confirm no surprise keys leaked from the descriptor schema.
    CHECK(p.size() == 4);
}

// #2 — Legacy descriptor without codec: exit 1 + error JSON.
TEST_CASE("example-props #2: legacy descriptor without codec exits 1") {
    auto registry = make_legacy_registry_no_codec();
    ExamplePropsArgs args;
    args.comp_id = "TestLegacyNoCodec";

    const auto r = capture_stdout_int([&]() {
        return command_example_props(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());

    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "no_canonical_example");
    CHECK(js["composition_id"] == "TestLegacyNoCodec");
    CHECK(js["status"] == "FAIL");
    CHECK(js.contains("reason"));
}

// #3 — Unknown composition: exit 1 + error JSON.
TEST_CASE("example-props #3: unknown composition exits 1") {
    CompositionRegistry registry;
    ExamplePropsArgs args;
    args.comp_id = "DoesNotExist";

    const auto r = capture_stdout_int([&]() {
        return command_example_props(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());

    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "composition_not_found");
    CHECK(js["composition_id"] == "DoesNotExist");
    CHECK(js["status"] == "FAIL");
}
