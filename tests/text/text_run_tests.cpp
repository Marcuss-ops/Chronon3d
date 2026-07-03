static chronon3d::TextLayoutCache s_text_cache;
#include <doctest/doctest.h>

#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/glyph_selector.hpp>
using namespace chronon3d;


namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

/// Build a minimal PlacedGlyphRun with `count` glyphs at fixed width.
PlacedGlyphRun make_placed_run_tr(size_t count) {
    PlacedGlyphRun pr;
    pr.font_size = 32.0f;
    pr.total_width = static_cast<float>(count) * 12.0f;
    pr.total_height = 40.0f;
    pr.ascent = 28.0f;
    pr.descent = 12.0f;
    pr.baseline = 28.0f;

    for (size_t i = 0; i < count; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(100 + i);
        g.x = static_cast<float>(i) * 12.0f;
        g.y = 0.0f;
        g.advance_x = 12.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        pr.glyphs.push_back(g);

        PlacedGlyphRun::Cluster c;
        c.start_glyph = i;
        c.end_glyph = i + 1;
        c.byte_offset = i;
        c.byte_len = 1;
        c.advance = 12.0f;
        c.raw_advance = 10.0f;
        pr.clusters.push_back(c);
    }

    return pr;
}

/// Build a sample TextRunLayout (non-const for modification in tests).
std::shared_ptr<TextRunLayout> make_test_layout_tr(const std::string& text = "ABC") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->source_text = text;
    layout->font = FontSpec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    layout->font_size = 32.0f;
    layout->placed = make_placed_run_tr(text.size());
    layout->units = build_text_unit_map(layout->placed, text);
    layout->bounds = {layout->placed.total_width, layout->placed.total_height};
    layout->line_height = 38.4f;
    layout->tracking = 0.0f;
    layout->wrap = TextWrap::None;
    layout->direction = TextDirection::LTR;
    return layout;
}

/// Build glyph states from a placed run.
std::vector<GlyphInstanceState> make_glyph_states_tr(const PlacedGlyphRun& placed) {
    std::vector<GlyphInstanceState> states;
    for (size_t i = 0; i < placed.glyphs.size(); ++i) {
        const auto& g = placed.glyphs[i];
        GlyphInstanceState s;
        s.glyph_id = g.glyph_id;
        s.layout_position = {g.x, g.y};
        s.opacity = 1.0f;
        s.fill = {1.0f, 1.0f, 1.0f, 1.0f};
        states.push_back(s);
    }
    return states;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TextRunLayout tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunLayout: basic construction") {
    auto layout = make_test_layout_tr("Hello");
    CHECK(layout->source_text == "Hello");
    CHECK(layout->font.font_path == "assets/fonts/Inter-Regular.ttf");
    CHECK(layout->font_size == 32.0f);
    CHECK(layout->placed.glyphs.size() == 5);
    CHECK(layout->units.grapheme_count == 5);
    CHECK(layout->bounds.x == layout->placed.total_width);
    CHECK(layout->line_height == 38.4f);
}

TEST_CASE("TextRunLayout: layout_hash is stable") {
    auto a = make_test_layout_tr("Test");
    auto b = make_test_layout_tr("Test");
    CHECK(a->layout_hash() == b->layout_hash());
}

TEST_CASE("TextRunLayout: layout_hash changes with different text") {
    auto a = make_test_layout_tr("Hello");
    auto b = make_test_layout_tr("World");
    CHECK(a->layout_hash() != b->layout_hash());
}

TEST_CASE("TextRunLayout: layout_hash changes with different font") {
    auto layout = make_test_layout_tr("Test");
    auto b = make_test_layout_tr("Test");
    b->font.font_weight = 700;
    CHECK(layout->layout_hash() != b->layout_hash());
}

TEST_CASE("TextRunLayout: shaping_hash excludes source text") {
    auto a = make_test_layout_tr("Hello");
    auto b = make_test_layout_tr("World");
    CHECK(a->shaping_hash() == b->shaping_hash());
}

TEST_CASE("TextRunLayout: shaping_hash changes with font_size") {
    auto a = make_test_layout_tr("Test");
    auto b = make_test_layout_tr("Test");
    b->font_size = 64.0f;
    CHECK(a->shaping_hash() != b->shaping_hash());
}

