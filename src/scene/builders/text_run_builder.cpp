#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <chronon3d/text/text_run_driver.hpp>

#include <cassert>
#include <iterator>
#include <utility>

namespace chronon3d {

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
    m_spec->params.animators.push_back(std::move(spec));
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
    // Font size lives inside the composable TextSpec under
    // .text().font.font_size — it affects HarfBuzz shaping upstream of
    // any glyph-level animation, so this mutator updates the BASE
    // parameter (no animator injection).  Setting the size also
    // invalidates the layout cache so the next commit re-shapes.
    m_spec->params.text.font.font_size = v;
    m_cache_layout = false;
    // PR 2: sync immediately so observers reading `spec().cache_layout`
    // (without calling `.commit()`) see the invalidated state.
    m_spec->params.cache_layout = false;
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
    m_last_explicit_animator_idx = m_spec->params.animators.size() - 1;
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
        m_spec->params.animators[m_last_explicit_animator_idx]
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
    //   3. shared_font_engine()       (process-wide fallback in materialize_text_run_shape)
    m_spec->font_engine = engine;
    return *this;
}

TextRunBuilder& TextRunBuilder::cache_layout(bool value) {
    m_cache_layout = value;
    // PR 2: sync to spec immediately so observers / auto-build can read
    // the value without an explicit `.commit()` call.
    m_spec->params.cache_layout = value;
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
    // LayerBuilder::build() reads m_text_runs directly; the spec is
    // already up-to-date.  Touching m_cache_layout=false here forces
    // a re-shape even if the layout cache already contains an entry
    // for the spec's TextRunParams (because user edits may have
    // changed shaping inputs).
    m_spec->params.cache_layout = m_cache_layout;
    return *m_parent;
}

// ═══════════════════════════════════════════════════════════════════════════
// materialize_text_run_shape — shared helper
//
// Reads the composable nested TextRunSpec fields after the PR3→PR4
// migration (TextRunParams is now an alias of TextRunSpec — see
// builder_params.hpp).
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_USE_BLEND2D

namespace text_run_materialize_detail {

/// WP-8 PR 8.0 — resolver is now REQUIRED at the per-spec override
/// (`PendingTextRun.font_engine`) or per-layer default
/// (`LayerBuilder.m_font_engine`).  Returns `preferred` if non-null and
/// nullptr otherwise; the legacy `&shared_font_engine()` fallback has
/// been REMOVED in PR 8.0 — production code paths must supply a
/// `FontEngine*` bound to an explicit `AssetResolver&` either at the
/// per-spec override (PendingTextRun.font_engine) or at the per-layer
/// default (LayerBuilder::m_font_engine).
[[nodiscard]] FontEngine* resolve_engine(FontEngine* preferred) {
    return preferred;
}

} // namespace text_run_materialize_detail

