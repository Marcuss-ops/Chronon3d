// ==============================================================================
// tests/assets/test_asset_resolver.cpp
//
// WP-8 PR 8.0 unit tests for chronon3d::assets::AssetResolver.
// Exercises the four behaviour categories listed at the class
// doc-comment (absolute / relative-mounted / relative-unmounted /
// clamp-escape / on-disk-missing).  Uses a persistent temp directory
// shared across all TEST_CASEs in this TU so per-test setup is
// minimal.
// ==============================================================================

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

// Process-wide scratch directory used by every TEST_CASE below.  Created
// on construction, removed on destruction.  Allocated under
// std::filesystem::temp_directory_path() for portability across CI hosts.
class ProcessTempDir {
public:
    ProcessTempDir() {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp = std::chrono::system_clock::now()
                               .time_since_epoch().count();
        path = base / ("chronon3d_test_asset_resolver_" +
                       std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~ProcessTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::filesystem::path path;
};

ProcessTempDir g_temp;

// Creates `rel` (relative to g_temp.path) with a single byte if not
// already present.  Returns true on first creation, false on reuse.
bool ensure_file(const std::filesystem::path& rel) {
    const auto full = g_temp.path / rel;
    if (std::filesystem::exists(full)) return false;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream f(full);
    f << "x";
    return true;
}

void remove_if_present(const std::filesystem::path& abs) {
    std::error_code ec;
    std::filesystem::remove(abs, ec);
}

} // namespace

TEST_CASE("AssetResolver::mount sets has_mount and records root") {
    chronon3d::assets::AssetResolver r;
    CHECK_FALSE(r.has_mount());
    r.mount(g_temp.path);
    CHECK(r.has_mount());
    CHECK(r.mount_root() ==
          std::filesystem::path(g_temp.path).lexically_normal());
}

TEST_CASE("AssetResolver::mount(empty) keeps has_mount == false") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    CHECK(r.has_mount());
    r.mount({});
    CHECK_FALSE(r.has_mount());
}

TEST_CASE("AssetResolver::unmount clears state") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    CHECK(r.has_mount());
    r.unmount();
    CHECK_FALSE(r.has_mount());
    CHECK(r.mount_root() == std::filesystem::path{});
}

TEST_CASE("AssetResolver::resolve absolute bypasses mount when path exists") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    const auto abs = std::filesystem::current_path() /
                     "marker_abs_present_zzz.txt";
    {
        std::ofstream f(abs);
        f << "x";
    }
    const auto res = r.resolve(abs);
    REQUIRE(res.has_value());
    CHECK(*res == abs.lexically_normal());
    remove_if_present(abs);
}

TEST_CASE("AssetResolver::resolve absolute missing returns nullopt") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    const auto abs =
        std::filesystem::current_path() /
        "marker_abs_definitely_missing_zzz.txt";
    CHECK_FALSE(r.resolve(abs).has_value());
}

TEST_CASE("AssetResolver::resolve under mount (file exists)") {
    ensure_file("marker_mounted.txt");
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    const auto res = r.resolve(std::filesystem::path("marker_mounted.txt"));
    REQUIRE(res.has_value());
    CHECK(*res == (g_temp.path / "marker_mounted.txt").lexically_normal());
}

TEST_CASE("AssetResolver::resolve under mount (nested relative)") {
    ensure_file("nested/deep/marker_nested.txt");
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    const auto res = r.resolve(
        std::filesystem::path("nested/deep/marker_nested.txt"));
    REQUIRE(res.has_value());
    CHECK(*res == (g_temp.path / "nested/deep/marker_nested.txt")
                      .lexically_normal());
}

TEST_CASE("AssetResolver::resolve under mount (file missing) returns nullopt") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    CHECK_FALSE(
        r.resolve(std::filesystem::path("definitely_missing_xyz_marker.txt"))
            .has_value());
}

TEST_CASE("AssetResolver::resolve without mount (relative) returns nullopt") {
    chronon3d::assets::AssetResolver r;
    // no mount configured
    CHECK_FALSE(r.resolve(std::filesystem::path("anything")).has_value());
}

TEST_CASE("AssetResolver::resolve ../escape returns nullopt") {
    ensure_file("marker_escape_above.txt");
    chronon3d::assets::AssetResolver r;
    // Mount INSIDE `nested/` — "../marker_escape_above.txt" points OUT
    // of the mount and must be rejected.
    r.mount(g_temp.path / "nested");
    CHECK_FALSE(
        r.resolve(std::filesystem::path("../marker_escape_above.txt"))
            .has_value());
}

TEST_CASE("AssetResolver::resolve within nested mount, deep path works") {
    ensure_file("nested/deep/marker_clamped_ok.txt");
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path / "nested");
    const auto res = r.resolve(
        std::filesystem::path("deep/marker_clamped_ok.txt"));
    REQUIRE(res.has_value());
    CHECK(*res == (g_temp.path / "nested" / "deep" / "marker_clamped_ok.txt")
                      .lexically_normal());
}

