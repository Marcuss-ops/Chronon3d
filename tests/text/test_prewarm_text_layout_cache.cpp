// ═══════════════════════════════════════════════════════════════════════════
// test_prewarm_text_layout_cache.cpp — Success-path coverage for PR 10's
// prewarm_text_run_layout_for_frame hook (PR 11).
//
// PR 10 covered only the no-op guard rails:
//   - shape.animated_doc == null   → return false
//   - shape.engine == nullptr      → return false
// PR 11 adds coverage for the success path: shape.engine != nullptr,
// build_text_run actually runs, and the resulting layout lands in
// shared_text_layout_cache().
//
// Strategy: use the dedicated tests/fixtures/Inter-Bold.ttf font
// fixture (a copy of assets/fonts/Inter-Bold.ttf, tracked by git,
// discoverable from the repo root) so the test runs deterministically
// across CI environments without depending on installed fonts.
// Each test uses a uniquely-tagged target text to avoid colliding
// with other tests sharing the process-wide cache.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>  // build_text_run, make_initial_glyph_states
#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <doctest/doctest.h>

#include <memory>
#include <string>

using namespace chronon3d;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────

/// Default font loaded by every test: Inter-Bold at 700 weight, 32px.
/// Path is asset-relative (existing project convention); tests run
/// from the repo root via CMake's WORKING_DIRECTORY setting.
FontSpec inter_bold() {
    return FontSpec{
        .font_path   = "tests/fixtures/Inter-Bold.ttf",
        .font_family = "Inter",
        .font_weight = 700,
        .font_style  = "normal",
        .font_size   = 32.0f,
    };
}

/// Build a TextRunShape holding a real Inter-Bold-backed TextRunLayout.
/// Glyph count is non-zero when assets/fonts/Inter-Bold.ttf is present;
/// tests skip per-glyph assertions when the layout is empty.
std::shared_ptr<TextRunShape> make_real_shape(
    const std::string& text,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    FontSpec font = inter_bold()
) {
    TextDocument doc;
    doc.utf8 = text;
    doc.defaults.font = font;
    doc.split_paragraphs();

    auto& cache = shared_text_layout_cache();
    auto result = build_text_run(doc, engine, layout, &cache);
    if (result.paragraphs.empty() || !result.paragraphs.front()) {
        return nullptr;  // font load failed
    }
    auto shape = std::make_shared<TextRunShape>();
    shape->layout = std::const_pointer_cast<const TextRunLayout>(
        std::const_pointer_cast<TextRunLayout>(result.paragraphs.front()));
    shape->glyphs = make_initial_glyph_states(shape->layout->placed);
    shape->engine = &engine;
    shape->layout_spec = layout;
    return shape;
}

/// Build the cached TextLayoutCacheKey that prewarm's build_text_run
/// call would compute.
///
/// IMPORTANT — coupling: this helper mirrors the single-font branch of
/// `build_cache_key()` in `src/text/text_run_builder.cpp` (anonymous
/// namespace).  Any future change to that helper's field set must
/// update this mirror too.  Consider extracting the production
/// helper into a public test-only entry point if the drift grows.
TextLayoutCacheKey build_expected_cache_key(
    const std::string& text,
    const FontSpec& font_spec,
    const TextLayoutSpec& layout
) {
    TextLayoutCacheKey key;
    key.text        = text;
    key.font_path   = font_spec.font_path;
    key.font_weight = font_spec.font_weight;
    key.font_style  = font_spec.font_style;
    key.font_size   = font_spec.font_size;
    key.tracking    = layout.tracking;
    key.box_width   = layout.box.x;
    key.wrap        = layout.wrap;
    key.direction   = TextDirection::Auto;
    key.language.clear();
    key.paragraph   = layout.paragraph;
    return key;
}

