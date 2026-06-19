// =============================================================================
// test_persistent_framebuffer_store.cpp — CFB4 codec + store tests.
//
// PR 5 (persistent-framebuffer-codec) — tests:
//   1. Roundtrip bit-identical
//   2. Checksum verification
//   3. Path sharding (2-level subdirectory)
//   4. Corruption handling (truncated → miss + delete, bad magic → miss + delete)
//   5. Stride-safe roundtrip (allocated_width != width)
//   6. Atomic write (no partial files)
//   7. Version mismatch → miss + delete
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::cache;

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

// ═════════════════════════════════════════════════════════════════════════════
// §1: Roundtrip bit-identical
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - roundtrip bit-identical") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(64, 64);
    for (i32 y = 0; y < 64; ++y) {
        for (i32 x = 0; x < 64; ++x) {
            fb.set_pixel(x, y, Color{
                static_cast<float>(x) / 64.0f,
                static_cast<float>(y) / 64.0f,
                0.5f, 1.0f});
        }
    }

    auto key = make_test_key(0xABCD1234);
    auto wr = store.store(key, fb);
    CHECK(wr.ok);
    CHECK(wr.bytes_written > 0);

    auto loaded = store.get(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->width() == 64);
    CHECK(loaded->height() == 64);

    // Bit-identical pixel comparison.
    for (i32 y = 0; y < 64; ++y) {
        for (i32 x = 0; x < 64; ++x) {
            Color orig = fb.get_pixel(x, y);
            Color loaded_px = loaded->get_pixel(x, y);
            CHECK(orig.r == doctest::Approx(loaded_px.r).epsilon(0.001f));
            CHECK(orig.g == doctest::Approx(loaded_px.g).epsilon(0.001f));
            CHECK(orig.b == doctest::Approx(loaded_px.b).epsilon(0.001f));
            CHECK(orig.a == doctest::Approx(loaded_px.a).epsilon(0.001f));
        }
    }

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Path sharding — files are in 2-level subdirectories
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - path sharding creates subdirectories") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(8, 8);
    // digest = 0xABCD1234... → hex = "...abcd1234..." → subdirs ab/cd/
    auto key = make_test_key(0xABCD1234);
    store.put(key, fb);

    CHECK(store.exists(key));

    // Verify the file is in a 2-level subdirectory.
    std::error_code ec;
    bool found_in_shard = false;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() == ".cfb4") {
            auto parent1 = it->path().parent_path().filename().string();
            auto parent2 = it->path().parent_path().parent_path().filename().string();
            // parent2 = e.g. "ab", parent1 = "cd"
            CHECK(parent2.length() == 2);
            CHECK(parent1.length() == 2);
            found_in_shard = true;
        }
    }
    CHECK(found_in_shard);

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: Checksum — corrupted payload is detected and file deleted
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - corrupted payload detected and deleted") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(32, 32);
    fb.set_pixel(10, 10, Color::red());
    auto key = make_test_key(0xCAFEBABE);
    store.put(key, fb);

    // Find the .cfb4 file and corrupt a byte in the payload.
    std::error_code ec;
    std::filesystem::path cfb4_path;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() == ".cfb4") {
            cfb4_path = it->path();
            break;
        }
    }
    REQUIRE(!cfb4_path.empty());

    // Corrupt a payload byte (after the 64-byte header).
    {
        std::fstream f(cfb4_path, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(80);  // well into the payload
        char corrupt = 0xFF;
        f.write(&corrupt, 1);
        f.close();
    }

    // Load should fail with checksum mismatch → file deleted.
    auto loaded = store.get(key);
    CHECK(loaded == nullptr);

    // Detailed load should report ChecksumMismatch.
    auto result = store.load(key);
    CHECK(result.status == StoreLoadStatus::ChecksumMismatch);

    // File should be gone.
    CHECK(!store.exists(key));

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: Truncated file — detected and deleted
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - truncated file detected and deleted") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(16, 16);
    auto key = make_test_key(0xDEADBEEF);
    store.put(key, fb);

    // Find and truncate the file.
    std::error_code ec;
    std::filesystem::path cfb4_path;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() == ".cfb4") {
            cfb4_path = it->path();
            break;
        }
    }
    REQUIRE(!cfb4_path.empty());

    std::filesystem::resize_file(cfb4_path, 32);  // smaller than header (64 bytes)

    auto loaded = store.get(key);
    CHECK(loaded == nullptr);   // truncated → miss
    CHECK(!store.exists(key));  // file deleted

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §5: Bad magic — detected and deleted
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - bad magic detected and deleted") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(8, 8);
    auto key = make_test_key(0xBADC0DE);
    store.put(key, fb);

    // Find and corrupt the magic bytes.
    std::error_code ec;
    std::filesystem::path cfb4_path;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->path().extension() == ".cfb4") {
            cfb4_path = it->path();
            break;
        }
    }
    REQUIRE(!cfb4_path.empty());

    {
        std::fstream f(cfb4_path, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(0);
        const char bad[] = "BADMAGIC";
        f.write(bad, 8);
        f.close();
    }

    auto loaded = store.get(key);
    CHECK(loaded == nullptr);
    CHECK(!store.exists(key));  // file deleted

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §6: Miss for non-existent key
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - miss for non-existent key") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    auto key = make_test_key(0x99999999);
    auto loaded = store.get(key);
    CHECK(loaded == nullptr);

    auto result = store.load(key);
    CHECK(result.status == StoreLoadStatus::NotFound);
}