std::shared_ptr<TextRunShape> materialize_text_run_shape(
    const TextRunParams& params,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc
) {
    using namespace text_run_materialize_detail;

    // ── Composable nested field paths (PR4 canonical form) ──────────
    const std::string& text      = params.text.content.value;
    const FontSpec&     font_spec = params.text.font;
    const TextLayoutSpec& layout = params.text.layout;
    const TextAppearanceSpec& appearance = params.text.appearance;

    if (text.empty()) {
        spdlog::warn("materialize_text_run_shape: empty text — skipping.");
        return nullptr;
    }

    FontEngine* use_engine = resolve_engine(engine);
    if (!use_engine) {
        spdlog::error(
            "materialize_text_run_shape: no FontEngine available — "
            "text_run '{}' (no resolver bound) will render blank.  "
            "WP-8 PR 8.0: caller must supply a FontEngine* bound to an "
            "explicit AssetResolver via PendingTextRun.font_engine or "
            "LayerBuilder.m_font_engine.",
            text
        );
        return nullptr;
    }

    const FontSpec shaped_font{
        font_spec.font_path,
        font_spec.font_family,
        font_spec.font_weight,
        font_spec.font_style,
    };

    // ── Cache lookup ────────────────────────────────────────────────
    TextLayoutCacheKey cache_key{
        .text = text,
        .font_path = font_spec.font_path,
        .font_weight = font_spec.font_weight,
        .font_style = font_spec.font_style,
        .font_size = font_spec.font_size,
        .tracking = layout.tracking,
        .box_width = layout.box.x,
        .wrap = layout.wrap,
        .direction = params.direction,
        .language = params.language,
    };

    std::shared_ptr<TextRunLayout> text_layout;
    auto& cache = shared_text_layout_cache();
    if (params.cache_layout) {
        if (auto cached = cache.find(cache_key)) {
            text_layout = std::const_pointer_cast<TextRunLayout>(cached);
        }
    }

    if (!text_layout) {
        // ── Fresh shape ────────────────────────────────────────────
        TextShaping shaping;
        shaping.direction = params.direction;
        shaping.language  = params.language;
        auto hb_run = use_engine->shape_text(
            text, shaped_font, font_spec.font_size, shaping);
        if (!hb_run) {
            spdlog::warn(
                "materialize_text_run_shape: shape_text failed for "
                "'{}' (font='{}', size={}) — text_run skipped.",
                text, font_spec.font_path, font_spec.font_size
            );
            return nullptr;
        }

        auto placed = resolve_placed_glyph_run(
            *hb_run, layout.tracking, text);

        text_layout = std::make_shared<TextRunLayout>();
        text_layout->source_text = text;
        text_layout->font = shaped_font;
        text_layout->font_size = font_spec.font_size;
        text_layout->placed = placed;
        text_layout->units = build_text_unit_map(placed, text);
        text_layout->bounds = { placed.total_width, placed.total_height };
        text_layout->tracking = layout.tracking;
        text_layout->wrap = layout.wrap;
        text_layout->direction = params.direction;
        text_layout->language = params.language;
        text_layout->line_height = layout.line_height;

        if (params.cache_layout) {
            cache.store(std::move(cache_key), text_layout);
        }
    }

    // ── Evaluate animators at sample_time ───────────────────────────
    auto glyph_states = evaluate_animator_stack(
        params.animators, text_layout->placed, text, sample_time);

    // ── Materialize TextRunShape ────────────────────────────────────
    auto shape = std::make_shared<TextRunShape>();
    shape->layout   = text_layout;
    shape->glyphs   = std::move(glyph_states);
    shape->paint    = appearance.paint;
    shape->material = appearance.material;
    shape->shadows  = appearance.shadows;

    // ── PR 8 wiring ──────────────────────────────────────────────────
    // Stash the animator list on the shape so the per-frame driver
    // (update_text_run_shape_per_frame in text_run_driver.hpp) can
    // re-evaluate glyph state on each render frame without
    // re-materializing the shape.  Empty list = static layout; the
    // driver treats it as no-op.
    shape->animators = params.animators;

    // ── PR 9 wiring — bind animated_doc + helpers onto the shape ──────
    //
    // Storing `animated_doc`, the FontEngine, and the TextLayoutSpec
    // ON THE SHAPE lets the per-frame driver
    // (`update_text_run_shape_per_frame`) re-sample the doc on every
    // frame and forward the resulting ActiveTextState through
    // `apply_active_state_to_text_run_shape`.  Without this, only the
    // build-time (one-shot) call would resolve the doc — subsequent
    // frames would render the layout text FROM the first frame, not
    // the current `transition_text` (Scramble / Morph) or
    // active-document text.
    shape->animated_doc = animated_doc;
    shape->engine      = use_engine;
    shape->layout_spec = params.text.layout;

    // Also run a single preliminary materialization-time apply so the
    // shape is already mid-transition-state-correct on the very first
    // frame the compositor reads (avoids one frame of "initial literal"
    // ghosting before the per-frame driver kicks in).
    if (animated_doc && use_engine) {
        const Frame integral = sample_time.integral_frame();
        const ActiveTextState state = animated_doc->sample_at(integral);
        if (state.active != nullptr) {
            (void)apply_active_state_to_text_run_shape(
                *shape, state, *use_engine, params.text.layout);
            // TODO(PR 10+): cache-key invalidation tracking.  When
            // transition_text drives a layout swap, the executor's
            // pre-mutation cache key doesn't reflect the new layout
            // and we end up keying stale entries.  See the matching
            // TODO in src/text/text_run_driver.cpp update_text_run_shape_per_frame
            // for the per-frame analogue.
        }
    }

    return shape;
}

#endif // CHRONON3D_USE_BLEND2D

} // namespace chronon3d