TEST_CASE("TextRunLayout: shaping_hash changes with wrap mode") {
    auto a = make_test_layout_tr("Test");
    auto b = make_test_layout_tr("Test");
    b->wrap = TextWrap::Word;
    CHECK(a->shaping_hash() != b->shaping_hash());
}

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCacheKey tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutCacheKey: same values → same digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    CHECK(a.digest() == b.digest());
    CHECK(a == b);
}

TEST_CASE("TextLayoutCacheKey: different text → different digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"World", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    CHECK(a.digest() != b.digest());
}

TEST_CASE("TextLayoutCacheKey: different font_size → different digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"Hello", "font.ttf", 400, "normal", 64.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    CHECK(a.digest() != b.digest());
}

TEST_CASE("TextLayoutCacheKey: different tracking → different digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"Hello", "font.ttf", 400, "normal", 32.0f, 2.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    CHECK(a.digest() != b.digest());
}

TEST_CASE("TextLayoutCacheKey: different wrap → different digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::Word, TextDirection::LTR, "en"};
    CHECK(a.digest() != b.digest());
}

TEST_CASE("TextLayoutCacheKey: different box_width → different digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 200.0f, TextWrap::Word, TextDirection::LTR, "en"};
    CHECK(a.digest() != b.digest());
}

TEST_CASE("TextLayoutCacheKey: different direction → different digest") {
    TextLayoutCacheKey a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey b{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::RTL, "en"};
    CHECK(a.digest() != b.digest());
}

