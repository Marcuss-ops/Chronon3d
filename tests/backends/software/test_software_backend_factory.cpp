// ═══════════════════════════════════════════════════════════════════════════
// test_software_backend_factory.cpp — TICKET-070 regression coverage
// (TICKET-118/119: contractive-removal of `owner` field)
// ═══════════════════════════════════════════════════════════════════════════
//
// Fase 1#10 close-out: verifies the validation-gate factory
// `make_software_backend(SoftwareBackendServices)` rejects malformed
// services bundles in BOTH the debug-build loud-fail (`assert(...)`) and
// the release-build structured release (`Result::err(SoftwareBackend...
// ServicesError)`).
//
// TICKET-118 closure — `SoftwareBackendServices::owner` and the
// `SoftwareBackendServicesError::Code::MissingOwner` enum value have
// been REMOVED.  Per thinker-with-files-gemini validation, this file
// applies Option A (DELETE-only): the helper `services_from_renderer`
// no longer sets the `owner` field (it does not exist), the static-grep
// layer no longer greps for `MissingOwner` / `assert(services.owner`,
// and the NDEBUG `null owner` test case has been deleted.  Net test
// count dropped from 6 to 4 happy-path + 9 missing-service cases
// (1 + 1 static-grep-asserts + 1 static-grep-codes + 4 Missing* NDEBUG).
//
// Three layers of coverage so the regression stays deterministic regardless
// of build mode:
//
//   1. Static-grep (BOTH modes): reads `src/backends/software/software_
//      backend.cpp` and asserts each canonical assert() guard AND each
//      MissingXxx release branch is present.  This is the defence-in-depth
//      layer — the source-level regression lock that survives even when
//      debug builds abort before reaching Result::err at runtime.
//
//   2. Happy path (BOTH modes): submits a fully-wired SoftwareBackend
//      Services from a live SoftwareRenderer and asserts the factory
//      returns a unique_ptr<SoftwareBackend>.  Ensures the production
//      route (renderer.runtime() → make_software_backend(...).value())
//      does not regress.
//
//   3. Result::err exercise (NDEBUG-only): in release builds (`-DNDEBUG`)
//      the asserts are stripped, so we can exercise each REQUIRED-field
//      null path and verify the structured Code + field_name exactly.
//      In debug builds (`#ifndef NDEBUG`) the asserts abort the test
//      runner before reaching Result::err, so this layer is gated.
//
// Pattern precedent: tests/text/test_font_io_fence.cpp (Cat-2 regression
// for synchronous font I/O re-introduction).  Same static-grep discipline,
// adapted to the factory contract.
//
// Framework: doctest, matching tests/text/* + tests/backends/software/*.
// No new public API surface; no new include or class.  Pure Cat-2
// regression gate per AGENTS.md freeze policy.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_backend_services.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace chronon3d;

