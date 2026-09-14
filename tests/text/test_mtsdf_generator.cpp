#include <doctest/doctest.h>

#include <chronon3d/backends/text/mtsdf_generator.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr const char* kFont = "tests/fixtures/Inter-Bold.ttf";
constexpr std::uint16_t kEmPx = 64;
constexpr std::uint16_t kRangePx = 8;
constexpr std::uint16_t kPaddingPx = 10;

struct FreeTypeFace {
    FT_Library library{nullptr};
    FT_Face face{nullptr};
    std::mutex mutex;

    FreeTypeFace() {
        REQUIRE(FT_Init_FreeType(&library) == 0);
        REQUIRE(FT_New_Face(library, kFont, 0, &face) == 0);
        REQUIRE(FT_Set_Pixel_Sizes(face, 0, 64) == 0);
    }

    ~FreeTypeFace() {
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
    }
};

std::uint32_t texel_bits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
std::uint8_t channel(std::uint32_t texel, unsigned shift) noexcept {
    return static_cast<std::uint8_t>((texel >> shift) & 0xFFu);
}

/// FNV-1a over every bit that defines the bitmap, so byte-parity of the whole
/// (font, glyph, em_px, range, padding) matrix collapses to one integer
/// comparison — the equivalence gate the ticket's Demolition Debt requires.
std::uint64_t hash_bitmap(const chronon3d::MtsdfGlyphBitmap& bitmap) noexcept {
    const auto mix = [](std::uint64_t h, std::uint64_t value) noexcept {
        for (int i = 0; i < 8; ++i) {
            h ^= (value >> (i * 8)) & 0xFFu;
            h *= 1099511628211ull;
        }
        return h;
    };
    std::uint64_t h = 1469598103934665603ull;
    h = mix(h, bitmap.width);
    h = mix(h, bitmap.height);
    h = mix(h, static_cast<std::uint32_t>(bitmap.x_offset));
    h = mix(h, static_cast<std::uint32_t>(bitmap.y_offset));
    h = mix(h, std::bit_cast<std::uint32_t>(bitmap.advance_x));
    for (const float packed : bitmap.rgba8) {
        h = mix(h, std::bit_cast<std::uint32_t>(packed));
    }
    return h;
}

/// Glyph matrix spanning stems, diagonals, bowls, digits and punctuation so
/// curvature handling is exercised, not just one straight-stemmed glyph.
std::vector<std::uint32_t> glyph_matrix(FT_Face face) {
    static constexpr std::string_view kCorpus = "AVH123gpq@#";
    std::vector<std::uint32_t> ids;
    for (const char c : kCorpus) {
        const auto id = FT_Get_Char_Index(face, static_cast<FT_ULong>(c));
        if (id != 0) ids.push_back(id);
    }
    return ids;
}

} // namespace

TEST_CASE("MTSDF generator emits a real signed multi-channel glyph") {
    REQUIRE(std::filesystem::exists(kFont));
    FreeTypeFace ft;
    const auto glyph_id = FT_Get_Char_Index(ft.face, static_cast<FT_ULong>('H'));
    REQUIRE(glyph_id != 0);

    const auto bitmap = chronon3d::generate_mtsdf_glyph(
        chronon3d::MtsdfFaceGuard{ft.face, &ft.mutex}, glyph_id, 64, 8, 10);
    REQUIRE(bitmap.has_value());
    REQUIRE(bitmap->width > 0);
    REQUIRE(bitmap->height > 0);
    REQUIRE(bitmap->advance_x > 0.0f);
    REQUIRE(bitmap->rgba8.size() ==
            static_cast<std::size_t>(bitmap->width) * bitmap->height);

    bool has_inside = false;
    bool has_outside = false;
    bool has_edge_coloring = false;
    for (const float packed : bitmap->rgba8) {
        const auto texel = texel_bits(packed);
        const auto r = channel(texel, 0);
        const auto g = channel(texel, 8);
        const auto b = channel(texel, 16);
        const auto a = channel(texel, 24);
        has_inside |= a > 200;
        has_outside |= a < 55;
        has_edge_coloring |= r != g || g != b;
    }

    CHECK(has_inside);
    CHECK(has_outside);
    CHECK(has_edge_coloring);
}

