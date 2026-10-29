#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include "commands/render/bake_layer_cache.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using chronon3d::cli::assets_tree_fingerprint;
using chronon3d::cli::BakeLayerCache;
using chronon3d::cli::BakeLayerCacheKeyInput;
using chronon3d::cli::bake_layer_cache_filename;
using chronon3d::cli::bake_layer_cache_key;

namespace {

BakeLayerCacheKeyInput base_input() {
    BakeLayerCacheKeyInput input;
    input.node_content_digest = 0x0123456789abcdefULL;
    input.width = 1920;
    input.height = 1080;
    input.comp_id = "comp";
    input.layer_id = "layer";
    return input;
}

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("bake_layer_cache_test_" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

} // namespace

TEST_CASE("bake cache key: identical inputs fold to the same key") {
    CHECK(bake_layer_cache_key(base_input()) == bake_layer_cache_key(base_input()));
    CHECK(bake_layer_cache_filename(base_input()) ==
          bake_layer_cache_filename(base_input()));
}

TEST_CASE("bake cache key: every render-relevant field changes the key") {
    const BakeLayerCacheKeyInput base = base_input();

    auto changed = base;
    changed.node_content_digest += 1;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.width += 1;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.height += 1;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.exr_bake = true;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.diagnostic_overlay = true;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.diagnostic_overlay_only = true;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.assets_fingerprint += 1;
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.comp_id = "other";
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));

    changed = base;
    changed.layer_id = "other";
    CHECK(bake_layer_cache_key(changed) != bake_layer_cache_key(base));
}

TEST_CASE("bake cache key: node identity carries static versus animated frame state") {
    const auto static_node = base_input();
    auto animated_node = base_input();
    // NodeCacheKey::digest() already includes the effective frame for a
    // frame-varying node and canonicalizes it for a static node. The bake
    // cache must reuse static outputs across requested timeline frames while
    // different animated-node digests remain distinct.
    animated_node.node_content_digest += 1;
    CHECK(bake_layer_cache_key(static_node) == bake_layer_cache_key(static_node));
    CHECK(bake_layer_cache_key(animated_node) != bake_layer_cache_key(static_node));
}

TEST_CASE("bake cache key: field re-partitioning cannot fold two keys together") {
    // A naive concat hash lets (a="ab", b="c") collide with (a="a", b="bc").
    auto left = base_input();
    left.comp_id = "ab";
    left.layer_id = "c";
    auto right = base_input();
    right.comp_id = "a";
    right.layer_id = "bc";
    CHECK(bake_layer_cache_key(left) != bake_layer_cache_key(right));
}

TEST_CASE("bake cache filename: 16 hex digits plus the format extension") {
    auto input = base_input();
    std::string name = bake_layer_cache_filename(input);
    REQUIRE(name.size() == 16 + 4);
    CHECK(name.substr(16) == ".png");
    for (std::size_t i = 0; i < 16; ++i) {
        const char c = name[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        CHECK(hex);
    }

    input.exr_bake = true;
    name = bake_layer_cache_filename(input);
    REQUIRE(name.size() == 16 + 4);
    CHECK(name.substr(16) == ".exr");
}

TEST_CASE("bake cache: a miss is a miss until the entry is stored") {
    TempDir dir;
    const BakeLayerCache cache(dir.path() / "entries");
    const std::string name = bake_layer_cache_filename(base_input());

    CHECK_FALSE(cache.lookup(name).has_value());

    const auto source = dir.path() / "baked.png";
    write_file(source, "PNG-BYTES");
    REQUIRE(cache.store_from(source, name));

    const auto hit = cache.lookup(name);
    REQUIRE(hit.has_value());
    CHECK(std::filesystem::file_size(*hit) == 9);
}

TEST_CASE("bake cache: materialize copies the entry to the caller's output path") {
    TempDir dir;
    const BakeLayerCache cache(dir.path() / "entries");
    const std::string name = bake_layer_cache_filename(base_input());

    const auto source = dir.path() / "baked.png";
    write_file(source, "PNG-BYTES");
    REQUIRE(cache.store_from(source, name));
    const auto hit = cache.lookup(name);
    REQUIRE(hit.has_value());

    const auto output = dir.path() / "out" / "baked.png";
    std::filesystem::create_directories(output.parent_path());
    REQUIRE(cache.materialize(*hit, output));
    CHECK(std::filesystem::file_size(output) == 9);
}

TEST_CASE("bake cache: storing the same name twice replaces the entry atomically") {
    TempDir dir;
    const BakeLayerCache cache(dir.path() / "entries");
    const std::string name = bake_layer_cache_filename(base_input());

    const auto first = dir.path() / "first.png";
    write_file(first, "FIRST");
    REQUIRE(cache.store_from(first, name));

    const auto second = dir.path() / "second.png";
    write_file(second, "SECOND-AND-LONGER");
    REQUIRE(cache.store_from(second, name));

    const auto hit = cache.lookup(name);
    REQUIRE(hit.has_value());
    CHECK(std::filesystem::file_size(*hit) == 17);
    // No temporary file survives a successful store.
    std::size_t entries = 0;
    for (const auto& item : std::filesystem::directory_iterator(cache.root())) {
        (void)item;
        ++entries;
    }
    CHECK(entries == 1);
}

TEST_CASE("bake cache: storing a missing source fails without publishing") {
    TempDir dir;
    const BakeLayerCache cache(dir.path() / "entries");
    const std::string name = bake_layer_cache_filename(base_input());

    CHECK_FALSE(cache.store_from(dir.path() / "does_not_exist.png", name));
    CHECK_FALSE(cache.lookup(name).has_value());
}

TEST_CASE("assets fingerprint: a missing root hashes as zero") {
    CHECK(assets_tree_fingerprint({}) == 0);
    CHECK(assets_tree_fingerprint("/definitely/not/a/real/directory") == 0);
}

TEST_CASE("assets fingerprint: changing the asset tree changes the fingerprint") {
    TempDir dir;
    const auto assets = dir.path() / "assets";
    std::filesystem::create_directories(assets);

    write_file(assets / "a.png", "AAAA");
    const std::uint64_t before = assets_tree_fingerprint(assets);
    CHECK(before != 0);

    // Same name, different content size: the tree fingerprint must move.
    write_file(assets / "a.png", "AAAAAAAA");
    CHECK(assets_tree_fingerprint(assets) != before);

    // Adding a file must move it again.
    const std::uint64_t after_resize = assets_tree_fingerprint(assets);
    write_file(assets / "b.txt", "B");
    CHECK(assets_tree_fingerprint(assets) != after_resize);
}

TEST_CASE("assets fingerprint: path changes invalidate even with equal byte totals") {
    TempDir dir;
    const auto assets = dir.path() / "assets";
    std::filesystem::create_directories(assets);
    const auto original = assets / "one.png";
    const auto renamed = assets / "two.png";
    write_file(original, "same-bytes");
    const std::uint64_t before = assets_tree_fingerprint(assets);

    std::filesystem::rename(original, renamed);
    CHECK(assets_tree_fingerprint(assets) != before);
}