TEST_CASE("AssetResolver::resolve empty path returns nullopt") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    CHECK(r.has_mount());                  // sanity: mount in effect
    CHECK_FALSE(r.resolve(std::filesystem::path{}).has_value());
}

TEST_CASE("AssetResolver::mount(non-absolute) throws invalid_argument") {
    chronon3d::assets::AssetResolver r;
    CHECK_THROWS_AS(r.mount(std::filesystem::path("relative/path")),
                    std::invalid_argument);
    CHECK_FALSE(r.has_mount());              // state unchanged after throw
    // A subsequent absolute mount must succeed normally.
    r.mount(g_temp.path);
    CHECK(r.has_mount());
}

TEST_CASE("AssetResolver::resolve_lexical skips on-disk check (exists case)") {
    ensure_file("marker_lex_exists.txt");
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    const auto res = r.resolve_lexical(
        std::filesystem::path("marker_lex_exists.txt"));
    REQUIRE(res.has_value());
    CHECK(*res == (g_temp.path / "marker_lex_exists.txt").lexically_normal());
}

TEST_CASE("AssetResolver::resolve_lexical accepts non-existent files") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);
    const auto res = r.resolve_lexical(
        std::filesystem::path("does_not_exist_but_lexically_valid.txt"));
    REQUIRE(res.has_value());
    CHECK(*res == (g_temp.path / "does_not_exist_but_lexically_valid.txt")
                      .lexically_normal());
}

TEST_CASE("AssetResolver::resolve_lexical still rejects ../escape") {
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path / "nested");
    CHECK_FALSE(
        r.resolve_lexical(std::filesystem::path("../escape_attempt.txt"))
            .has_value());
}

TEST_CASE("AssetResolver two engines with different mounts resolve independently") {
    ensure_file("a_dir/marker_a.txt");
    ensure_file("b_dir/marker_b.txt");

    const auto dir_a = g_temp.path / "a_dir";
    const auto dir_b = g_temp.path / "b_dir";

    chronon3d::assets::AssetResolver a;
    a.mount(dir_a);
    chronon3d::assets::AssetResolver b;
    b.mount(dir_b);

    // Same relative key ("marker_X.txt") maps to two distinct absolute
    // paths because each engine owns its own mount.
    const auto a_marker =
        a.resolve(std::filesystem::path("marker_a.txt"));
    const auto b_marker =
        b.resolve(std::filesystem::path("marker_b.txt"));
    REQUIRE(a_marker.has_value());
    REQUIRE(b_marker.has_value());
    CHECK(*a_marker == (dir_a / "marker_a.txt").lexically_normal());
    CHECK(*b_marker == (dir_b / "marker_b.txt").lexically_normal());
    CHECK(*a_marker != *b_marker);
}

// ==============================================================================
// WP-8 PR 8.1 — tests for runtime::typed_resolver_for_deep_code()
//
// These tests rely on the common render-runtime fixtures: any default-
// constructed RenderRuntime publishes itself as `active_runtime()` and
// owns a typed resolver.  We deliberately do NOT spin up a runtime in
// this test TU (it would link render_runtime + its whole ecosystem);
// instead the active-runtime side is documented by the test name and
// the helper's contract.  We exercise the process-wide fallback path
// (the deep-code path) by calling set_process_wide_assets_root with
// the temp dir, then asserting that resolve_lexical against
// a relative path joined to that root resolves correctly.
//
// Test isolation: every helper test calls
// `detail::reset_typed_resolver_for_deep_code_for_testing()` at
// entry so a previous test's first-mount is wiped.  Without this,
// the static resolution would persist across tests and the
// "unmounted" / "non-absolute skipped" assertions would silently
// observe stale mount state.
// ==============================================================================

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

TEST_CASE("typed_resolver_for_deep_code: process-wide fallback resolves relative path") {
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    ensure_file("deep_helper_marker.txt");
    const auto saved = chronon3d::runtime::process_wide_assets_root();
    chronon3d::runtime::set_process_wide_assets_root(g_temp.path.string());

    const auto& resolver =
        chronon3d::runtime::typed_resolver_for_deep_code();
    const auto opt = resolver.resolve_lexical(
        std::filesystem::path("deep_helper_marker.txt"));
    REQUIRE(opt.has_value());
    CHECK(*opt == (g_temp.path / "deep_helper_marker.txt").lexically_normal());

    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}

TEST_CASE("typed_resolver_for_deep_code: no-mount + relative returns nullopt") {
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    const auto saved = chronon3d::runtime::process_wide_assets_root();
    chronon3d::runtime::set_process_wide_assets_root("");

    const auto& resolver =
        chronon3d::runtime::typed_resolver_for_deep_code();
    CHECK_FALSE(resolver.resolve_lexical(
        std::filesystem::path("any_relative_path")).has_value());

    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}

