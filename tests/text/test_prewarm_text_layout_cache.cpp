#include <optional>
static chronon3d::TextLayoutCache s_text_cache;
// ═══════════════════════════════════════════════════════════════════════════
// test_prewarm_text_layout_cache.cpp — Success-path coverage for PR 10's
// prewarm_text_run_layout_for_frame hook (PR 11).
//
// PR 10 covered only the no-op guard rails:
//   - shape.animated_doc == null   → return false
//   - shape.engine == nullptr      → return false
// PR 11 adds coverage for the success path: shape.engine != nullptr,
// build_text_run actually runs, and the resulting layout lands in
// s_text_cache.
//
// Strategy: use the dedicated tests/fixtures/Inter-Bold.ttf font
// fixture (a copy of assets/fonts/Inter-Bold.ttf, tracked by git,
// discoverable from the repo root) so the test runs deterministically
// across CI environments without depending on installed fonts.
// Each test uses a uniquely-tagged target text to avoid colliding
// with other tests sharing the process-wide cache.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>  // build_text_run, make_initial_glyph_states
#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <doctest/doctest.h>
#include "test_text_font_fixture.hpp"

#include <memory>
#include <string>

using namespace chronon3d;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────

// inter_bold() re-exported via test_text_font_fixture.hpp

/// Build a TextRunShape holding a real Inter-Bold-backed TextRunLayout.
/// Glyph count is non-zero when assets/fonts/Inter-Bold.ttf is present;
/// tests skip per-glyph assertions when the layout is empty.
std::shared_ptr<TextRunShape> make_real_shape(
    const std::string& text,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    FontSpec font = test_text_fixture::inter_bold()
) {
    TextDocument doc;
    doc.utf8 = text;
    doc.defaults.font = font;
    doc.split_paragraphs();

    auto& cache = s_text_cache;
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
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    const FontSpec spec = test_text_fixture::inter_bold();
    REQUIRE(engine.can_load(spec));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Static (Hold) prewarm — populates the cache for one text
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: static (Hold) prewarm populates cache with active->utf8") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

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

    auto& cache = s_text_cache;
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
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

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

    auto& cache = s_text_cache;
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
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

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
    auto& cache = s_text_cache;
    const auto key = build_expected_cache_key(text, font, layout);
    CHECK(cache.contains(key));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Different frames → different cache entries (Scramble's per-frame text)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR11: Scramble prewarm at different frames writes distinct cache entries") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

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

    auto& cache = s_text_cache;
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
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

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
    auto& cache = s_text_cache;
    const auto key_dst = build_expected_cache_key(dst_text, font, layout);
    CHECK(cache.contains(key_dst));
    auto found = cache.find(key_dst);
    REQUIRE(found != nullptr);
    CHECK(found->source_text == dst_text);
}

// ═══════════════════════════════════════════════════════════════════════════
// PR 11 — DissolveLayouts per-glyph blend path tests
// ═══════════════════════════════════════════════════════════════════════════
//
// These tests exercise the per-glyph blend path that landed with PR 11.
// They verify:
//   7. Cache prewarm populates TWO entries during the gap (active side
//      AND dissolve_from side) so both layouts are cache-hot before
//      draw.
//   8. apply_active_state_to_text_run_shape populates shape.crossfade_*
//      slots when inside the gap (mix strictly in (0, 1)), and clears
//      them when outside (mix at the boundary, dissolve_from null,
//      transition != DissolveLayouts).
//   9. Fallback path: when dissolve_from == nullptr, the crossfade
//      slots stay empty regardless of transition type so the compositor
//      short-circuits the secondary tier loop.

/// Build a 2-keyframe AnimatedTextDocument that crossfades from
/// "PR11CFSrc" to "PR11CFDst" between frames 0 and 60, with the
/// DissolveLayouts transition type set on the outgoing keyframe.
static std::shared_ptr<AnimatedTextDocument> make_crossfade_doc() {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "PR11CFSrc";
    kf0.transition = SourceTextTransition::DissolveLayouts;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "PR11CFDst";
    doc->add_keyframe(kf60);
    return doc;
}

TEST_CASE("Prewarm PR11 CF: DissolveLayouts prewarm populates BOTH active and dissolve_from caches") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

    auto shape = make_real_shape("PR11CFSrc", engine, layout, font);
    REQUIRE(shape != nullptr);

    shape->animated_doc = make_crossfade_doc();
    shape->animated_doc->keyframes()[0].document.defaults.font = font;
    shape->animated_doc->keyframes()[1].document.defaults.font = font;

    // Frame 30 is in-gap: mix must be in (0, 1) and dissolve_from
    // non-null.  Prewarm must succeed AND populate cache for BOTH
    // the active side and the dissolve_from side.
    const auto state = shape->animated_doc->sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::DissolveLayouts);
    REQUIRE(state.active != nullptr);
    REQUIRE(state.dissolve_from != nullptr);
    REQUIRE(state.mix > 0.0f);
    REQUIRE(state.mix < 1.0f);

    CHECK(prewarm_text_run_layout_for_frame(*shape, Frame{30}));

    auto& cache = s_text_cache;
    const auto key_active =
        build_expected_cache_key(state.active->utf8, font, layout);
    const auto key_from =
        build_expected_cache_key(state.dissolve_from->utf8, font, layout);

    CHECK(cache.contains(key_active));
    CHECK(cache.contains(key_from));
    // Distinct cache entries (different source_text + likely different
    // font).
    CHECK(key_active.digest() != key_from.digest());

    auto found_active = cache.find(key_active);
    auto found_from = cache.find(key_from);
    REQUIRE(found_active != nullptr);
    REQUIRE(found_from != nullptr);
    CHECK(found_active->source_text == state.active->utf8);
    CHECK(found_from->source_text == state.dissolve_from->utf8);
}