TEST_CASE("TextLayoutCacheKeyHash: produces valid hash") {
    TextLayoutCacheKeyHash hasher;
    TextLayoutCacheKey key{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    size_t h = hasher(key);
    CHECK(h != 0);
    TextLayoutCacheKey key2{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    CHECK(hasher(key2) == h);
}

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCache tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutCache: find returns nullptr for missing key") {
    TextLayoutCache cache(1024 * 1024);
    TextLayoutCacheKey key{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    CHECK(cache.find(key) == nullptr);
    CHECK(!cache.contains(key));
}

TEST_CASE("TextLayoutCache: store and find round-trip") {
    TextLayoutCache cache(1024 * 1024);
    TextLayoutCacheKey key{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    auto layout = make_test_layout_tr("Hello");

    cache.store(key, layout);
    CHECK(cache.contains(key));

    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->source_text == "Hello");
    CHECK(found == layout);
}

TEST_CASE("TextLayoutCache: different keys are independent") {
    TextLayoutCache cache(1024 * 1024);
    TextLayoutCacheKey key_a{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    TextLayoutCacheKey key_b{"World", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};

    cache.store(key_a, make_test_layout_tr("Hello"));
    cache.store(key_b, make_test_layout_tr("World"));

    auto found_a = cache.find(key_a);
    auto found_b = cache.find(key_b);
    REQUIRE(found_a != nullptr);
    REQUIRE(found_b != nullptr);
    CHECK(found_a->source_text == "Hello");
    CHECK(found_b->source_text == "World");
}

TEST_CASE("TextLayoutCache: erase removes entry") {
    TextLayoutCache cache(1024 * 1024);
    TextLayoutCacheKey key{"Hello", "font.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, "en"};
    cache.store(key, make_test_layout_tr("Hello"));
    CHECK(cache.contains(key));

    CHECK(cache.erase(key));
    CHECK(!cache.contains(key));
    CHECK(cache.find(key) == nullptr);
}

TEST_CASE("TextLayoutCache: clear removes all entries") {
    TextLayoutCache cache(1024 * 1024);
    TextLayoutCacheKey a{"A", "f.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, ""};
    TextLayoutCacheKey b{"B", "f.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, ""};
    cache.store(a, make_test_layout_tr("A"));
    cache.store(b, make_test_layout_tr("B"));
    CHECK(cache.size() == 2);

    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(!cache.contains(a));
    CHECK(!cache.contains(b));
}

TEST_CASE("TextLayoutCache: store overwrites existing key") {
    TextLayoutCache cache(1024 * 1024);
    TextLayoutCacheKey key{"Test", "f.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, ""};

    auto v1 = make_test_layout_tr("Test");
    auto v2 = make_test_layout_tr("Test");
    v2->tracking = 5.0f;

    cache.store(key, v1);
    cache.store(key, v2);

    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->tracking == 5.0f);
}

TEST_CASE("TextLayoutCache: thread-safe singleton") {
    auto& cache = s_text_cache;
    TextLayoutCacheKey key{"Singleton", "f.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, ""};
    cache.store(key, make_test_layout_tr("Singleton"));

    auto found = cache.find(key);
    CHECK(found != nullptr);

    cache.erase(key);
    CHECK(!cache.contains(key));
}

TEST_CASE("TextLayoutCache: reset clears singleton") {
    auto& cache = s_text_cache;
    TextLayoutCacheKey key{"Reset", "f.ttf", 400, "normal", 32.0f, 0.0f, 0.0f, TextWrap::None, TextDirection::LTR, ""};
    cache.store(key, make_test_layout_tr("Reset"));
    CHECK(cache.contains(key));

    reset_s_text_cache;
    CHECK(!cache.contains(key));
}

// ═══════════════════════════════════════════════════════════════════════════
// TextRunShape tests (standalone, not embedded in Shape)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunShape: basic construction") {
    TextRunShape shape;
    shape.layout = make_test_layout_tr("XYZ");
    shape.glyphs = make_glyph_states_tr(shape.layout->placed);

    CHECK(shape.layout != nullptr);
    CHECK(shape.glyphs.size() == 3);
    CHECK(shape.glyphs[0].glyph_id == 100);
}

TEST_CASE("TextRunShape: hash_text_run_shape stable for same state") {
    TextRunShape a;
    a.layout = make_test_layout_tr("ABC");
    a.glyphs = make_glyph_states_tr(a.layout->placed);
    a.material = TextMaterial::premium();
    a.paint = TextPaint{};

    TextRunShape b;
    b.layout = make_test_layout_tr("ABC");
    b.glyphs = make_glyph_states_tr(b.layout->placed);
    b.material = TextMaterial::premium();
    b.paint = TextPaint{};

    CHECK(hash_text_run_shape(a) == hash_text_run_shape(b));
}

TEST_CASE("TextRunShape: hash changes with different layout") {
    TextRunShape a;
    a.layout = make_test_layout_tr("ABC");
    a.glyphs = make_glyph_states_tr(a.layout->placed);

    TextRunShape b;
    b.layout = make_test_layout_tr("XYZ");
    b.glyphs = make_glyph_states_tr(b.layout->placed);

    CHECK(hash_text_run_shape(a) != hash_text_run_shape(b));
}

TEST_CASE("TextRunShape: hash changes with different glyph state") {
    TextRunShape a;
    a.layout = make_test_layout_tr("ABC");
    a.glyphs = make_glyph_states_tr(a.layout->placed);

    TextRunShape b;
    b.layout = make_test_layout_tr("ABC");
    b.glyphs = make_glyph_states_tr(b.layout->placed);
    b.glyphs[0].opacity = 0.5f;

    CHECK(hash_text_run_shape(a) != hash_text_run_shape(b));
}

TEST_CASE("TextRunShape: hash changes with different material") {
    TextRunShape a;
    a.layout = make_test_layout_tr("ABC");
    a.glyphs = make_glyph_states_tr(a.layout->placed);
    a.material = TextMaterial::premium();

    TextRunShape b;
    b.layout = make_test_layout_tr("ABC");
    b.glyphs = make_glyph_states_tr(b.layout->placed);
    b.material = TextMaterial::neon();

    CHECK(hash_text_run_shape(a) != hash_text_run_shape(b));
}

TEST_CASE("TextRunShape: hash changes with different paint") {
    TextRunShape a;
    a.layout = make_test_layout_tr("ABC");
    a.glyphs = make_glyph_states_tr(a.layout->placed);
    a.paint.fill = Color{1.0f, 0.0f, 0.0f, 1.0f};

    TextRunShape b;
    b.layout = make_test_layout_tr("ABC");
    b.glyphs = make_glyph_states_tr(b.layout->placed);
    b.paint.fill = Color{0.0f, 0.0f, 1.0f, 1.0f};

    CHECK(hash_text_run_shape(a) != hash_text_run_shape(b));
}

TEST_CASE("TextRunShape: hash with null layout") {
    TextRunShape a;
    a.layout = nullptr;
    a.glyphs = {};

    TextRunShape b;
    b.layout = nullptr;
    b.glyphs = {};

    CHECK(hash_text_run_shape(a) == hash_text_run_shape(b));
}