namespace {

// ── helpers ─────────────────────────────────────────────────────────────

// Build a fully-wired SoftwareBackendServices from a live SoftwareRenderer.
// Mirrors `tests/helpers/test_utils.hpp::attach_software_backend`
// but does NOT route through `make_software_backend(...).value()` — we're
// testing that factory in the surrounding TEST_CASE, so we want a
// hand-built bundle the test can mutate (null a field) before the call.
//
// TICKET-118 — the `/* owner = */ &r` field has been REMOVED.  The
// `SoftwareRenderer*` back-pointer no longer lives on the services
// bundle; orchestrator-only fields (registry, image_backend, font_engine)
// are wired via `SoftwareBackend::attach_processor_context(...)` after
// construction (see `internal/software_processor_services.hpp`).
SoftwareBackendServices services_from_renderer(SoftwareRenderer& r) noexcept {
    return SoftwareBackendServices{
        /* owner REMOVED IN TICKET-118 — was: &r, */
        /* counters           = */ r.counters(),
        /* settings           = */ &r.render_settings(),
        /* framebuffer_pool   = */ r.runtime().framebuffer_pool_shared(),
        /* asset_resolver     = */ &r.runtime().resolver(),
        /* text_resources     = */ r.text_render_resources(),
        /* images             = */ nullptr,
        /* text_raster        = */ nullptr,
        /* debug_config       = */ nullptr,
    };
}

// Project-relative path.  `add_test` in `tests/backends_software_tests.cmake`
// sets `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` so this resolves relative
// to the project root correctly.
constexpr const char* kBackendCppPath = "src/backends/software/software_backend.cpp";

bool source_contains(std::string_view needle) {
    std::ifstream f(kBackendCppPath);
    if (!f.is_open()) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string haystack = ss.str();
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════
// 1. Happy-path — valid bundle yields `unique_ptr<SoftwareBackend>`
//    (works in BOTH debug and release; asserts pass on valid fields)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("make_software_backend: accepts a valid services bundle and returns unique_ptr") {
    SoftwareRenderer renderer(Config{});
    auto services = services_from_renderer(renderer);

    auto result = make_software_backend(std::move(services));
    REQUIRE(result.has_value());
    REQUIRE(result.value() != nullptr);
}

// ═════════════════════════════════════════════════════════════════════════
// 2a. Static-grep: every REQUIRED field has an `assert(...)` guard
//     running under `#ifndef NDEBUG`.  Pattern precedent:
//     `tests/text/test_font_io_fence.cpp`.
//
//     TICKET-118 — `assert(services.owner` CHECK dropped (the field
//     has been removed from SoftwareBackendServices).
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("make_software_backend: source contains canonical assert() guards for each REQUIRED service (static-grep)") {
    REQUIRE(std::filesystem::exists(kBackendCppPath));
    CHECK(source_contains("assert(services.counters"));
    CHECK(source_contains("assert(services.settings"));
    CHECK(source_contains("assert(services.asset_resolver"));
    CHECK(source_contains("assert(services.text_resources"));
    CHECK(source_contains("assert(services.framebuffer_pool"));
}

// ═════════════════════════════════════════════════════════════════════════
// 2a-bis. Contractive-removal static-grep: the TICKET-118 changes are
// visibly applied.  Proves the contractive removal didn't regress to a
// zombie half-applied state.
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("make_software_backend: TICKET-118 contractive-removal of owner is visibly applied (static-grep)") {
    REQUIRE(std::filesystem::exists(kBackendCppPath));
    // mandatory: services struct no longer carries the `owner` field.
    CHECK_FALSE(source_contains("services.owner"));
    // mandatory: assert/services.owner guard has been removed.
    CHECK_FALSE(source_contains("assert(services.owner"));
    // mandatory: MissingOwner Code has been removed (renumbered to
    // MissingCounters=1, ..., MissingTextResources=5).
    CHECK_FALSE(source_contains("MissingOwner"));
    // narrative marker — the source comment line announcing the
    // contractive removal must be present so a future maintainer can
    // grep for the TICKET header quickly.
    CHECK(source_contains("TICKET-118"));
}

// ═════════════════════════════════════════════════════════════════════════
// 2b. Static-grep: every REQUIRED field has a `Result::err` release path
//     via `SoftwareBackendServicesError::Code::MissingX{x}`.
//
//     TICKET-118 — `MissingOwner` CHECK dropped (the Code value has
//     been removed; downstream regression-grep `MissingOwner` would
//     be a false-positive in the layered coverage model).
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("make_software_backend: source contains Result::err release branch for each REQUIRED service (static-grep)") {
    REQUIRE(std::filesystem::exists(kBackendCppPath));
    CHECK(source_contains("MissingCounters"));
    CHECK(source_contains("MissingSettings"));
    CHECK(source_contains("MissingFramebufferPool"));
    CHECK(source_contains("MissingAssetResolver"));
    CHECK(source_contains("MissingTextResources"));
}

#if defined(NDEBUG)
// ═════════════════════════════════════════════════════════════════════════
// 3. Result::err exercise — release-only.  Debug builds assert() before
//    Result::err can run, aborting the test runner.
//
//    These TEST_CASEs verify the production release path explicitly:
//    each REQUIRED service is nulled in turn, and we assert
//      (a) result.has_value() == false,
//      (b) result.error().code matches the expected MissingXxx,
//      (c) result.error().field_name matches the source field name.
//
//    TICKET-118 — the `null owner` test case has been deleted (the
//    `owner` field no longer exists).  Downstream coverage is the
//    `2a-bis` static-grep above.
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("make_software_backend: null counters → Result::err(MissingCounters)") {
    SoftwareRenderer renderer(Config{});
    auto services = services_from_renderer(renderer);
    services.counters = nullptr;
    auto r = make_software_backend(std::move(services));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code   == SoftwareBackendServicesError::Code::MissingCounters);
    CHECK(r.error().field_name == "counters");
}

TEST_CASE("make_software_backend: null settings → Result::err(MissingSettings)") {
    SoftwareRenderer renderer(Config{});
    auto services = services_from_renderer(renderer);
    services.settings = nullptr;
    auto r = make_software_backend(std::move(services));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code   == SoftwareBackendServicesError::Code::MissingSettings);
    CHECK(r.error().field_name == "settings");
}

TEST_CASE("make_software_backend: empty framebuffer_pool → Result::err(MissingFramebufferPool)") {
    SoftwareRenderer renderer(Config{});
    auto services = services_from_renderer(renderer);
    services.framebuffer_pool.reset();
    auto r = make_software_backend(std::move(services));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code   == SoftwareBackendServicesError::Code::MissingFramebufferPool);
    CHECK(r.error().field_name == "framebuffer_pool");
}

TEST_CASE("make_software_backend: null asset_resolver → Result::err(MissingAssetResolver)") {
    SoftwareRenderer renderer(Config{});
    auto services = services_from_renderer(renderer);
    services.asset_resolver = nullptr;
    auto r = make_software_backend(std::move(services));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code   == SoftwareBackendServicesError::Code::MissingAssetResolver);
    CHECK(r.error().field_name == "asset_resolver");
}

TEST_CASE("make_software_backend: null text_resources → Result::err(MissingTextResources)") {
    SoftwareRenderer renderer(Config{});
    auto services = services_from_renderer(renderer);
    services.text_resources = nullptr;
    auto r = make_software_backend(std::move(services));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code   == SoftwareBackendServicesError::Code::MissingTextResources);
    CHECK(r.error().field_name == "text_resources");
}
#endif // defined(NDEBUG)