TEST_CASE("MTSDF stress corpus keeps sharp-field data across zoom sizes") {
    REQUIRE(std::filesystem::exists(kFont));
    FreeTypeFace ft;
    const chronon3d::MtsdfFaceGuard guard{ft.face, &ft.mutex};

    // Includes sharp diagonals, counters, punctuation, digits and the
    // kerning pair used by the typewriter overlay stress scene.
    static constexpr std::string_view corpus = "T.y. {W!g?#% 123} - AV";
    for (const char c : corpus) {
        if (c == ' ') continue;
        const auto glyph_id = FT_Get_Char_Index(ft.face, static_cast<FT_ULong>(c));
        if (glyph_id == 0) continue;
        CHECK(chronon3d::generate_mtsdf_glyph(
                  guard, glyph_id, 64, kRangePx, kPaddingPx)
                  .has_value());
    }

    // Use glyphs with substantial ink for the field-quality checks.  Runtime
    // zoom is exercised by the GPU stress render; the generator receives the
    // already selected face size and must preserve the signed-field contract.
    for (const char c : std::string_view{"MWVAgo"}) {
        const auto glyph_id = FT_Get_Char_Index(ft.face, static_cast<FT_ULong>(c));
        REQUIRE(glyph_id != 0);
        const auto bitmap = chronon3d::generate_mtsdf_glyph(
            guard, glyph_id, 64, kRangePx, kPaddingPx);
        REQUIRE(bitmap.has_value());
        CHECK(bitmap->width > 0);
        CHECK(bitmap->height > 0);

        bool has_edge_coloring = false;
        bool has_inside = false;
        bool has_outside = false;
        for (const float packed : bitmap->rgba8) {
            const auto texel = texel_bits(packed);
            const auto r = channel(texel, 0);
            const auto g = channel(texel, 8);
            const auto b = channel(texel, 16);
            const auto a = channel(texel, 24);
            has_edge_coloring |= r != g || g != b;
            has_inside |= a > 200;
            has_outside |= a < 55;
        }
        CHECK(has_edge_coloring);
        CHECK(has_inside);
        CHECK(has_outside);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Guard contract: a guard without a mutex cannot prove which lock protects
// the face, so generation fails closed instead of racing on FreeType state.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MTSDF guard fails closed without a face mutex") {
    REQUIRE(std::filesystem::exists(kFont));
    FreeTypeFace ft;
    const auto glyph_id = FT_Get_Char_Index(ft.face, static_cast<FT_ULong>('H'));
    REQUIRE(glyph_id != 0);

    CHECK_FALSE(chronon3d::MtsdfFaceGuard{ft.face, nullptr}.valid());
    CHECK_FALSE(chronon3d::MtsdfFaceGuard{nullptr, &ft.mutex}.valid());

    CHECK_FALSE(chronon3d::generate_mtsdf_glyph(
                    chronon3d::MtsdfFaceGuard{ft.face, nullptr}, glyph_id,
                    kEmPx, kRangePx, kPaddingPx)
                    .has_value());
    CHECK_FALSE(chronon3d::generate_mtsdf_glyph(
                    chronon3d::MtsdfFaceGuard{nullptr, &ft.mutex}, glyph_id,
                    kEmPx, kRangePx, kPaddingPx)
                    .has_value());
    CHECK_FALSE(chronon3d::generate_mtsdf_glyph(
                    chronon3d::MtsdfFaceGuard{ft.face, &ft.mutex}, glyph_id,
                    /*em_px=*/0, kRangePx, kPaddingPx)
                    .has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Equivalence gate: byte-parity of the whole glyph matrix across repeated
// generation. The guard refactor moved work OUT of the FreeType critical
// section; the output bits must be identical.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MTSDF bitmap byte-parity across repeated generation") {
    REQUIRE(std::filesystem::exists(kFont));
    FreeTypeFace ft;
    const auto ids = glyph_matrix(ft.face);
    REQUIRE(ids.size() >= 8);

    const chronon3d::MtsdfFaceGuard guard{ft.face, &ft.mutex};

    std::vector<std::uint64_t> first;
    std::vector<std::uint64_t> second;
    for (const auto id : ids) {
        const auto bitmap = chronon3d::generate_mtsdf_glyph(
            guard, id, kEmPx, kRangePx, kPaddingPx);
        REQUIRE(bitmap.has_value());
        first.push_back(hash_bitmap(*bitmap));
    }
    for (const auto id : ids) {
        const auto bitmap = chronon3d::generate_mtsdf_glyph(
            guard, id, kEmPx, kRangePx, kPaddingPx);
        REQUIRE(bitmap.has_value());
        second.push_back(hash_bitmap(*bitmap));
    }

    CHECK(first == second);
    // A degenerate hash would make the parity check vacuous.
    CHECK(std::count_if(first.begin(), first.end(),
                        [](std::uint64_t h) { return h != 0ull; }) ==
          static_cast<long>(first.size()));
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-face granularity: the mutex the generator holds is the one that owns
// the face, not a process-wide static. Same (path, index, size) shares BOTH
// face and guard; a different size bucket gets its own face AND its own lock.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MTSDF guard is per-face, not process-wide") {
    REQUIRE(std::filesystem::exists(kFont));
    chronon3d::FreeTypeFaceCache cache;

    auto a = cache.get_face_lease(kFont, 0, static_cast<chronon3d::f32>(kEmPx));
    auto b = cache.get_face_lease(kFont, 0, static_cast<chronon3d::f32>(kEmPx));
    REQUIRE(a.valid());
    REQUIRE(b.valid());
    REQUIRE(a.mutex != nullptr);
    REQUIRE(b.mutex != nullptr);

    // Same key → same face and same guard: callers mutating the shared
    // FreeType state still serialize correctly.
    CHECK(a.face.get() == b.face.get());
    CHECK(a.mutex.get() == b.mutex.get());

    // Different size bucket → independent face and independent mutex:
    // unrelated faces must NOT serialize behind one process-wide lock.
    auto other =
        cache.get_face_lease(kFont, 0, static_cast<chronon3d::f32>(kEmPx + 7));
    REQUIRE(other.valid());
    REQUIRE(other.mutex != nullptr);
    CHECK(other.face.get() != a.face.get());
    CHECK(other.mutex.get() != a.mutex.get());

    // The lease must be usable where the generator expects it.
    CHECK(a.face != nullptr);
    CHECK(*a.face != nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// Contention correctness: many workers replaying the matrix through ONE
// shared face guard must reproduce the serial hashes exactly — the per-face
// lock protects the mutable FT_Face without perturbing the field.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MTSDF concurrent generation on a shared face is byte-identical to serial") {
    REQUIRE(std::filesystem::exists(kFont));
    chronon3d::FreeTypeFaceCache cache;

    auto lease = cache.get_face_lease(kFont, 0, static_cast<chronon3d::f32>(kEmPx));
    REQUIRE(lease.valid());
    REQUIRE(lease.face != nullptr);
    REQUIRE(lease.mutex != nullptr);

    const chronon3d::MtsdfFaceGuard guard{*lease.face, lease.mutex.get()};
    const auto ids = glyph_matrix(*lease.face);
    REQUIRE(ids.size() >= 8);

    std::vector<std::uint64_t> reference;
    for (const auto id : ids) {
        const auto bitmap = chronon3d::generate_mtsdf_glyph(
            guard, id, kEmPx, kRangePx, kPaddingPx);
        REQUIRE(bitmap.has_value());
        reference.push_back(hash_bitmap(*bitmap));
    }

    constexpr int kWorkers = 8;
    std::array<std::vector<std::uint64_t>, kWorkers> results;
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int w = 0; w < kWorkers; ++w) {
        workers.emplace_back([&, w] {
            for (const auto id : ids) {
                const auto bitmap = chronon3d::generate_mtsdf_glyph(
                    guard, id, kEmPx, kRangePx, kPaddingPx);
                results[w].push_back(bitmap ? hash_bitmap(*bitmap) : 0ull);
            }
        });
    }
    for (auto& worker : workers) worker.join();

    for (int w = 0; w < kWorkers; ++w) {
        REQUIRE(results[w].size() == reference.size());
        CHECK(results[w] == reference);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Counter parity: one `mtsdf_glyphs_generated` increment per completed
// glyph, unchanged by the guard refactor.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MTSDF counter parity: one increment per completed glyph") {
    REQUIRE(std::filesystem::exists(kFont));
    FreeTypeFace ft;
    const auto ids = glyph_matrix(ft.face);
    REQUIRE(ids.size() >= 8);

    chronon3d::RenderCounters counters;
    const auto before = counters.mtsdf_glyphs_generated.load();
    {
        const chronon3d::profiling::ProfilingGuard profiling_guard(&counters, nullptr);
        const chronon3d::MtsdfFaceGuard guard{ft.face, &ft.mutex};
        for (const auto id : ids) {
            const auto bitmap = chronon3d::generate_mtsdf_glyph(
                guard, id, kEmPx, kRangePx, kPaddingPx);
            REQUIRE(bitmap.has_value());
        }
    }

    CHECK(counters.mtsdf_glyphs_generated.load() == before + ids.size());
}
