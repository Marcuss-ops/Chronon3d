#include <doctest/doctest.h>

#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <cstdlib>
#include <filesystem>
using namespace chronon3d;

using namespace chronon3d::cache;
using namespace chronon3d::graph;

namespace {

std::filesystem::path make_temp_cache_dir() {
    auto path = std::filesystem::temp_directory_path() / "chronon3d_test_persistent_fb_store";
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

NodeCacheKey make_test_key(uint64_t digest) {
    return NodeCacheKey{
        .scope  = "test",
        .frame  = 0,
        .width  = 100,
        .height = 100,
        .params_hash = digest
    };
}

} // namespace

// ── Original PersistentBakeCache coverage ported to the unified store ────

TEST_CASE("PersistentFramebufferStore - put and get roundtrip") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);

    Framebuffer fb(64, 64);
    fb.set_pixel(10, 10, Color::red());
    fb.set_pixel(20, 20, Color::blue());

    auto key = make_test_key(0xABCD1234);
    PersistentFramebufferStore::instance().put(key, fb);
    CHECK(PersistentFramebufferStore::instance().exists(key));

    auto loaded = PersistentFramebufferStore::instance().get(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->width() == 64);
    CHECK(loaded->height() == 64);
    CHECK(loaded->get_pixel(10, 10).r == Color::red().r);
    CHECK(loaded->get_pixel(20, 20).b == Color::blue().b);

    PersistentFramebufferStore::instance().clear();
}

TEST_CASE("PersistentFramebufferStore - second run reuses cache") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);

    Framebuffer fb(32, 32);
    fb.set_pixel(5, 5, Color::green());

    auto key = make_test_key(0xDEADBEEF);

    PersistentFramebufferStore::instance().put(key, fb);
    CHECK(PersistentFramebufferStore::instance().entry_count() == 1);

    auto loaded = PersistentFramebufferStore::instance().get(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->get_pixel(5, 5).g == Color::green().g);

    PersistentFramebufferStore::instance().clear();
}

TEST_CASE("PersistentFramebufferStore - different params_hash creates separate entries") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);

    Framebuffer fb(16, 16);
    auto key_a = make_test_key(0x11111111);
    auto key_b = make_test_key(0x22222222);

    PersistentFramebufferStore::instance().put(key_a, fb);
    PersistentFramebufferStore::instance().put(key_b, fb);

    CHECK(PersistentFramebufferStore::instance().entry_count() == 2);

    PersistentFramebufferStore::instance().clear();
}

TEST_CASE("PersistentFramebufferStore - get returns nullptr for missing key") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);
    PersistentFramebufferStore::instance().clear();

    auto key = make_test_key(0x99999999);
    auto loaded = PersistentFramebufferStore::instance().get(key);

    CHECK(loaded == nullptr);
}

TEST_CASE("PersistentFramebufferStore - stride-safe roundtrip with non-aligned dimensions") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);
    PersistentFramebufferStore::instance().clear();

    // Non-aligned dimensions (1277x719) — neither multiple of 64 nor cache-line.
    Framebuffer fb(1277, 719);
    CHECK(fb.allocated_width() >= 1277);

    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            float v = static_cast<float>(y * fb.width() + x) /
                      static_cast<float>(fb.width() * fb.height());
            fb.set_pixel(x, y, Color{v, v * 0.5f, 1.0f - v, 1.0f});
        }
    }

    auto key = make_test_key(0xCAFE1277);
    PersistentFramebufferStore::instance().put(key, fb);
    CHECK(PersistentFramebufferStore::instance().exists(key));

    auto loaded = PersistentFramebufferStore::instance().get(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->width() == 1277);
    CHECK(loaded->height() == 719);

    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color orig = fb.get_pixel(x, y);
            Color loaded_px = loaded->get_pixel(x, y);
            CHECK(orig.r == doctest::Approx(loaded_px.r).epsilon(0.001f));
            CHECK(orig.g == doctest::Approx(loaded_px.g).epsilon(0.001f));
            CHECK(orig.b == doctest::Approx(loaded_px.b).epsilon(0.001f));
            CHECK(orig.a == doctest::Approx(loaded_px.a).epsilon(0.001f));
        }
    }

    PersistentFramebufferStore::instance().clear();
}

TEST_CASE("PersistentFramebufferStore - put_batch stores all entries") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);
    PersistentFramebufferStore::instance().clear();

    std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>> entries;
    for (int i = 0; i < 3; ++i) {
        auto fb = std::make_shared<Framebuffer>(16, 16);
        fb->set_pixel(i, i, Color::white());
        entries.emplace_back(make_test_key(0x1000 + i), fb);
    }

    PersistentFramebufferStore::instance().put_batch(entries);
    CHECK(PersistentFramebufferStore::instance().entry_count() == 3);

    for (int i = 0; i < 3; ++i) {
        auto loaded = PersistentFramebufferStore::instance().get(make_test_key(0x1000 + i));
        REQUIRE(loaded != nullptr);
        CHECK(loaded->get_pixel(i, i).r == Color::white().r);
    }

    PersistentFramebufferStore::instance().clear();
}

// ── New tests covering the post-merge behaviour ─────────────────────────

TEST_CASE("PersistentFramebufferStore - file extension is .cfb3 (CFB3 magic only)") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);
    PersistentFramebufferStore::instance().clear();

    Framebuffer fb(8, 8);
    auto key = make_test_key(0xCFB30001);
    PersistentFramebufferStore::instance().put(key, fb);

    // Exactly one .cfb3 file present.
    std::size_t cfb3_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        CHECK(entry.path().extension() != ".cfb");   // legacy DiskNodeCache
        CHECK(entry.path().extension() != ".cfb2");  // legacy PersistentBakeCache
        if (entry.path().extension() == ".cfb3") ++cfb3_count;
    }
    CHECK(cfb3_count == 1);

    PersistentFramebufferStore::instance().clear();
}

TEST_CASE("PersistentFramebufferStore - enabled gate short-circuits get / put") {
    auto dir = make_temp_cache_dir();
    PersistentFramebufferStore::instance().set_cache_dir(dir);
    PersistentFramebufferStore::instance().clear();

    // Force the gate closed for this test.
    const char* prev = std::getenv("CHRONON_DISABLE_PERSISTENT_FB_CACHE");
    ::setenv("CHRONON_DISABLE_PERSISTENT_FB_CACHE", "1", 1);

    Framebuffer fb(8, 8);
    auto key = make_test_key(0xCFB30002);
    PersistentFramebufferStore::instance().put(key, fb);

    // No file written when disabled.
    bool any = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        any = true;
        break;
    }
    CHECK(!any);

    CHECK(PersistentFramebufferStore::instance().get(key) == nullptr);
    CHECK(!PersistentFramebufferStore::instance().exists(key));
    CHECK(PersistentFramebufferStore::instance().entry_count() == 0);

    if (prev) ::setenv("CHRONON_DISABLE_PERSISTENT_FB_CACHE", prev, 1);
    else      ::unsetenv("CHRONON_DISABLE_PERSISTENT_FB_CACHE");
}