TEST_CASE("Prewarm PR11 CF: apply_active_state populates crossfade_* slots inside the gap, clears outside") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

    // Pre-boundary frame 0: state.transition is Hold (we're at the
    // outgoing keyframe's frame but BEFORE the gap opens).  Actually,
    // the gap is opened by the OUTGOING keyframe's transition field.
    // Per spec, frame 0 is the start of the gap (post-boundary for the
    // previous key, opening of gap for the outgoing keyframe).
    //
    // Sample at the middle (frame 30) and at the very start of the gap
    // (frame 0) to verify:
    //   - frame 30 (midgap, mix in (0,1)) — crossfade slots populated
    //   - frame 60 (post-gap)            — crossfade slots cleared
    auto shape = make_real_shape("PR11CFSrc", engine, layout, font);
    REQUIRE(shape != nullptr);
    shape->animated_doc = make_crossfade_doc();
    shape->animated_doc->keyframes()[0].document.defaults.font = font;
    shape->animated_doc->keyframes()[1].document.defaults.font = font;

    // ── Mid-gap — slots populated ────────────────────────────────────
    {
        const auto mid_state = shape->animated_doc->sample_at(Frame{30});
        REQUIRE(mid_state.transition == SourceTextTransition::DissolveLayouts);
        REQUIRE(mid_state.dissolve_from != nullptr);
        REQUIRE(mid_state.mix > 0.0f);
        REQUIRE(mid_state.mix < 1.0f);

        CHECK(apply_active_state_to_text_run_shape(
            *shape, mid_state, engine, layout));

        REQUIRE(shape->dissolve_layout != nullptr);
        REQUIRE_FALSE(shape->dissolve_glyphs.empty());
        CHECK(shape->dissolve_layout->source_text == mid_state.dissolve_from->utf8);
        CHECK(shape->dissolve_mix > 0.0f);
        CHECK(shape->dissolve_mix < 1.0f);
    }

    // ── Post-gap — slots cleared ─────────────────────────────────────
    {
        const auto post_state = shape->animated_doc->sample_at(Frame{61});
        REQUIRE(post_state.transition == SourceTextTransition::Hold
                || post_state.active->utf8 == "PR11CFDst");

        CHECK(apply_active_state_to_text_run_shape(
            *shape, post_state, engine, layout));

        CHECK(shape->dissolve_layout == nullptr);
        CHECK(shape->dissolve_glyphs.empty());
        CHECK(shape->dissolve_mix == 0.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. DissolveLayouts fallback — Hold→Hold doc never populates crossfade slot
// ═══════════════════════════════════════════════════════════════════════════
//
// When the source keyframe's transition != DissolveLayouts (or
// dissolve_from is null for any other reason), apply_active must NOT
// populate the crossfade slot.  The compositor's `if
// (shape.dissolve_layout && !shape.dissolve_glyphs.empty())`
// short-circuit then makes the second tier loop a no-op.  We verify
// the inverse: a 2-keyframe Hold→Hold doc never produces a
// dissolve_from, and apply populates only the active side.  This
// guarantees the fallback path doesn't regress when future transition
// modes route through this code.

TEST_CASE("Prewarm PR11 CF: fallback — Hold→Hold doc never populates crossfade slot") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "PR11CFFallback_Src";
    kf0.transition = SourceTextTransition::Hold;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "PR11CFFallback_Dst";
    kf60.transition = SourceTextTransition::Hold;
    doc->add_keyframe(kf60);

    auto shape = make_real_shape("PR11CFFallback_Src", engine, layout, font);
    REQUIRE(shape != nullptr);
    shape->animated_doc = doc;
    shape->animated_doc->keyframes()[0].document.defaults.font = font;
    shape->animated_doc->keyframes()[1].document.defaults.font = font;

    // Sample at frame 30 — should be mid-Hold with dissolve_from null.
    const auto state = shape->animated_doc->sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::Hold);
    REQUIRE(state.dissolve_from == nullptr);

    apply_active_state_to_text_run_shape(*shape, state, engine, layout);

    // Crossfade slots stay empty regardless of active-side updates.
    CHECK(shape->dissolve_layout == nullptr);
    CHECK(shape->dissolve_glyphs.empty());
    CHECK(shape->dissolve_mix == 0.0f);
}

} // namespace