/// Build a 2-keyframe AnimatedTextDocument that produces a static
/// (Hold) state at every frame between 0 and 60.
std::shared_ptr<AnimatedTextDocument> make_static_doc(const std::string& utf8) {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf;
    kf.frame = Frame{0};
    kf.document.utf8 = utf8;
    doc->add_keyframe(kf);
    return doc;
}

/// Build a 2-keyframe AnimatedTextDocument that scrambles from
/// "PR11ScrambleSrc" to "PR11ScrambleDst" between frames 0 and 60.
/// The Scramble transition is set on kf0 (outgoing) per the
/// convention documented on SourceTextKeyframe::transition.
std::shared_ptr<AnimatedTextDocument> make_scramble_doc() {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "PR11ScrambleSrc";
    kf0.transition = SourceTextTransition::Scramble;
    kf0.scramble_params.seed = 9999;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "PR11ScrambleDst";
    doc->add_keyframe(kf60);
    return doc;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Font fixture reachability — sanity check
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: tests/fixtures/Inter-Bold.ttf fixture is loadable") {
    FontEngine engine;
    const FontSpec spec = inter_bold();
    REQUIRE(engine.can_load(spec));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Static (Hold) prewarm — populates the cache for one text
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: static (Hold) prewarm populates cache with active->utf8") {
    FontEngine engine;
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = inter_bold();

    const std::string held_text = "PR11StaticHold_v1";
    auto shape = make_real_shape(held_text, engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);

    shape->animated_doc = make_static_doc(held_text);

    // Hash the same active->utf8 on the doc's defaults font so the
    // prewarm's TD matches the cache key shape.
    shape->animated_doc->keyframes()[0].document.defaults.font = font;

    const Frame frame{30};
    const bool ok = prewarm_text_run_layout_for_frame(*shape, frame);
    REQUIRE(ok);

    auto& cache = shared_text_layout_cache();
    const auto key = build_expected_cache_key(held_text, font, layout);
    CHECK(cache.contains(key));
    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->source_text == held_text);
    CHECK(found->placed.glyphs.size() > 0);  // shape actually produced glyphs
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Scramble prewarm — populates cache with transition_text, not active->utf8
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: Scramble prewarm populates cache with transition_text bytes") {
    FontEngine engine;
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = inter_bold();

    auto shape = make_real_shape("PR11ScrambleSrc", engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);

    shape->animated_doc = make_scramble_doc();
    // Force the layout-side font to match so the post-apply cache key
    // matches what we built by hand below.
    shape->animated_doc->keyframes()[0].document.defaults.font = font;
    shape->animated_doc->keyframes()[1].document.defaults.font = font;

    const Frame frame{30};
    auto state = shape->animated_doc->sample_at(frame);
    REQUIRE(state.transition == SourceTextTransition::Scramble);
    REQUIRE_FALSE(state.transition_text.empty());

    const bool ok = prewarm_text_run_layout_for_frame(*shape, frame);
    REQUIRE(ok);

    auto& cache = shared_text_layout_cache();
    const auto key = build_expected_cache_key(state.transition_text, font, layout);
    CHECK(cache.contains(key));
    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->source_text == state.transition_text);

    // Sanity: the cache key we used to look up is distinct from the
    // active->utf8 key, so this proves the entry was stored under
    // the transition_text bytes — not the active doc's.
    const auto active_key = build_expected_cache_key(
        state.active->utf8, font, layout);
    CHECK(key.digest() != active_key.digest());
    CHECK(found->source_text != state.active->utf8);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Idempotency — two consecutive prewarms for the same frame don't crash
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: prewarming the same frame twice is safe (idempotent)") {
    FontEngine engine;
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = inter_bold();

    const std::string text = "PR11Idempotent_v1";
    auto shape = make_real_shape(text, engine, layout, font);
    REQUIRE(shape != nullptr);

    shape->animated_doc = make_static_doc(text);
    shape->animated_doc->keyframes()[0].document.defaults.font = font;

    const Frame frame{30};
    CHECK(prewarm_text_run_layout_for_frame(*shape, frame));
    CHECK(prewarm_text_run_layout_for_frame(*shape, frame));
    CHECK(prewarm_text_run_layout_for_frame(*shape, frame));

    // Cache still contains the entry (LruCache::put re-promotes on
    // overwrite; size unchanged but contains() is true).
    auto& cache = shared_text_layout_cache();
    const auto key = build_expected_cache_key(text, font, layout);
    CHECK(cache.contains(key));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Different frames → different cache entries (Scramble's per-frame text)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: Scramble prewarm at different frames writes distinct cache entries") {
    FontEngine engine;
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = inter_bold();

    auto shape = make_real_shape("PR11ScrambleSrc", engine, layout, font);
    REQUIRE(shape != nullptr);

    shape->animated_doc = make_scramble_doc();
    shape->animated_doc->keyframes()[0].document.defaults.font = font;
    shape->animated_doc->keyframes()[1].document.defaults.font = font;

    // Prewarm at frame 30 and frame 31 — the Scramble produces
    // different transition_text bytes per frame, so the cache MUST
    // contain TWO entries (with distinct source_text).
    CHECK(prewarm_text_run_layout_for_frame(*shape, Frame{30}));
    CHECK(prewarm_text_run_layout_for_frame(*shape, Frame{31}));

    const auto s30 = shape->animated_doc->sample_at(Frame{30}).transition_text;
    const auto s31 = shape->animated_doc->sample_at(Frame{31}).transition_text;
    REQUIRE_FALSE(s30.empty());
    REQUIRE_FALSE(s31.empty());
    // Determinism: scrambling bytes differ per frame.
    REQUIRE(s30 != s31);

    auto& cache = shared_text_layout_cache();
    const auto key30 = build_expected_cache_key(s30, font, layout);
    const auto key31 = build_expected_cache_key(s31, font, layout);
    REQUIRE(key30.digest() != key31.digest());

    CHECK(cache.contains(key30));
    CHECK(cache.contains(key31));
    auto f30 = cache.find(key30); REQUIRE(f30 != nullptr);
    auto f31 = cache.find(key31); REQUIRE(f31 != nullptr);
    CHECK(f30->source_text == s30);
    CHECK(f31->source_text == s31);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Settled tail — prewarm at frame 120 (post-boundary) caches active->utf8
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: post-boundary prewarm caches active->utf8 (not transition_text)") {
    FontEngine engine;
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = inter_bold();

    // Static doc — at every sample_at, transition_text is empty and
    // active->utf8 is the held text.  prewarm must buffer the entry.
    const std::string text = "PR11Settled_v1";
    auto shape = make_real_shape(text, engine, layout, font);
    REQUIRE(shape != nullptr);

    shape->animated_doc = make_static_doc(text);
    shape->animated_doc->keyframes()[0].document.defaults.font = font;

    const std::string dst_text = "PR11SettledDst_v1";
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = dst_text;
    kf60.document.defaults.font = font;
    kf60.transition = SourceTextTransition::Cut;
    shape->animated_doc->add_keyframe(kf60);

    // Sample at frame 90 (post-Cut).  The active becomes the
    // destination; transition_text empty for settled tail.
    const auto state = shape->animated_doc->sample_at(Frame{90});
    REQUIRE(state.transition == SourceTextTransition::Hold);
    REQUIRE(state.active != nullptr);
    REQUIRE(state.active->utf8 == dst_text);
    REQUIRE(state.transition_text.empty());

    CHECK(prewarm_text_run_layout_for_frame(*shape, Frame{90}));

    // Cache should hold an entry keyed on dst_text, NOT on the
    // pre-boundary text or transition_text (which is empty here).
    auto& cache = shared_text_layout_cache();
    const auto key_dst = build_expected_cache_key(dst_text, font, layout);
    CHECK(cache.contains(key_dst));
    auto found = cache.find(key_dst);
    REQUIRE(found != nullptr);
    CHECK(found->source_text == dst_text);
}
