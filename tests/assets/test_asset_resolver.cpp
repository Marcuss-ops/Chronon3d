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

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <chrono>
#include <filesystem>
#include <thread>
#include <fstream>
#include <string>
#include <string_view>
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

void write_file(const std::filesystem::path& root,
                const std::filesystem::path& rel,
                std::string_view contents) {
    const auto full = root / rel;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream file(full, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

chronon3d::render_plan::RenderPlan image_plan(std::string path) {
    chronon3d::render_plan::RenderPlan plan;
    chronon3d::render_plan::LayerPlan layer;
    layer.id = "image";
    layer.type = chronon3d::render_plan::LayerType::Image;
    layer.asset = std::move(path);
    plan.layers.push_back(std::move(layer));
    return plan;
}

const chronon3d::assets::PreparedAsset& only_asset(
    const chronon3d::assets::PreparedAssetManifest& manifest) {
    return manifest.assets().front();
}

chronon3d::render_plan::RenderPlan text_plan(std::string preset = {}) {
    chronon3d::render_plan::RenderPlan plan;
    chronon3d::render_plan::LayerPlan layer;
    layer.id = "text";
    layer.type = chronon3d::render_plan::LayerType::Text;
    layer.text = "HELLO";
    layer.preset = std::move(preset);
    plan.layers.push_back(std::move(layer));
    return plan;
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

TEST_CASE("AssetResolver::resolve_logical enforces the mounted root") {
    ensure_file("logical/ok.txt");
    chronon3d::assets::AssetResolver r;
    r.mount(g_temp.path);

    const auto resolved = r.resolve_logical("logical/ok.txt");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == (g_temp.path / "logical/ok.txt").lexically_normal());
    CHECK_FALSE(r.resolve_logical("../logical/ok.txt").has_value());
    CHECK_FALSE(r.resolve_logical(g_temp.path / "logical/ok.txt").has_value());
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

TEST_CASE("PreparedAssetManifest hashes and normalizes logical assets") {
    write_file(g_temp.path, "images/hero.png", "abc");
    auto plan = image_plan("images/./hero.png");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    auto result = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    REQUIRE(result);
    const auto& asset = only_asset(result.value());
    CHECK(asset.logical_path == "images/hero.png");
    CHECK(asset.kind == chronon3d::assets::PreparedAssetKind::Image);
    CHECK(asset.byte_size == 3);
    CHECK(asset.content_digest.hex() ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(result->manifest_digest().hex().size() == 64);
}

TEST_CASE("PreparedAssetManifest digest cache hits and invalidates safely") {
    write_file(g_temp.path, "images/cache.png", "cache-v1");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);
    const auto plan = image_plan("images/cache.png");

    const auto before = chronon3d::assets::asset_digest_cache_stats();
    const auto cold = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    REQUIRE(cold);
    const auto after_cold = chronon3d::assets::asset_digest_cache_stats();
    CHECK(after_cold.misses >= before.misses + 1);
    CHECK(only_asset(cold.value()).content_digest ==
          chronon3d::assets::sha256_string("cache-v1"));

    const auto warm = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    REQUIRE(warm);
    const auto after_warm = chronon3d::assets::asset_digest_cache_stats();
    CHECK(after_warm.hits >= after_cold.hits + 1);
    CHECK(after_warm.bytes_hashed == after_cold.bytes_hashed);
    CHECK(warm->manifest_digest() == cold->manifest_digest());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    write_file(g_temp.path, "images/cache.png", "cache-v2");
    const auto changed = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    REQUIRE(changed);
    const auto after_change = chronon3d::assets::asset_digest_cache_stats();
    CHECK(after_change.invalidations >= after_warm.invalidations + 1);
    CHECK(after_change.bytes_hashed > after_warm.bytes_hashed);
    CHECK(only_asset(changed.value()).content_digest !=
          only_asset(warm.value()).content_digest);
}

TEST_CASE("PreparedAssetManifest hashes font_asset references as Font assets") {
    write_file(g_temp.path, "fonts/custom.ttf", "font-bytes");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    auto plan = text_plan();
    chronon3d::render_plan::FontAssetPlan font_asset;
    font_asset.asset = "fonts/custom.ttf";
    font_asset.family = "Custom";
    font_asset.weight = 700;
    plan.layers.front().font_asset = std::move(font_asset);

    auto result = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    REQUIRE(result);
    REQUIRE(result->assets().size() == 1);
    const auto& asset = result->assets().front();
    CHECK(asset.logical_path == "fonts/custom.ttf");
    CHECK(asset.kind == chronon3d::assets::PreparedAssetKind::Font);
    CHECK(asset.content_digest == chronon3d::assets::sha256_string("font-bytes"));
}

TEST_CASE("PreparedAssetManifest hashes the visual preset's default font asset") {
    write_file(g_temp.path, "assets/fonts/Poppins-Bold.ttf", "poppins-bold-bytes");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    auto plan = text_plan("caption_card");
    auto result = chronon3d::assets::prepare_asset_manifest(plan, resolver);
    REQUIRE(result);
    REQUIRE(result->assets().size() == 1);
    const auto& asset = result->assets().front();
    CHECK(asset.logical_path == "assets/fonts/Poppins-Bold.ttf");
    CHECK(asset.kind == chronon3d::assets::PreparedAssetKind::Font);
    CHECK(asset.content_digest == chronon3d::assets::sha256_string("poppins-bold-bytes"));
}

TEST_CASE("PreparedAssetManifest rejects invalid logical paths") {
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    auto absolute = image_plan((g_temp.path / "images/hero.png").string());
    auto absolute_result = chronon3d::assets::prepare_asset_manifest(absolute, resolver);
    REQUIRE_FALSE(absolute_result);
    CHECK(absolute_result.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::AbsolutePathRejected);

    auto windows_absolute = image_plan("C:\\Windows\\secret.png");
    auto windows_result = chronon3d::assets::prepare_asset_manifest(windows_absolute, resolver);
    REQUIRE_FALSE(windows_result);
    CHECK(windows_result.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::AbsolutePathRejected);

    auto traversal = image_plan("images/../../secret.png");
    auto traversal_result = chronon3d::assets::prepare_asset_manifest(traversal, resolver);
    REQUIRE_FALSE(traversal_result);
    CHECK(traversal_result.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::PathTraversalRejected);
}

TEST_CASE("PreparedAssetManifest checks existence and asset kind") {
    write_file(g_temp.path, "images/real.png", "image");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    auto missing = image_plan("images/missing.png");
    auto missing_result = chronon3d::assets::prepare_asset_manifest(missing, resolver);
    REQUIRE_FALSE(missing_result);
    CHECK(missing_result.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::MissingAsset);

    write_file(g_temp.path, "video.mp4", "video");
    auto wrong_kind = image_plan("video.mp4");
    auto wrong_kind_result = chronon3d::assets::prepare_asset_manifest(wrong_kind, resolver);
    REQUIRE_FALSE(wrong_kind_result);
    CHECK(wrong_kind_result.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::WrongAssetKind);
}

TEST_CASE("PreparedAssetManifest enforces per-asset and total budgets") {
    write_file(g_temp.path, "images/one.png", "1234");
    write_file(g_temp.path, "images/two.png", "5678");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    chronon3d::assets::AssetPreflightPolicy single_policy;
    single_policy.max_single_asset_bytes = 3;
    auto single = chronon3d::assets::prepare_asset_manifest(
        image_plan("images/one.png"), resolver, single_policy);
    REQUIRE_FALSE(single);
    CHECK(single.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::AssetTooLarge);

    auto total_plan = image_plan("images/one.png");
    chronon3d::render_plan::LayerPlan second;
    second.id = "second";
    second.type = chronon3d::render_plan::LayerType::Image;
    second.asset = "images/two.png";
    total_plan.layers.push_back(std::move(second));
    chronon3d::assets::AssetPreflightPolicy total_policy;
    total_policy.max_total_asset_bytes = 7;
    auto total = chronon3d::assets::prepare_asset_manifest(
        total_plan, resolver, total_policy);
    REQUIRE_FALSE(total);
    CHECK(total.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::TotalBudgetExceeded);
}

TEST_CASE("PreparedAssetManifest keeps symlinks inside root and rejects escapes") {
    const auto target = g_temp.path / "images/target.png";
    write_file(g_temp.path, "images/target.png", "inside");
    std::error_code ec;
    std::filesystem::create_symlink(target, g_temp.path / "images/alias.png", ec);
    if (ec) {
        MESSAGE("symlink creation unavailable: " << ec.message());
        return;
    }
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);
    auto internal = chronon3d::assets::prepare_asset_manifest(
        image_plan("images/alias.png"), resolver);
    REQUIRE(internal);
    CHECK(only_asset(internal.value()).content_digest ==
          only_asset(chronon3d::assets::prepare_asset_manifest(
              image_plan("images/target.png"), resolver).value()).content_digest);

    const auto outside = g_temp.path.parent_path() / "chronon3d_outside.png";
    write_file(g_temp.path.parent_path(), outside.filename(), "outside");
    std::filesystem::create_symlink(outside, g_temp.path / "images/outside.png", ec);
    if (ec) {
        MESSAGE("external symlink creation unavailable: " << ec.message());
        return;
    }
    auto external = chronon3d::assets::prepare_asset_manifest(
        image_plan("images/outside.png"), resolver);
    REQUIRE_FALSE(external);
    CHECK(external.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::SymlinkOutsideRoot);
}

TEST_CASE("PreparedAssetManifest is deterministic and deduplicates assets") {
    const auto root_a = g_temp.path / "root_a";
    const auto root_b = g_temp.path / "root_b";
    write_file(root_a, "images/shared.png", "same-bytes");
    write_file(root_b, "images/shared.png", "same-bytes");
    chronon3d::assets::AssetResolver resolver_a;
    chronon3d::assets::AssetResolver resolver_b;
    resolver_a.mount(root_a);
    resolver_b.mount(root_b);

    auto first = image_plan("images/shared.png");
    chronon3d::render_plan::LayerPlan duplicate;
    duplicate.id = "duplicate";
    duplicate.type = chronon3d::render_plan::LayerType::Image;
    duplicate.asset = "images/shared.png";
    first.layers.push_back(duplicate);
    auto first_result = chronon3d::assets::prepare_asset_manifest(first, resolver_a);
    REQUIRE(first_result);
    CHECK(first_result->assets().size() == 1);

    write_file(root_a, "images/other.png", "other");
    auto ordered = image_plan("images/shared.png");
    chronon3d::render_plan::LayerPlan other;
    other.id = "other";
    other.type = chronon3d::render_plan::LayerType::Image;
    other.asset = "images/other.png";
    ordered.layers.push_back(other);
    auto ordered_result = chronon3d::assets::prepare_asset_manifest(ordered, resolver_a);
    REQUIRE(ordered_result);

    auto reversed = image_plan("images/other.png");
    chronon3d::render_plan::LayerPlan shared;
    shared.id = "shared";
    shared.type = chronon3d::render_plan::LayerType::Image;
    shared.asset = "images/shared.png";
    reversed.layers.push_back(shared);
    auto reversed_result = chronon3d::assets::prepare_asset_manifest(reversed, resolver_a);
    REQUIRE(reversed_result);

    auto root_b_result = chronon3d::assets::prepare_asset_manifest(
        image_plan("images/shared.png"), resolver_b);
    REQUIRE(root_b_result);
    CHECK(only_asset(first_result.value()).content_digest ==
          only_asset(root_b_result.value()).content_digest);
    CHECK(only_asset(first_result.value()).logical_path ==
          only_asset(root_b_result.value()).logical_path);
    CHECK(first_result->manifest_digest() == root_b_result->manifest_digest());
    CHECK(ordered_result->manifest_digest() == reversed_result->manifest_digest());

    write_file(root_b, "images/shared.png", "changed-bytes");
    auto changed = chronon3d::assets::prepare_asset_manifest(
        image_plan("images/shared.png"), resolver_b);
    REQUIRE(changed);
    CHECK(only_asset(changed.value()).content_digest !=
          only_asset(root_b_result.value()).content_digest);
    CHECK(changed->manifest_digest() != root_b_result->manifest_digest());
}

TEST_CASE("PreparedAssetManifest detects bytes changed after preflight") {
    write_file(g_temp.path, "images/changed.png", "AAAA");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    const auto prepared = chronon3d::assets::prepare_asset_manifest(
        image_plan("images/changed.png"), resolver);
    REQUIRE(prepared);

    // Keep the size stable: the integrity boundary must not rely on size
    // alone. The timestamp fast path falls back to SHA-256 after this write.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    write_file(g_temp.path, "images/changed.png", "BBBB");
    const auto verified = chronon3d::assets::verify_asset_manifest(
        prepared.value(), resolver);
    REQUIRE_FALSE(verified);
    CHECK(verified.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::AssetChangedAfterPreflight);
    CHECK(verified.error().logical_path == "images/changed.png");
}

TEST_CASE("PreparedAssetStore owns subtitle bytes and keeps media metadata") {
    write_file(g_temp.path, "captions.srt", "1\n00:00:00,000 --> 00:00:01,000\nHello\n");
    write_file(g_temp.path, "clip.mp4", "media");
    chronon3d::render_plan::RenderPlan plan;
    chronon3d::render_plan::LayerPlan subtitle;
    subtitle.id = "captions";
    subtitle.type = chronon3d::render_plan::LayerType::SubtitleTrack;
    subtitle.source = "captions.srt";
    subtitle.subtitle_format = chronon3d::render_plan::SubtitleFormat::Srt;
    plan.layers.push_back(subtitle);
    chronon3d::render_plan::LayerPlan video;
    video.id = "video";
    video.type = chronon3d::render_plan::LayerType::Video;
    video.source = "clip.mp4";
    plan.layers.push_back(video);

    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);
    auto result = chronon3d::assets::prepare_asset_store(plan, resolver);
    REQUIRE(result);

    const auto captions = result->find(
        "captions.srt", chronon3d::assets::PreparedAssetKind::Subtitle);
    REQUIRE(captions);
    CHECK(std::string(reinterpret_cast<const char*>(captions->bytes.data()),
                      captions->bytes.size()) ==
          "1\n00:00:00,000 --> 00:00:01,000\nHello\n");
    CHECK(captions->content_digest == only_asset(
        chronon3d::assets::prepare_asset_manifest(plan, resolver).value()).content_digest);

    const auto media = result->find(
        "clip.mp4", chronon3d::assets::PreparedAssetKind::Video);
    REQUIRE(media);
    CHECK(media->bytes.empty());
    CHECK(media->byte_size == 5);
}

TEST_CASE("PreparedAssetStore recovers after a failed preflight and keeps prepared bytes immutable") {
    const auto subtitle = std::filesystem::path("recovery/captions.srt");
    chronon3d::render_plan::RenderPlan plan;
    chronon3d::render_plan::LayerPlan layer;
    layer.id = "recovery-captions";
    layer.type = chronon3d::render_plan::LayerType::SubtitleTrack;
    layer.source = subtitle.generic_string();
    layer.subtitle_format = chronon3d::render_plan::SubtitleFormat::Srt;
    plan.layers.push_back(layer);

    chronon3d::assets::AssetResolver resolver;
    resolver.mount(g_temp.path);

    auto failed = chronon3d::assets::prepare_asset_store(plan, resolver);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code ==
          chronon3d::assets::AssetPreflightErrorCode::MissingAsset);

    const std::string original = "1\n00:00:00,000 --> 00:00:01,000\nRecover me\n";
    write_file(g_temp.path, subtitle, original);
    auto recovered = chronon3d::assets::prepare_asset_store(plan, resolver);
    REQUIRE(recovered);

    const auto view = recovered->find(
        subtitle.generic_string(), chronon3d::assets::PreparedAssetKind::Subtitle);
    REQUIRE(view);
    CHECK(std::string(reinterpret_cast<const char*>(view->bytes.data()),
                      view->bytes.size()) == original);

    write_file(g_temp.path, subtitle, "changed after preparation");
    CHECK(std::string(reinterpret_cast<const char*>(view->bytes.data()),
                      view->bytes.size()) == original);
    CHECK(view->content_digest == chronon3d::assets::sha256_string(original));
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