TEST_CASE("typed_resolver_for_deep_code: absolute path bypasses mount root") {
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    const auto saved = chronon3d::runtime::process_wide_assets_root();
    chronon3d::runtime::set_process_wide_assets_root(g_temp.path.string());

    const auto abs = std::filesystem::current_path() /
                     "helper_abs_bypass_marker.txt";
    {
        std::ofstream f(abs);
        f << "x";
    }
    const auto& resolver =
        chronon3d::runtime::typed_resolver_for_deep_code();
    const auto opt = resolver.resolve_lexical(abs);
    REQUIRE(opt.has_value());
    CHECK(*opt == abs.lexically_normal());

    remove_if_present(abs);
    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}

TEST_CASE("typed_resolver_for_deep_code: empty input rounds-trips to nullopt") {
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    const auto saved = chronon3d::runtime::process_wide_assets_root();
    chronon3d::runtime::set_process_wide_assets_root(g_temp.path.string());

    const auto& resolver =
        chronon3d::runtime::typed_resolver_for_deep_code();
    CHECK_FALSE(resolver.resolve_lexical(std::filesystem::path{})
                     .has_value());

    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}

TEST_CASE("typed_resolver_for_deep_code: stable reference across calls") {
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    const auto saved = chronon3d::runtime::process_wide_assets_root();
    chronon3d::runtime::set_process_wide_assets_root(g_temp.path.string());

    const auto& resolver_a =
        chronon3d::runtime::typed_resolver_for_deep_code();
    const auto& resolver_b =
        chronon3d::runtime::typed_resolver_for_deep_code();
    CHECK(&resolver_a == &resolver_b);  // same static fallback reference

    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}

TEST_CASE("typed_resolver_for_deep_code: static cache holds first mount across set+reset") {
    // PR 8.1 contract: first-mount-only.  Once `g_deep_resolver` is
    // mounted, subsequent `set_process_wide_assets_root(...)` calls
    // do NOT propagate.  Verified by observing the resolver's mount
    // root directly between transitions — not via resolve_lexical
    // (which would conflate mount-root state with existence/clamp
    // checks).
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    const auto dir1 = (g_temp.path / "cache_dir1").lexically_normal();
    const auto dir2 = (g_temp.path / "cache_dir2").lexically_normal();
    std::filesystem::create_directories(dir1);
    std::filesystem::create_directories(dir2);

    const auto saved = chronon3d::runtime::process_wide_assets_root();

    // Step 1: set(/dir1) + helper call -- first mount succeeds; root = /dir1.
    chronon3d::runtime::set_process_wide_assets_root(dir1.string());
    const auto& resolver =
        chronon3d::runtime::typed_resolver_for_deep_code();
    REQUIRE(resolver.has_mount());
    CHECK(resolver.mount_root() == dir1);

    // Step 2: set(/dir2) -- cache holds /dir1 even though the new
    // process-wide root is /dir2.  Calling the helper again returns
    // the same static; `mount_root()` is unchanged.
    chronon3d::runtime::set_process_wide_assets_root(dir2.string());
    const auto& resolver_repeat =
        chronon3d::runtime::typed_resolver_for_deep_code();
    CHECK(&resolver_repeat == &resolver);            // same static fallback
    CHECK(resolver_repeat.has_mount());
    CHECK(resolver_repeat.mount_root() == dir1);     // still first root

    // Step 3: reset hook + helper re-call -- the reset unmounts; the
    // fresh helper call sees `!has_mount()` and re-mounts against
    // the current process_wide_assets_root (/dir2, already set in
    // step 2 — no need to re-set).
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    CHECK(!resolver_repeat.has_mount());
    const auto& resolver_after_reset =
        chronon3d::runtime::typed_resolver_for_deep_code();
    CHECK(&resolver_after_reset == &resolver);       // same static identity
    CHECK(resolver_after_reset.has_mount());
    CHECK(resolver_after_reset.mount_root() == dir2);

    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}

TEST_CASE("typed_resolver_for_deep_code: non-absolute process root skipped") {
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
    const auto saved = chronon3d::runtime::process_wide_assets_root();
    chronon3d::runtime::set_process_wide_assets_root("relative/not/abs");

    const auto& resolver =
        chronon3d::runtime::typed_resolver_for_deep_code();
    // Non-absolute process root must NOT mount the resolver.  Any
    // subsequent relative resolve returns nullopt, the same as the
    // unmounted path.
    CHECK(!resolver.has_mount());
    CHECK_FALSE(resolver.resolve_lexical(
        std::filesystem::path("anything")).has_value());

    chronon3d::runtime::set_process_wide_assets_root(saved);
    chronon3d::runtime::detail::reset_typed_resolver_for_deep_code_for_testing();
}
