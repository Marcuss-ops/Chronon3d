// ═══════════════════════════════════════════════════════════════════════════
// test_freetype_face_cache_concurrency.cpp — Bug #7 regression coverage
// ═══════════════════════════════════════════════════════════════════════════
//
// P0 race that was fixed by Fase 1#7: FreeTypeFaceCache held a single
// raw FT_Face slot.  Thread A was using that face (e.g. calling
// FT_Load_Glyph via GlyphOutlineCache::build_outline); thread B called
// get_face() for a different font; thread B's call did FT_Done_Face()
// on the slot's face before installing the new one — Thread A's
// subsequent FT_Load_Glyph hit a use-after-free.
//
// Fix: key-indexed cache (path, face_index, size_bucket) with
// shared_ptr<FT_Face> so that swapping or eviction never destroys
// a face still in use.  This file exercises the invariants:
//   1. concurrent get_face() across many threads is safe;
//   2. (path,idx,size) hit → shared underlying FT_Face;
//   3. same (path,idx) DIFFERENT size → independent FT_Face
//      (structurally prevents the secondary FT_Set_Pixel_Sizes race);
//   4. FontFaceHandle-style handles outlive cache destruction
//      (the lease anchor pattern ft_lifeline depends on);
//   5. invalid inputs (empty path, non-positive size) return nullptr
//      and never produce a "size-1 fallback" face.
//
// Framework: doctest (matching other tests/text/*.cpp conventions).

#include <chronon3d/backends/text/text_render_resources.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <chronon3d/core/types/types.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

// ── Fixtures ────────────────────────────────────────────────────────────

// tests/fixtures/ is the canonical location for tracked font TTF files
// (see tests/text/test_prewarm_text_layout_cache.cpp which uses
// Inter-Bold.ttf).  Any test that depends on TTF files must skip on
// missing fixtures so CI runs without bundled fonts still pass.
constexpr const char* kFontAPath = "tests/fixtures/Inter-Bold.ttf";
constexpr const char* kFontBPath = "assets/fonts/Poppins-Regular.ttf";