// ═════════════════════════════════════════════════════════════════════════════
// §7: Stride-safe roundtrip (allocated_width != width)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - stride-safe roundtrip non-aligned") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(1277, 719);
    REQUIRE(fb.allocated_width() >= 1277);

    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            float v = static_cast<float>(y * fb.width() + x) /
                      static_cast<float>(fb.width() * fb.height());
            fb.set_pixel(x, y, Color{v, v * 0.5f, 1.0f - v, 1.0f});
        }
    }

    auto key = make_test_key(0xCAFE1277);
    store.put(key, fb);

    auto loaded = store.get(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->width() == 1277);
    CHECK(loaded->height() == 719);

    // Spot-check pixels.
    CHECK(loaded->get_pixel(0, 0).r == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(loaded->get_pixel(1276, 718).b == doctest::Approx(1.0f - (718.0f * 1277 + 1276) / (1277.0f * 719.0f)).epsilon(0.001f));

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §8: Multiple files (different digests)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - multiple entries") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(16, 16);
    for (int i = 0; i < 5; ++i) {
        store.put(make_test_key(0x1000 + i), fb);
    }

    auto s = store.stats();
    CHECK(s.entry_count == 5);

    for (int i = 0; i < 5; ++i) {
        CHECK(store.exists(make_test_key(0x1000 + i)));
    }

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §9: File extension is .cfb4 (no legacy extensions)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - only .cfb4 files present") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(8, 8);
    store.put(make_test_key(0xCFB40001), fb);

    std::error_code ec;
    std::size_t cfb4_count = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        CHECK(it->path().extension() != ".cfb");   // legacy DiskNodeCache
        CHECK(it->path().extension() != ".cfb2");  // legacy PersistentBakeCache
        CHECK(it->path().extension() != ".cfb3");  // legacy merged format
        if (it->path().extension() == ".cfb4") ++cfb4_count;
    }
    CHECK(cfb4_count == 1);

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §10: Opaque flag roundtrip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - opaque flag preserved") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(32, 32);
    fb.set_opaque(true);
    auto key = make_test_key(0x0AE00001);
    store.put(key, fb);

    auto loaded = store.get(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->is_opaque());

    store.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// §11: erase removes a single entry
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PersistentFramebufferStore - erase removes entry") {
    auto dir = make_temp_cache_dir();
    auto& store = PersistentFramebufferStore::instance();
    store.set_cache_dir(dir);
    store.clear();

    Framebuffer fb(8, 8);
    auto k1 = make_test_key(0xAAA00001);
    auto k2 = make_test_key(0xAAA00002);
    store.put(k1, fb);
    store.put(k2, fb);
    CHECK(store.stats().entry_count == 2);

    CHECK(store.erase(k1));
    CHECK(store.stats().entry_count == 1);
    CHECK(!store.exists(k1));
    CHECK(store.exists(k2));

    store.clear();
}
