#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
// TICKET-100 — route the legacy materialize_text_run_shape pipeline through
// compile_text_layout.  Single canonical TextRunLayout compiler lives in
// this directory (text_run_builder.cpp implementation TU); we include it here
// so the materializer
// can delegate to it instead of duplicating cache/shape/place/build/store
// inline.  TextDocument + split_paragraphs are also required to build
// the per-shape document compile_text_layout consumes.
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/text_document.hpp>
// TICKET-104 -- internal consumed-decrement helper mirrors the include
// pattern used by the LayerBuilder implementation TU (this file's
// predecessor, the historical layer_builder.cpp).  Relative path
// from src/scene/builders/ to src/text/ = "../../text/...".
#include "../../text/pending_text_run_impl.hpp"
#include "../../text/prepared_text_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <utility>
#include <spdlog/spdlog.h>

namespace chronon3d {

namespace {

// Keep font fallback resolution anchored to the authored primary font.  The
// canonical TextDefinition path used to leave bundled_fonts_root empty, which
// silently degraded fallback to primary-only even when the repository font
// bundle was present.  A sibling directory is the narrowest deterministic
// fallback scope and also works for absolute SDK asset paths.
[[nodiscard]] std::filesystem::path bundled_font_root_for(const FontSpec& font) {
    if (font.font_path.empty()) {
        return {};
    }
    const auto parent = std::filesystem::path{font.font_path}.parent_path();
    return parent.empty() ? std::filesystem::path{"."} : parent;
}

} // namespace

// ── Private ctor ────────────────────────────────────────────────────────

TextRunBuilder::TextRunBuilder(LayerBuilder* parent, PendingTextRun* spec)
    : m_parent(parent), m_spec(spec) {}

// ── Internal helpers ────────────────────────────────────────────────────

GlyphSelectorSpec TextRunBuilder::make_global_glyph_selector(std::string id) {
    GlyphSelectorSpec sel;
    sel.id = std::move(id);
    sel.unit = TextSelectorUnit::Glyph;
    sel.shape = TextSelectorShape::Square;
    sel.start = {0.0f};
    sel.end = {100.0f};
    sel.amount = {100.0f};
    sel.exclude_spaces = false; // include all glyphs including whitespace
    return sel;
}

void TextRunBuilder::append_animator(TextAnimatorSpec spec) {
    m_spec->params.animation.animators.push_back(std::move(spec));
}

// ── Per-glyph mutators (inject implicit animator) ───────────────────────

TextRunBuilder& TextRunBuilder::position(Vec3 v) {
    TextAnimatorSpec spec;
    spec.id = "__trb_position_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { PositionProperty{v} };
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::opacity(f32 v) {
    TextAnimatorSpec spec;
    spec.id = "__trb_opacity_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { OpacityProperty{v} };
    // Opacity's most natural blend semantics is Multiply (anim value is
    // multiplied against the base 1.0) so chained transforms compose.
    spec.transform_mode = TextPropertyBlendMode::Multiply;
    spec.color_mode = TextPropertyBlendMode::Multiply;
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::anchor(Vec3 a) {
    TextAnimatorSpec spec;
    spec.id = "__trb_anchor_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { AnchorProperty{a} };
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::rotate(Vec3 euler_deg) {
    TextAnimatorSpec spec;
    spec.id = "__trb_rotate_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { RotationProperty{euler_deg} };
    spec.transform_mode = TextPropertyBlendMode::Add;
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::scale(Vec3 s) {
    TextAnimatorSpec spec;
    spec.id = "__trb_scale_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { ScaleProperty{s} };
    spec.transform_mode = TextPropertyBlendMode::Multiply;
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::font_size(f32 v) {
    // Font size lives inside the composable TextDefaults under
    // .text().font.font_size — it affects HarfBuzz shaping upstream of
    // any glyph-level animation, so this mutator updates the BASE
    // parameter (no animator injection).
    //
    // FIX #3 — Do NOT disable caching here.  The cache key already
    // includes font_size (cache_key.font_size = font_spec.font_size),
    // so a different font_size produces a different key and naturally
    // invalidates the cache.  Previously, setting font_size forced
    // cache_layout=false, which permanently disabled cache lookups
    // for that text run — causing the observed 27% hit rate (7 hits
    // / 19 misses) because virtually every composition calls .font_size().
    // This setter is called at BUILD TIME (once); animation goes through
    // the animator stack, not through this mutator.
    m_spec->params.style.font.font_size = v;
    return *this;
}

TextRunBuilder& TextRunBuilder::font(std::string path) {
    // Font path shorthand — sets the composable TextDefaults's font_path
    // directly.  This triggers font resolution at materialization time.
    m_spec->params.style.font.font_path = std::move(path);
    return *this;
}

TextRunBuilder& TextRunBuilder::blur(f32 radius) {
    TextAnimatorSpec spec;
    spec.id = "__trb_blur_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { BlurProperty{radius} };
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::tracking(f32 px) {
    TextAnimatorSpec spec;
    spec.id = "__trb_tracking_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { TrackingProperty{px} };
    append_animator(std::move(spec));
    return *this;
}

TextRunBuilder& TextRunBuilder::baseline_shift(f32 px) {
    TextAnimatorSpec spec;
    spec.id = "__trb_baseline_" + std::to_string(m_implicit_id_seq++);
    spec.enabled = true;
    spec.selectors = { make_global_glyph_selector(spec.id + "_sel") };
    spec.properties = { BaselineShiftProperty{px} };
    append_animator(std::move(spec));
    return *this;
}

// ── Explicit user-supplied animators / selectors ────────────────────────

TextRunBuilder& TextRunBuilder::animator(TextAnimatorSpec spec) {
    // Prepend any pending selectors accumulated via `.selector()` calls BEFORE
    // this `.animator()` call.  This ensures `.selector(s).animator(a)` chains
    // correctly: s controls a, not a phantom placeholder.
    if (!m_pending_selectors.empty()) {
        spec.selectors.insert(
            spec.selectors.begin(),
            std::make_move_iterator(m_pending_selectors.begin()),
            std::make_move_iterator(m_pending_selectors.end()));
        m_pending_selectors.clear();
    }
    append_animator(std::move(spec));
    // PR 3 — record anchor for selector-after-animator binding.
    // Captured AFTER the push_back so the index is correct (push_back may
    // reallocate; indexing into the up-to-date size is safe in the very
    // next selector() call because nothing else mutates the vector size
    // in between).
    m_last_explicit_animator_idx = m_spec->params.animation.animators.size() - 1;
    m_has_explicit_animator = true;
    return *this;
}

TextRunBuilder& TextRunBuilder::selector(GlyphSelectorSpec spec) {
    // PR 3 selector binding rules (two-way dichotomy):
    //  A. Selector-after-explicit-animator: if `.selector(s)` follows an
    //     explicit `.animator(a)` (pending drained OR never filled) → append
    //     `s` directly to that anchor animator's selector list.  No phantom
    //     animator entry is created.  Covers `.animator(a).selector(s)` and
    //     `.selector(s).animator(a).selector(t).animator(b)` (the second
    //     selector binds to b).
    //  B. Selector-first / standalone: otherwise (no recent explicit
    //     `.animator(...)`) → queue into `m_pending_selectors` so the
    //     upcoming `.animator(a)` can drain it and prepend to `a.selectors`.
    //     Covers `.selector(s).animator(a)` (selector-first) and standalone
    //     `.selector(s)` (waits forever if no `.animator(...)` follows;
    //     pending selectors are silently dropped at materialization time
    //     when the chain ends without an explicit animator, which is the
    //     documented "no animator, no binding" outcome).
    if (m_pending_selectors.empty() && m_has_explicit_animator) {
        // Case A: append to the most recent explicit animator's selector list.
        // `splitors[i]` never grows beyond what's inserted (selectors are the
        // new ones from `.selector(...)` calls); safe to push_back here.
        m_spec->params.animation.animators[m_last_explicit_animator_idx]
            .selectors.push_back(std::move(spec));
    } else {
        // Case B: queue for an upcoming `.animator(...)`.
        m_pending_selectors.push_back(std::move(spec));
    }
    return *this;
}

// ── Meta ──

TextRunBuilder& TextRunBuilder::font_engine(FontEngine* engine) {
    // Per-spec override lives on PendingTextRun so callers can read it
    // back via build_spec().  Priority at materialization:
    //   1. PendingTextRun.font_engine (per-spec, set here)
    //   2. LayerBuilder.m_font_engine  (per-layer default)
    //   3. resolve_engine() fallback   (F1.D: process-wide FontEngine + AssetResolver)
    m_spec->font_engine = engine;
    return *this;
}

TextRunBuilder& TextRunBuilder::cache_layout(bool value) {
    m_cache_layout = value;
    // PR 2: sync to spec immediately so observers / auto-build can read
    // the value without an explicit `.commit()` call.
    m_spec->params.animation.cache_layout = value;
    return *this;
}

TextRunBuilder& TextRunBuilder::name(std::string n) {
    m_spec->name = std::move(n);
    return *this;
}

// ── PR 9 — AnimatedTextDocument attachment ──────────────────────────────
//
// The pending entry's `animated_doc` slot is set so the materializer
// picks it up at LayerBuilder::build() / RenderNodeFactory::text_run()
// time.  We DON'T sample the document here — sampling needs the layer's
// current SampleTime, which is only known at materialization (and the
// compositor may rebuild with a different time later).

TextRunBuilder& TextRunBuilder::from_animated_document(
    std::shared_ptr<const AnimatedTextDocument> doc
) {
    m_spec->animated_doc = std::move(doc);
    return *this;
}

// ── Commit ──
//
// For now, mutators directly mutate `m_spec.params` so there's nothing
// extra to commit.  We just hand back the parent LayerBuilder for
// re-entry into layer-level chaining.  If we ever introduce a deferred
// optimization (e.g. binary packing of the animators), `commit()` is
// the right place to do it.

LayerBuilder& TextRunBuilder::commit() {
    assert(m_parent != nullptr && "TextRunBuilder commit() called with null parent");

    // TICKET-104 — selector/animator chain validation.  If a selector
    // spec was queued via `.selector(...)` WITHOUT a preceding
    // `.animator(...)`, the chain is semantically incomplete: the
    // selector would have no animator to live on.  Drop the orphaned
    // selectors here + emit a one-shot `spdlog::warn` diagnostic that
    // log-scrapers can lock against.  This matches the existing
    // `build_text_run` skip+warn pattern — both are cat-3-compliant
    // structural diagnostics (no new public classes, no new Result
    // types).  The structural outcome (animators vector remains
    // empty, the orphaned selectors are dropped) is testable
    // directly.  Two failure modes are intentionally identical from
    // the caller's perspective: (a) `.selector(...) .commit()` w/o
    // `.animator(...)`  (this branch), and (b) build-cache-miss on an
    // unsupported multi-font shape (build_text_run) — both emit warn
    // + drop the dangling sub-spec rather than fail noisily.
    if (!m_pending_selectors.empty() && !m_has_explicit_animator) {
        spdlog::warn(
            "TextRunBuilder::commit: {} selector spec(s) dropped — "
            "no .animator(...) call registered before .commit().  "
            "Selectors require an animator host (call .animator(spec) "
            "before .selector(spec) or use .commit() only after wiring "
            "at least one animator).  See TICKET-104.",
            m_pending_selectors.size());
        m_pending_selectors.clear();
    }

    // LayerBuilder::build() reads m_text_runs directly; the spec is
    // already up-to-date.  Touching m_cache_layout=false here forces
    // a re-shape even if the layout cache already contains an entry
    // for the spec's TextRunDefinition (because user edits may have
    // changed shaping inputs).
    m_spec->params.animation.cache_layout = m_cache_layout;

    // TICKET-104 — consumed-flag lifecycle: commit() finalizes ONLY the
    // animator/selector stack onto the spec.  The `consumed` flag MUST
    // NOT be set here — doing so would cause LayerBuilder::build() to
    // skip the spec (via `if (spec.consumed) continue;`) and the text
    // node would never be materialized.  `mark_consumed` is called
    // exclusively in LayerBuilder::build() AFTER the RenderNode is
    // created (see the matching call site in that method).
    //
    //   ❌ Previous (buggy): chronon3d::text_internal::mark_consumed(*m_spec);
    //   ✅ Correct:         defer consumed-flag to build() post-node-creation.

    return *m_parent;
}

// ═══════════════════════════════════════════════════════════════════════════
// materialize_text_run_shape — shared helper
//
// Reads the composable nested TextRunDefinition fields after the PR3→PR4
// migration (TextRunDefinition is now an alias of TextRunDefinition — see
// builder_params.hpp).
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_USE_BLEND2D

namespace text_run_materialize_detail {

/// F1.D → Audit §10 — FontEngine Automatico: process-wide fallback.
///
/// Returns `preferred` if non-null.  When null (CLI still render, precomp
/// nodes, text audit, or any path without a SoftwareRenderer), falls back
/// to a process-wide FontEngine backed by a DEFAULT-CONSTRUCTED
/// AssetResolver (intentionally UN-mounted — the historical
/// implicit-CWD mounting was REMOVED per audit §10).
///
/// Behaviour after the unmount:
///   - Absolute font paths (e.g. "/usr/share/fonts/Inter.ttf") still
///     resolve via system-font fallback.
///   - Relative paths (e.g. "assets/fonts/Inter.ttf") intentionally
///     do NOT resolve — callers must wire an explicit FontEngine via
///     `PendingTextRun.font_engine`, `LayerBuilder::m_font_engine`, or
///     `sdk::RenderEngine::set_assets_root(path)` to make relative
///     resolution work.  This is the desired hard-fail behaviour per
///     audit §10 ("fallback CWD" delisting).
///   - The process-wide fallback remains as a SAFETY NET for absolute
///     paths + system fonts so pre-existing convenience compositions
///     that don't wire a font engine don't immediately regress on
///     shapes that don't depend on `assets/fonts/Inter.ttf`.
///
/// Cat-3 minimal-surface: the resolver remains a per-TU function-local
/// static (no new public symbols introduced).  The previous `mount(
/// implicit-CWD wiring becomes a no-op mount-then-unmount dance
/// removed entirely per audit §10.
///
/// Thread safety: the C++11 magic-statics rule guarantees the
/// initialisation runs exactly once across threads on first call.
/// Subsequent calls keep returning the same `&s_fallback_engine`.
[[nodiscard]] FontEngine* resolve_engine(FontEngine* preferred) {
    if (preferred) return preferred;

    static assets::AssetResolver s_fallback_resolver;  // intentionally UN-mounted (audit §10)
    static FontEngine s_fallback_engine(s_fallback_resolver);

    // One-shot warning: log once per process lifetime to avoid spamming
    // on every text-run materialization in a composition without an
    // explicit FontEngine.
    static bool s_warned = false;
    if (!s_warned) {
        s_warned = true;
        spdlog::warn(
            "resolve_engine: no FontEngine provided — using process-wide "
            "fallback (UN-MOUNTED resolver; only absolute paths + system "
            "fonts resolve). "
            "Wire a FontEngine* via PendingTextRun.font_engine / "
            "LayerBuilder::m_font_engine, or call "
            "engine.set_assets_root(path), to enable relative-path "
            "asset resolution (audit §10: process-wide asset root ripout).");
    }
    return &s_fallback_engine;
}

} // namespace text_run_materialize_detail

std::shared_ptr<TextRunShape> materialize_text_run_shape(
    const PreparedText& prepared,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc
) {
    return materialize_prepared_text(
        text_internal::normalize_prepared_text(prepared),
        engine,
        sample_time,
        std::move(animated_doc));
}

std::shared_ptr<TextRunShape> materialize_prepared_text(
    const PreparedText& prepared,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc
) {
    using namespace text_run_materialize_detail;

    const PreparedText normalized = text_internal::normalize_prepared_text(prepared);
    const std::string& text = normalized.document.utf8;

    // Early-out for empty / whitespace-only input (mirror the legacy path).
    const bool only_whitespace =
        text.empty() ||
        std::all_of(text.begin(), text.end(),
            [](unsigned char c) { return std::isspace(c); });
    if (only_whitespace) {
        const std::string sample =
            text.size() > 16 ? text.substr(0, 16) + "..." : text;
        spdlog::warn(
            "materialize_prepared_text: text is empty or whitespace-only "
            "(len={}, sample='{}') — skipping compile_text_layout",
            text.size(), sample);
        return nullptr;
    }

    FontEngine* use_engine = resolve_engine(engine);

    // Default font fallback to preserve the legacy convenience behaviour.
    PreparedText prepared_with_font = normalized;
    if (prepared_with_font.style.font.font_path.empty() &&
        prepared_with_font.style.font.font_family.empty()) {
        prepared_with_font.style.font.font_path = "assets/fonts/Inter-Bold.ttf";
    }

    TextCompileServices services{
        use_engine,
        prepared_with_font.animation.cache_layout
            ? &use_engine->text_layout_cache() : nullptr,
        bundled_font_root_for(prepared_with_font.style.font),
    };

    auto compiled = compile_text_layout(prepared_with_font, services);
    if (!compiled) {
        spdlog::warn(
            "materialize_prepared_text: compile_text_layout failed — "
            "kind={} msg={}",
            static_cast<int>(compiled.error().kind),
            compiled.error().message);
        return nullptr;
    }

    auto text_layout = compiled.value();

    // Defense-in-depth: zero glyphs for non-empty text is a failure.
    if (text_layout->placed.glyphs.empty() && !text.empty()) {
        spdlog::warn(
            "materialize_prepared_text: merged PlacedGlyphRun has zero glyphs "
            "for non-empty input");
        return nullptr;
    }

    auto glyph_states = evaluate_animator_stack(
        prepared_with_font.animation.animators,
        text_layout->placed,
        text,
        sample_time);

    auto shape = std::make_shared<TextRunShape>();
    shape->layout   = text_layout;
    shape->glyphs   = std::move(glyph_states);
    shape->paint    = prepared_with_font.style.paint;
    shape->material = prepared_with_font.style.material;
    shape->shadows  = prepared_with_font.style.shadows;
    shape->animators = prepared_with_font.animation.animators;
    shape->animated_doc = animated_doc;
    shape->engine      = use_engine;

    TextLayoutSpec layout_spec;
    layout_spec.box            = prepared_with_font.frame.size;
    layout_spec.anchor         = prepared_with_font.frame.anchor;
    layout_spec.align          = prepared_with_font.frame.align;
    layout_spec.vertical_align = prepared_with_font.frame.vertical_align;
    layout_spec.wrap           = prepared_with_font.frame.wrap;
    layout_spec.overflow       = prepared_with_font.frame.overflow;
    layout_spec.centering_mode = prepared_with_font.frame.centering_mode;
    layout_spec.line_height    = prepared_with_font.frame.line_height;
    layout_spec.tracking       = prepared_with_font.frame.tracking;
    layout_spec.auto_fit       = prepared_with_font.frame.auto_fit;
    layout_spec.min_font_size  = prepared_with_font.frame.min_font_size;
    layout_spec.max_font_size  = prepared_with_font.frame.max_font_size;
    layout_spec.max_lines      = prepared_with_font.frame.max_lines;
    layout_spec.ellipsis       = prepared_with_font.frame.ellipsis;
    layout_spec.features       = prepared_with_font.shaping.open_type_features;
    shape->layout_spec = layout_spec;
    shape->placement_kind = prepared_with_font.frame.placement.kind;

    if (animated_doc && use_engine) {
        const Frame integral = sample_time.integral_frame();
        const ActiveTextState state = animated_doc->sample_at(integral);
        if (state.active != nullptr) {
            (void)apply_active_state_to_text_run_shape(
                *shape, state, *use_engine, layout_spec);
        }
    }

    return shape;
}

#endif // CHRONON3D_USE_BLEND2D

} // namespace chronon3d