bool fixture_exists(const char* p) noexcept {
    if (p == nullptr) return false;
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

// Match the project's skipping convention (see tests/text/test_text_layout.cpp
// "MESSAGE(\"Skipping: <file> not available\");" pattern). Doctest does not
// have a WARN_SKIP macro — the canonical skip pattern is a MESSAGE log
// followed by an early return from the TEST_CASE body.
void skip_if_missing(const char* fixture, const char* what) noexcept {
    if (!fixture_exists(fixture)) {
        MESSAGE("Skipping: ", what, " requires ", fixture,
                " which is unavailable.");
    }
}

// Touch a glyph the same way GlyphOutlineCache::build_outline does:
// FT_Load_Glyph + check glyph->format.  Mimicking the call site of the
// bug means a regression to the old behaviour (raw FT_Face held
// across an FT_Done_Face on another thread) traps as a segfault or
// nullptr deref here, not as a subtle visual artifact.
bool touch_glyph(FT_Face face, FT_UInt glyph_id) noexcept {
    if (face == nullptr) return false;
    if (FT_Load_Glyph(face, glyph_id, FT_LOAD_NO_BITMAP) != 0) return false;
    return face->glyph != nullptr;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════
// 1. Many threads, many get_face calls, no UAF
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("freetype_face_cache: concurrent get_face across many threads") {
    if (!fixture_exists(kFontAPath) || !fixture_exists(kFontBPath)) {
        skip_if_missing(kFontAPath, "Fase 1#7 concurrency test");
        return;
    }

    FreeTypeFaceCache cache;
    constexpr int kThreads = 8;
    constexpr int kIters   = 50;

    std::atomic<int> ok_count{0};
    std::atomic<int> glyph_ok_count{0};
    std::vector<std::thread> workers;

    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            const char* path = (t % 2 == 0) ? kFontAPath : kFontBPath;
            for (int i = 0; i < kIters; ++i) {
                // Hold the shared_ptr alive across subsequent calls —
                // this is the precise lifetime pattern FontFaceHandle
                // uses so a regression to "raw pointer + manual free"
                // would be observable here.
                std::shared_ptr<FT_Face> face =
                    cache.get_face(path, /*face_index=*/0, /*font_size=*/16.0f);

                if (face && *face && touch_glyph(*face, /*glyph_id=*/0)) {
                    glyph_ok_count.fetch_add(1, std::memory_order_relaxed);
                }
                ok_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) w.join();

    CHECK(ok_count.load() == kThreads * kIters);
    CHECK(glyph_ok_count.load() > 0);

    // One entry per (path, 0, 16).  Both threads landed all hits;
    // neither font should have triggered fresh FT_New_Face after the
    // very first call per path.
    SUBCASE("exposes: identity-stable cache hits") {
        const auto a_face = cache.get_face(kFontAPath, 0, 16.0f);
        const auto b_face = cache.get_face(kFontBPath, 0, 16.0f);
        REQUIRE(a_face != nullptr);
        REQUIRE(b_face != nullptr);
        CHECK(a_face.get() != b_face.get());
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 2. Handle outlives cache destruction (lease-anchor contract)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("freetype_face_cache: shared_ptr outlives cache destruction") {
    if (!fixture_exists(kFontAPath)) {
        skip_if_missing(kFontAPath, "Fase 1#7 lease-anchor test");
        return;
    }

    std::shared_ptr<FT_Face> handle;
    {
        FreeTypeFaceCache cache;
        handle = cache.get_face(kFontAPath, /*face_index=*/0, /*font_size=*/24.0f);
        REQUIRE(handle != nullptr);
        REQUIRE(*handle != nullptr);
    } // <-- cache destructed here

    // The defining promise of the bug #7 fix: a FontFaceHandle held
    // by a renderer (e.g.) maps its ft_lifeline to a shared_ptr<FT_Face>
    // that survives even aggressive cache lifetimes.
    REQUIRE(handle != nullptr);
    REQUIRE(*handle != nullptr);
    CHECK(touch_glyph(*handle, /*glyph_id=*/0));
}

// ═════════════════════════════════════════════════════════════════════════
// 3. Same key → same underlying FT_Face (ref-count preserved)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("freetype_face_cache: identical key returns same FT_Face") {
    if (!fixture_exists(kFontAPath)) {
        skip_if_missing(kFontAPath, "Fase 1#7 identity test");
        return;
    }

    FreeTypeFaceCache cache;

    auto a = cache.get_face(kFontAPath, 0, 16.0f);
    auto b = cache.get_face(kFontAPath, 0, 16.0f);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    CHECK(a.get() == b.get());
    // ref-count reflects two outstanding holders of the same face.
    CHECK(a.use_count() >= 2);
}

// ═════════════════════════════════════════════════════════════════════════
// 4. Different size buckets → distinct FT_Face entries
//    (structurally prevents the FT_Set_Pixel_Sizes mutation race)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("freetype_face_cache: distinct size buckets get distinct entries") {
    if (!fixture_exists(kFontAPath)) {
        skip_if_missing(kFontAPath, "Fase 1#7 size-bucket test");
        return;
    }

    FreeTypeFaceCache cache;

    auto size16 = cache.get_face(kFontAPath, 0, 16.0f);
    auto size24 = cache.get_face(kFontAPath, 0, 24.0f);

    REQUIRE(size16 != nullptr);
    REQUIRE(size24 != nullptr);

    CHECK(size16.get() != size24.get());
}

TEST_CASE("freetype_face_cache: identical size buckets share the entry") {
    if (!fixture_exists(kFontAPath)) {
        skip_if_missing(kFontAPath, "Fase 1#7 size-floats test");
        return;
    }

    FreeTypeFaceCache cache;

    auto a = cache.get_face(kFontAPath, 0, 16.0f);
    auto b = cache.get_face(kFontAPath, 0, 15.9999f);  // ceil(15.9999) = 16 → same bucket

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a.get() == b.get());
}

// ═════════════════════════════════════════════════════════════════════════
// 5. Invalid inputs short-circuit — no "size-1 fallback" leak
//    (regression guard for code-reviewer #1: max(1, ceil(0)) == 1 was wrong)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("freetype_face_cache: invalid inputs return nullptr") {
    FreeTypeFaceCache cache;

    CHECK(cache.get_face(/*resolved_path=*/"", 0, 16.0f) == nullptr);
    CHECK(cache.get_face(kFontAPath,    0, 0.0f)   == nullptr);
    CHECK(cache.get_face(kFontAPath,    0, -1.0f)  == nullptr);
}
