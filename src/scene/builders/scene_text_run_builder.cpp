#include <chronon3d/presets/font_asset_paths.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
// Scene input adapter for the canonical text compiler in src/text.
#include <chronon3d/text/text_document.hpp>
// TICKET-104: shared internal consumed-flag helper for text-run builders.
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

// Keep fallback font resolution beside the authored primary font. This is a
// deterministic scope for both repository fonts and absolute SDK asset paths.
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
    // Font size is a shaping input in TextDefaults. Update the base style;
    // animated changes belong to the animator stack. The cache key includes
    // font size, so changing it selects the matching layout entry.
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
    // Record the animator index so a following selector binds to this entry.
    m_last_explicit_animator_idx = m_spec->params.animation.animators.size() - 1;
    m_has_explicit_animator = true;
    return *this;
}

TextRunBuilder& TextRunBuilder::selector(GlyphSelectorSpec spec) {
    if (m_pending_selectors.empty() && m_has_explicit_animator) {
        m_spec->params.animation.animators[m_last_explicit_animator_idx]
            .selectors.push_back(std::move(spec));
    } else {
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
    //   3. resolve_engine() process-wide fallback.
    m_spec->font_engine = engine;
    return *this;
}

TextRunBuilder& TextRunBuilder::cache_layout(bool value) {
    m_cache_layout = value;
    // Sync immediately so observers and auto-build can read the value
    // without an explicit `.commit()` call.
    m_spec->params.animation.cache_layout = value;
    return *this;
}

TextRunBuilder& TextRunBuilder::name(std::string n) {
    m_spec->name = std::move(n);
    return *this;
}

// Attach the animated document to the pending spec. Sampling occurs during
// materialization, when the layer's SampleTime is known.

TextRunBuilder& TextRunBuilder::from_animated_document(
    std::shared_ptr<const AnimatedTextDocument> doc
) {
    m_spec->animated_doc = std::move(doc);
    return *this;
}

// ── Commit ──
//
// Return to layer-level chaining after finalizing the pending text-run spec.

LayerBuilder& TextRunBuilder::commit() {
    assert(m_parent != nullptr && "TextRunBuilder commit() called with null parent");

    // TICKET-104: selectors without an animator cannot be materialized.
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

    // The spec is already current; retain the selected cache policy.
    m_spec->params.animation.cache_layout = m_cache_layout;

    // TICKET-104: LayerBuilder::build marks the spec consumed after creating
    // its RenderNode; committing the fluent builder only finalizes its fields.

    return *m_parent;
}

#ifdef CHRONON3D_USE_BLEND2D

namespace text_run_materialize_detail {

/// Use the supplied engine when available. Otherwise use a process-wide
/// engine whose resolver supports absolute paths and system fonts. Relative
/// asset paths require an explicitly mounted engine supplied through the
/// pending spec, layer builder, or render engine. Function-local static
/// initialization is thread-safe.
[[nodiscard]] FontEngine* resolve_engine(FontEngine* preferred) {
    if (preferred) return preferred;

    static assets::AssetResolver s_fallback_resolver;
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

std::shared_ptr<const PreparedTextRun> prepare_text_run(
    const PreparedText& prepared,
    FontEngine* engine
) {
    using namespace text_run_materialize_detail;

    const PreparedText normalized = text_internal::normalize_prepared_text(prepared);
    const std::string& text = normalized.document.utf8;

    // Skip empty or whitespace-only input.
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

    // Supply the default font when neither path nor family was authored.
    PreparedText prepared_with_font = normalized;
    if (!prepared_with_font.glyph_paint_ranges.empty())
        prepared_with_font.animation.cache_layout = false;
    if (prepared_with_font.style.font.font_path.empty() &&
        prepared_with_font.style.font.font_family.empty()) {
        prepared_with_font.style.font.font_path = presets::inter_bold_font_path();
    }

    TextCompileServices services{
        use_engine,
        prepared_with_font.animation.cache_layout
            ? use_engine->text_layout_cache() : nullptr,
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

    // Keep HarfBuzz's normal composed glyph as the geometry source. For a
    // paint boundary inside a grapheme, shape that grapheme a second time
    // without canonical composition to recover the mark glyph and its GPOS
    // offset. The software rasterizer applies that glyph as a SRC_ATOP color
    // overlay, so the composed glyph's alpha coverage remains authoritative.
    if (!prepared_with_font.glyph_paint_ranges.empty()) {
        const std::string_view source{text_layout->source_text};
        const size_t clusters = detail::grapheme_cluster_count(source);
        for (size_t cluster = 0; cluster < clusters; ++cluster) {
            const size_t begin = detail::grapheme_byte_offset_at(source, cluster);
            const size_t end = detail::grapheme_byte_offset_at(source, cluster + 1);
            const bool split = std::any_of(
                prepared_with_font.glyph_paint_ranges.begin(),
                prepared_with_font.glyph_paint_ranges.end(),
                [begin, end](const TextGlyphPaintRange& range) {
                    return (range.byte_start > begin && range.byte_start < end) ||
                           (range.byte_end > begin && range.byte_end < end);
                });
            if (!split) continue;

            TextShaping mark_shaping;
            mark_shaping.direction = prepared_with_font.shaping.direction;
            mark_shaping.script = static_cast<int>(prepared_with_font.shaping.script);
            mark_shaping.language = prepared_with_font.shaping.language;
            mark_shaping.features = prepared_with_font.shaping.open_type_features;
            if (!mark_shaping.features.empty()) mark_shaping.features.push_back(',');
            mark_shaping.features += "ccmp=0,svg-preserve-mark-paint=1";

            const auto cluster_text = source.substr(begin, end - begin);
            const auto shaped_marks = use_engine->shape_text(
                cluster_text, prepared_with_font.style.font,
                text_layout->font_size, mark_shaping);
            if (!shaped_marks || shaped_marks->glyphs.size() < 2) continue;
            const auto decomposed = resolve_placed_glyph_run(*shaped_marks, 0.0f,
                                                              cluster_text);
            if (decomposed.glyphs.size() < 2) continue;

            const auto base_it = std::find_if(
                decomposed.glyphs.begin(), decomposed.glyphs.end(),
                [](const PlacedGlyph& glyph) { return glyph.byte_offset == 0; });
            if (base_it == decomposed.glyphs.end()) continue;

            auto target_it = std::find_if(
                text_layout->placed.glyphs.begin(), text_layout->placed.glyphs.end(),
                [begin](const PlacedGlyph& glyph) {
                    return glyph.byte_offset <= begin &&
                           begin < glyph.byte_offset + glyph.byte_len;
                });
            if (target_it == text_layout->placed.glyphs.end()) continue;

            Color base_color = prepared_with_font.style.color;
            for (const auto& range : prepared_with_font.glyph_paint_ranges) {
                if (begin >= range.byte_start && begin < range.byte_end) {
                    base_color = range.color;
                    break;
                }
            }
            for (const auto& glyph : decomposed.glyphs) {
                if (glyph.byte_offset == 0) continue;
                const size_t source_offset = begin + glyph.byte_offset;
                const auto paint = std::find_if(
                    prepared_with_font.glyph_paint_ranges.begin(),
                    prepared_with_font.glyph_paint_ranges.end(),
                    [source_offset](const TextGlyphPaintRange& range) {
                        return source_offset >= range.byte_start &&
                               source_offset < range.byte_end;
                    });
                if (paint == prepared_with_font.glyph_paint_ranges.end() ||
                    paint->color == base_color) continue;
                target_it->fill_paint_overlays.push_back(GlyphPaintOverlay{
                    glyph.glyph_id, glyph.x - base_it->x,
                    glyph.y - base_it->y, paint->color});
            }
        }
    }

    // SVG paint-only chunk boundaries must not enter TextDocument::spans when
    // they split an extended grapheme: that can move a combining mark into a
    // separately composed run. Apply the source byte ranges to the already
    // shaped glyph clusters instead, preserving the one-run positions.
    if (!prepared_with_font.glyph_paint_ranges.empty()) {
        for (auto& glyph : text_layout->placed.glyphs) {
            for (const auto& range : prepared_with_font.glyph_paint_ranges) {
                if (glyph.byte_offset >= range.byte_start &&
                    glyph.byte_offset < range.byte_end) {
                    glyph.fill_color_override = range.color;
                    break;
                }
            }
        }
    }

    // Defense-in-depth: zero glyphs for non-empty text is a failure.
    if (text_layout->placed.glyphs.empty() && !text.empty()) {
        spdlog::warn(
            "materialize_prepared_text: merged PlacedGlyphRun has zero glyphs "
            "for non-empty input");
        return nullptr;
    }

    TextLayoutSpec layout_spec;
    layout_spec.box            = prepared_with_font.frame.size;
    if (prepared_with_font.frame.grow_and_shrink) {
        const float vertical_padding = prepared_with_font.style.box_style.padding.y * 2.0f;
        layout_spec.box.y = std::max(
            1.0f, text_layout->bounds.y + vertical_padding);
        // The font-fit decision and measured height are resolved before this
        // immutable run reaches render time; the frame evaluator only clips
        // against the final pre-layout box.
        layout_spec.auto_fit = false;
    }
    layout_spec.anchor         = prepared_with_font.frame.anchor;
    layout_spec.align          = prepared_with_font.frame.align;
    layout_spec.vertical_align = prepared_with_font.frame.vertical_align;
    layout_spec.wrap           = prepared_with_font.frame.wrap;
    layout_spec.overflow       = prepared_with_font.frame.overflow;
    layout_spec.centering_mode = prepared_with_font.frame.centering_mode;
    layout_spec.line_height    = prepared_with_font.frame.line_height;
    layout_spec.tracking       = prepared_with_font.frame.tracking;
    layout_spec.auto_fit       = prepared_with_font.frame.auto_fit &&
                                 !prepared_with_font.frame.grow_and_shrink;
    layout_spec.min_font_size  = prepared_with_font.frame.min_font_size;
    layout_spec.max_font_size  = prepared_with_font.frame.max_font_size;
    layout_spec.max_lines      = prepared_with_font.frame.max_lines;
    layout_spec.ellipsis       = prepared_with_font.frame.ellipsis;
    layout_spec.features       = prepared_with_font.shaping.open_type_features;
    auto prepared_run = std::make_shared<PreparedTextRun>();
    prepared_run->layout = std::move(text_layout);
    prepared_run->paint = prepared_with_font.style.paint;
    prepared_run->material = prepared_with_font.style.material;
    prepared_run->shadows = prepared_with_font.style.shadows;
    prepared_run->animators = prepared_with_font.animation.animators;
    prepared_run->engine = use_engine;
    prepared_run->layout_spec = std::move(layout_spec);
    prepared_run->placement_kind = prepared_with_font.frame.placement.kind;
    return prepared_run;
}

std::shared_ptr<TextRunShape> materialize_prepared_text(
    const PreparedTextRun& prepared,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc
) {
    if (!prepared.layout || prepared.engine == nullptr) return nullptr;

    auto shape = std::make_shared<TextRunShape>();
    shape->layout = prepared.layout;
    shape->glyphs = evaluate_animator_stack(
        prepared.animators,
        prepared.layout->placed,
        prepared.layout->source_text,
        sample_time,
        &prepared.layout->font_spans);
    shape->paint = prepared.paint;
    shape->material = prepared.material;
    shape->shadows = prepared.shadows;
    shape->animators = prepared.animators;
    shape->animated_doc = std::move(animated_doc);
    shape->engine = prepared.engine;
    shape->layout_spec = prepared.layout_spec;
    shape->placement_kind = prepared.placement_kind;

    if (shape->animated_doc) {
        const Frame integral = sample_time.integral_frame();
        const ActiveTextState state = shape->animated_doc->sample_at(integral);
        if (state.active != nullptr) {
            (void)apply_active_state_to_text_run_shape(
                *shape, state, *prepared.engine, prepared.layout_spec);
        }
    }

    return shape;
}

std::shared_ptr<TextRunShape> materialize_prepared_text(
    const PreparedText& prepared,
    FontEngine* engine,
    SampleTime sample_time,
    std::shared_ptr<const AnimatedTextDocument> animated_doc
) {
    // PreparedText is the immutable canonical payload.  PreparedTextRun is a
    // derived materialization and must never be stored back into that payload:
    // authoring mutators may legally change animation/style fields before the
    // builder is consumed.  Layout reuse remains owned by FontEngine's
    // canonical compile_text_layout cache.
    auto cached = prepare_text_run(prepared, engine);
    if (!cached) return nullptr;
    return materialize_prepared_text(*cached, sample_time, std::move(animated_doc));
}

#endif // CHRONON3D_USE_BLEND2D

} // namespace chronon3d
