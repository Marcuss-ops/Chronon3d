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
