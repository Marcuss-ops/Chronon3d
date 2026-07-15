// test_schema_command.cpp — Phase 1d / Increment C
// ───────────────────────────────────────────────────────────────────────────
//
// Unit tests for `chronon3d_cli schema` subcommand. Verifies:
//   #1 — Known composition: emits JSON with `fields` array (exit 0).
//   #2 — Unknown composition: exits 1 + error JSON (`composition_not_found`).
//   #3 — Composition without schema: emits JSON with empty `fields` array.
//
// Pure-Logic tests; no rendering. The test target `chronon3d_introspection_tests`
// is gated by `CHRONON3D_BUILD_CLI_DEV` (the dev subcommand group is OFF by
// default).  AGENTS.md Cat-3: zero new public SDK API (the schema subcommand
// wraps the canonical `CompositionRegistry::descriptor_of` accessor).

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#  include <unistd.h>   // dup, dup2, close
#  define CHRONON3D_HAS_POSIX_DUP 1
#endif

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

/// Capture both the return value AND stdout for a single function call.
/// Mirrors the helper in `tests/cli/test_inspect_text.cpp` but returns
/// an `int + std::string` pair so TEST_CASEs can verify the exit code AND
/// the JSON content in the same pass.
struct CaptureResult {
    int exit_code{-1};
    std::string output;
};

CaptureResult capture_stdout_int(std::function<int()> fn) {
    CaptureResult r;
    static int s_counter = 0;
    const std::string tmp_path = "/tmp/chronon3d_schema_test_"
                               + std::to_string(++s_counter) + ".out";
    std::fflush(stdout);

#if defined(CHRONON3D_HAS_POSIX_DUP)
    const int saved_fd = ::dup(fileno(stdout));
    if (saved_fd < 0) return r;

    // RAII guard: flush + restore stdout + remove temp on scope exit.
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
    // Non-POSIX fallback: rely on `freopen`/re-read pattern from
    // test_inspect_text.cpp — NOT exercised on Linux CI but kept for
    // portability.
    r.exit_code = fn();
    std::fflush(stdout);
    std::freopen(tmp_path.c_str(), "w", stdout);
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

CompositionRegistry make_typed_registry_with_codec() {
    // Descriptor with an explicit PropsSchema via the PropsCodec path.
    // Pass an int Props via TypedCompositionDescriptor and a codec
    // declaring a single optional "title" field of type string.
    CompositionRegistry registry;

    // We add a typed descriptor that declares a flat PropsSchema; the
    // simplest reproduction is to attach a CompositionDescriptor with
    // a non-empty schema, which `descriptor_of()` exposes via the
    // canonical schema accessor.
    CompositionDescriptor desc;
    desc.id       = "TestSchemaOK";
    desc.category = "test";
    desc.width    = 1920;
    desc.height   = 1080;
    desc.fps      = FrameRate{30, 1};
    desc.duration = Frame{60};
    desc.schema   = PropsSchema{{
        PropField{"title", PropType::String, false,
                  "Headline text displayed on the composition"},
        PropField{"duration_frames", PropType::Integer, false,
                  "Number of frames the composition renders",
                  std::optional<std::string>{"60"}},
        PropField{"palette", PropType::Enum, false,
                  "Color palette preset", std::optional<std::string>{"default"},
                  std::vector<std::string>{"default", "warm", "cool"}},
    }};
    desc.factory = [](const CompositionProps&) {
        return composition({.name = "TestSchemaOK",
                            .width = 1920, .height = 1080,
                            .duration = Frame{60}},
                           [](const FrameContext&) {});
    };
    registry.add(std::move(desc));
    return registry;
}

CompositionRegistry make_legacy_registry_no_schema() {
    // Legacy CompositionDescriptor without schema (descriptors registered
    // before Phase 1).  `schema` field is std::nullopt; the JSON
    // output should have an empty `fields` array.
    CompositionRegistry registry;
    CompositionDescriptor desc;
    desc.id       = "TestLegacyNoSchema";
    desc.category = "legacy";
    desc.width    = 1280;
    desc.height   = 720;
    desc.fps      = FrameRate{24, 1};
    desc.duration = Frame{120};
    desc.factory = [](const CompositionProps&) {
        return composition({.name = "TestLegacyNoSchema",
                            .width = 1280, .height = 720,
                            .duration = Frame{120}},
                           [](const FrameContext&) {});
    };
    registry.add(std::move(desc));
    return registry;
}

} // anonymous namespace

// #1 — Known composition: emits JSON with `fields` array (exit 0).
TEST_CASE("schema #1: known composition emits JSON with fields array") {
    auto registry = make_typed_registry_with_codec();
    SchemaArgs args;
    args.comp_id = "TestSchemaOK";

    const auto r = capture_stdout_int([&]() {
        return command_schema(registry, args);
    });

    CHECK(r.exit_code == 0);
    REQUIRE_FALSE(r.output.empty());

    const auto js = nlohmann::json::parse(r.output);
    CHECK(js.contains("composition_id"));
    CHECK(js["composition_id"] == "TestSchemaOK");
    CHECK(js.contains("category"));
    CHECK(js.contains("fields"));
    REQUIRE(js["fields"].is_array());
    CHECK(js["fields"].size() == 3);

    // Verify each field has the canonical 5-key shape.
    for (const auto& field : js["fields"]) {
        CHECK(field.contains("name"));
        CHECK(field.contains("type"));
        CHECK(field.contains("required"));
    }

    // Spot-check the typed "title" entry.
    bool found_title = false;
    bool found_palette = false;
    for (const auto& field : js["fields"]) {
        if (field["name"] == "title") {
            found_title = true;
            CHECK(field["type"] == "string");
            CHECK(field["required"] == false);
            CHECK(field.contains("description"));
        }
        if (field["name"] == "palette") {
            found_palette = true;
            CHECK(field["type"] == "enum");
            REQUIRE(field.contains("enum_values"));
            CHECK(field["enum_values"].size() == 3);
        }
    }
    CHECK(found_title);
    CHECK(found_palette);
}

// #2 — Unknown composition: exit 1 + error JSON (`composition_not_found`).
TEST_CASE("schema #2: unknown composition exits 1 + error JSON") {
    CompositionRegistry registry;
    SchemaArgs args;
    args.comp_id = "DoesNotExist";

    const auto r = capture_stdout_int([&]() {
        return command_schema(registry, args);
    });

    CHECK(r.exit_code == 1);
    REQUIRE_FALSE(r.output.empty());

    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["error"] == "composition_not_found");
    CHECK(js["composition_id"] == "DoesNotExist");
    CHECK(js["status"] == "FAIL");
}

// #3 — Composition without schema: emits JSON with empty `fields` array.
TEST_CASE("schema #3: composition without schema emits empty fields array") {
    auto registry = make_legacy_registry_no_schema();
    SchemaArgs args;
    args.comp_id = "TestLegacyNoSchema";

    const auto r = capture_stdout_int([&]() {
        return command_schema(registry, args);
    });

    CHECK(r.exit_code == 0);
    REQUIRE_FALSE(r.output.empty());

    const auto js = nlohmann::json::parse(r.output);
    CHECK(js["composition_id"] == "TestLegacyNoSchema");
    REQUIRE(js["fields"].is_array());
    CHECK(js["fields"].size() == 0);
}
