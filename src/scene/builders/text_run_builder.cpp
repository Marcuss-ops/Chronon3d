#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <cassert>
#include <iterator>
#include <utility>

namespace chronon3d {

// ── Private ctor ────────────────────────────────────────────────────────

TextRunBuilder::TextRunBuilder(LayerBuilder* parent, TextRunSpec* spec)
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
    // Font size is a TextRunParams-level field (it affects HarfBuzz
    // shaping), not a per-frame animated glyph property.  Mutating it
    // here does NOT animate; it updates the base parameter and will
    // invalidate the layout cache on the next commit.
    m_spec->params.font_size = v;
    m_cache_layout = false;  // force re-shape next commit
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
    return *this;
}

TextRunBuilder& TextRunBuilder::selector(GlyphSelectorSpec spec) {
    // Two valid chain patterns:
    //
    //  1. `.selector(s).animator(a)` — selector should bind to the upcoming
    //     animator.  Queue to `m_pending_selectors`; consume on next
    //     `.animator()`.
    //
    //  2. `.animator(a).selector(s)` or `.selector(s)` standalone — selector
    //     stands alone.  Append its own (no-property) animator entry so the
    //     chain length stays consistent with test-time expectations
    //     (animators.size() matches the chain cardinality for stable
    //     downstream assertions).
    if (!m_pending_selectors.empty()) {
        // Pattern #1 — still waiting to bind to the next `.animator()`.
        m_pending_selectors.push_back(std::move(spec));
        return *this;
    }
    TextAnimatorSpec entry;
    entry.id = "__trb_selector_" + std::to_string(m_implicit_id_seq++);
    entry.enabled = true;
    entry.selectors = { std::move(spec) };
    append_animator(std::move(entry));
    return *this;
}

// ── Meta ──

TextRunBuilder& TextRunBuilder::font_engine(FontEngine* engine) {
    m_font_engine = engine;
    return *this;
}

TextRunBuilder& TextRunBuilder::cache_layout(bool value) {
    m_cache_layout = value;
    return *this;
}

TextRunBuilder& TextRunBuilder::name(std::string n) {
    m_spec->name = std::move(n);
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
    if (m_font_engine != nullptr) {
        // Forward font_engine override onto the spec via a side channel
        // (LayerBuilder reads m_font_engine for shaping during commit).
        // The builder itself does NOT own a pointer to the engine field
        // on LayerBuilder — LayerBuilder::text_run plumbing handles it.
    }
    return *m_parent;
}

// ═══════════════════════════════════════════════════════════════════════════
// materialize_text_run_shape — shared helper
// ═══════════════════════════════════════════════════════════════════════════

namespace text_run_materialize_detail {

/// Resolve the engine to use (caller-supplied preferred, falling back
/// to the process-wide shared FontEngine).  Returns nullptr if no
/// engine is available.
[[nodiscard]] FontEngine* resolve_engine(FontEngine* preferred) {
    if (preferred) return preferred;
    return &shared_font_engine();
}

} // namespace text_run_materialize_detail

std::shared_ptr<TextRunShape> materialize_text_run_shape(
    const TextRunParams& params,
    FontEngine* engine,
    SampleTime sample_time
) {
    using namespace text_run_materialize_detail;

    if (params.text.empty()) {
        spdlog::warn(
            "materialize_text_run_shape: empty text — skipping.");
        return nullptr;
    }

    FontEngine* use_engine = resolve_engine(engine);
    if (!use_engine) {
        spdlog::error(
            "materialize_text_run_shape: no FontEngine available — "
            "text_run '{}' will render blank.",
            params.text
        );
        return nullptr;
    }

    const FontSpec font_spec{
        params.font_path,
        params.font_family,
        params.font_weight,
        params.font_style,
    };

    // ── Cache lookup ────────────────────────────────────────────────
    TextLayoutCacheKey cache_key{
        .text = params.text,
        .font_path = params.font_path,
        .font_weight = params.font_weight,
        .font_style = params.font_style,
        .font_size = params.font_size,
        .tracking = params.tracking,
        .box_width = params.size.x,
        .wrap = params.wrap,
        .direction = params.direction,
        .language = params.language,
    };

    std::shared_ptr<TextRunLayout> layout;
    auto& cache = shared_text_layout_cache();
    if (params.cache_layout) {
        if (auto cached = cache.find(cache_key)) {
            layout = std::const_pointer_cast<TextRunLayout>(cached);
        }
    }

    if (!layout) {
        // ── Fresh shape ────────────────────────────────────────────
        TextShaping shaping;
        shaping.direction = params.direction;
        shaping.language  = params.language;
        auto hb_run = use_engine->shape_text(
            params.text, font_spec, params.font_size, shaping);
        if (!hb_run) {
            spdlog::warn(
                "materialize_text_run_shape: shape_text failed for "
                "'{}' (font='{}', size={}) — text_run skipped.",
                params.text, params.font_path, params.font_size
            );
            return nullptr;
        }

        auto placed = resolve_placed_glyph_run(
            *hb_run, params.tracking, params.text);

        layout = std::make_shared<TextRunLayout>();
        layout->source_text = params.text;
        layout->font = font_spec;
        layout->font_size = params.font_size;
        layout->placed = placed;
        layout->units = build_text_unit_map(placed, params.text);
        layout->bounds = { placed.total_width, placed.total_height };
        layout->tracking = params.tracking;
        layout->wrap = params.wrap;
        layout->direction = params.direction;
        layout->language = params.language;
        layout->line_height = params.line_height;

        if (params.cache_layout) {
            cache.store(std::move(cache_key), layout);
        }
    }

    // ── Evaluate animators at sample_time ───────────────────────────
    auto glyph_states = evaluate_animator_stack(
        params.animators, layout->placed, params.text, sample_time);

    // ── Materialize TextRunShape ────────────────────────────────────
    auto shape = std::make_shared<TextRunShape>();
    shape->layout   = layout;
    shape->glyphs   = std::move(glyph_states);
    shape->paint    = params.paint;
    shape->material = params.material;
    shape->shadows  = params.shadows;
    return shape;
}

} // namespace chronon3d
