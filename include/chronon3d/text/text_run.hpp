#pragma once

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>  // TextPaint, TextShadow
#include <chronon3d/scene/builders/builder_params.hpp>  // TextLayoutSpec — referenced by TextRunShape::layout_spec
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>     // Frame — for the cache-key overload

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-103a — freeze-minimum cat-3 type aliases used by TextLayoutRequest,
// TextRunLayout, and TextLayoutCacheKey.  Same bytewise layout as
// std::string / TextDirection — no new classes introduced, just explicit
// semantic names.  Promoted from src/text/aliases.hpp's
// Bcp47LanguageTag alias (which stays internal-and-unused) so the
// TICKET-103a cat-3 refactor can extend the existing TextLayoutRequest
// POD struct with named fields without expanding the public type surface.
// ═══════════════════════════════════════════════════════════════════════════

/// RFC 5646 BCP-47 language tag (e.g. "en", "en-US", "ar", "zh-Hant-HK").
/// Passes through to HarfBuzz hb_language_from_string() unchanged.
/// Same bytewise layout as std::string.
using Bcp47LanguageTag = std::string;

/// OpenType / HarfBuzz shaping features string (e.g. "kern=1,liga=1,dlig=0").
/// Empty default = "use the font's natural feature set".  Same bytewise
/// layout as std::string.  Held as a single string for the freeze-minimum
/// scope; the more elaborate per-feature toggle struct (i32 tag + u8
/// value + u8 start/end + u16 reserved fields, à la hb_feature_t) is
/// TICKET-093 (cat-violator post-baseline-verde).
using TextShapingFeatures = std::string;

// ═══════════════════════════════════════════════════════════════════════════
// TextRunLayout — immutable layout data for a shaped text run
// ═══════════════════════════════════════════════════════════════════════════
//
// Produced once per unique (text, font, size, tracking, wrap) combination.
// Shared across frames via a std::shared_ptr, so multiple TextRunShape
// instances (or multiple frames of the same animated text) reuse the same
// layout without re-shaping.
//
// Mutability: once created, the layout is immutable. Only the per-frame
// animated glyph states change (stored separately in GlyphInstanceState).

// ═══════════════════════════════════════════════════════════════════════════
// ShapedFontSpan — per-glyph font identity blocks (Phase 1.4)
// ═══════════════════════════════════════════════════════════════════════════
//
// A contiguous run of glyphs in `TextRunLayout::placed.glyphs` that share
// the SAME font identity.  Used by the renderer to switch BLFont at span
// boundaries WITHOUT re-allocating a fresh TextRunLayout for each font.
//
// Closure contract for verdict Issue #3 (Fase 1.4 additive):
//   compile_text_layout() populates `TextRunLayout::font_spans` so that
//   `glyphs[span.glyph_begin .. span.glyph_end)` are guaranteed to have
//   been shaped with `span.font` (FontIdentity).  The renderer iterates
//   the spans, resolves a BLFont per UNIQUE `span.font` (typically 1 —
//   single-font paragraphs — sometimes 2+ — bidirectional Latin/Hebrew
//   runs, or multi-font paragraphs mixing static bases), and switches
//   BLFont at span boundaries.  The `font_size` dimension is NOT in
//   FontIdentity (it's a layout concern — see FontIdentity header doc)
//   so size variation does NOT split a single span.
//
// Empty paragraphs and unresolved paragraphs (`placed.glyphs.empty()`)
// have an empty `font_spans` vector — the renderer's loop over
// `tier_glyphs` short-circuits to nothing in that case.
struct ShapedFontSpan {
    FontIdentity   font;        // identity of the font for glyphs [glyph_begin, glyph_end)
    std::uint32_t  glyph_begin{0};  // inclusive index into TextRunLayout::placed.glyphs
    std::uint32_t  glyph_end{0};    // EXCLUSIVE index
};

struct TextRunLayout {
    std::string source_text;                     // original UTF-8 source text
    FontSpec font;                               // primary font (drives cache key; may NOT cover all glyphs — see font_spans)
    f32 font_size{72.0f};                        // requested font size in pixels

    PlacedGlyphRun placed;                       // shaped + positioned glyphs
    TextUnitMap units;                           // glyph → grapheme/word/line maps

    Vec2 bounds{0.0f, 0.0f};                     // total bounding box (width, height)
    f32 line_height{0.0f};                       // font line height in pixels

    /// Per-glyph-blocks telling the renderer which font identity each
    /// contiguous glyph range uses.  For a single-font paragraph this
    /// is exactly ONE span [0, placed.glyphs.size()).  For a multi-font
    /// paragraph or a bidi paragraph that resolved into multiple
    /// ResolvedTextRun entries of the same font, this has one span per
    /// run (in glyph-index order).  EMPTY when the paragraph has zero
    /// glyphs.  Renderer iterates this to pre-resolve BLFont instances
    /// and switch at boundaries — see text_run_processor.cpp.
    std::vector<ShapedFontSpan> font_spans;

    /// Shaping parameters used (for cache key hashing / diagnostics)
    f32 tracking{0.0f};                          // per-cluster tracking in pixels
    TextWrap wrap{TextWrap::None};               // wrapping mode
    TextDirection direction{TextDirection::Auto};
    Bcp47LanguageTag language;                  // BCP-47 language tag (TICKET-103a: alias of std::string)
    TextShapingFeatures features;               // OT shaping features (TICKET-103a: new field)

    /// Compute a hash of the layout content (text + font + shaping + wrapping).
    /// Stable across different materials/strokes on the same text.
    [[nodiscard]] u64 layout_hash() const;

    /// Compute a hash of only the shaping parameters (font, size, wrap, direction).
    /// Excludes the source text — useful for cache partitioning.
    [[nodiscard]] u64 shaping_hash() const;
};

/// Immutable, shared ownership of a TextRunLayout.
/// Multiple frames/components can hold the same pointer without copying.
using SharedTextRunLayout = std::shared_ptr<const TextRunLayout>;

// ═══════════════════════════════════════════════════════════════════════════
// Fase C1 — CompiledTextRun (planned, deferred to post-feature-freeze)
// ═══════════════════════════════════════════════════════════════════════════
//
// The render hot path (draw_text_run) currently recomputes several
// lookup structures on every frame that depend ONLY on the immutable
// TextRunLayout.  These should be pre-compiled once and cached:
//
// Planned CompiledTextRun structure:
//
//   struct CompiledTextRun {
//       SharedTextRunLayout    layout;          // immutable shared layout
//       std::vector<ResolvedFontSpan> spans;    // pre-resolved font handles
//       std::vector<std::size_t> glyph_to_span; // per-glyph → span index
//   };
//
//   struct ResolvedFontSpan {
//       FontFaceHandle handle;   // pre-resolved via TextRenderResources
//       // NOTE: BLFont is a software-backend dependency. If CompiledTextRun
//       // moves into include/chronon3d/text/, use a type-erased handle or
//       // opaque pointer to avoid coupling text_core to Blend2D.
//       void*          blfont_opaque;  // type-erased BLFont handle
//       std::uint32_t  glyph_begin, glyph_end;  // same as ShapedFontSpan
//   };
//
//   // Per-frame blur tier classification.  blur values come from
//   // animated GlyphInstanceState, so tiers are computed O(G) per frame.
//   // This struct stores the result — it's not pre-compiled.
//   struct PerFrameBlurTiers {
//       std::array<std::vector<u32>, 5> tiers;
//   };
//
// Migration plan (post-feature-freeze):
//   1. CompiledTextRun stored alongside TextRunShape (or inside it)
//   2. compile(TextRunLayout, TextRenderResources&) → CompiledTextRun
//   3. draw_text_run receives const CompiledTextRun& instead of
//      resolving font spans inline
//   4. ResolvedFontSpan::handle stays valid for the engine lifetime
//      (TextRenderResources owns the underlying FontFaceHandle pool)
//
// Blocked by: new types in public headers → violates feature freeze.
// ═══════════════════════════════════════════════════════════════════════════

// Forward declaration for AnimatedTextDocument.  TextRunShape holds the
// doc as a `shared_ptr<const AnimatedTextDocument>`, which only needs a
// complete type when the template instantiates its deleter; the rest
// of the class only needs the type to be declared.  Avoids pulling the
// heavyweight `animated_text_document.hpp` (which transitively pulls
// `text_document.hpp` and `easing/easing.hpp`) into every TU that
// includes text_run.hpp.
class AnimatedTextDocument;

// ═══════════════════════════════════════════════════════════════════════════
// TextRunShape — batched text run with per-glyph animation state
// ═══════════════════════════════════════════════════════════════════════════
//
// Unlike TextShape (which describes a static string with styling), TextRunShape
// combines an immutable shared layout with per-frame animated glyph states.
// This enables After Effects-style text animators (selector + properties)
// to drive glyph-level transforms, opacity, color, blur, etc. without
// creating a separate layer per character.
//
// IMPORTANT: TextRunShape is NOT embedded in the Shape struct (shape.hpp).
// Instead, a dedicated TextRunNode in the render graph will hold it directly.
// This avoids a circular include: fill_style.hpp → shape.hpp → text_run.hpp
// → text_animator_property.hpp → glyph_selector.hpp → animated_value.hpp
// → fill_style.hpp (circular!).

struct TextRunShape {
    SharedTextRunLayout layout;                  // immutable layout (shared across frames)
    std::vector<GlyphInstanceState> glyphs;       // per-glyph animated state

    TextMaterial material;                         // premium material settings
    TextPaint paint;                              // fill/stroke paint settings
    std::vector<TextShadow> shadows;              // per-layer shadow stack

    // ── Animation driver payload (PR 8) ────────────────────────────────
    // Stored at materialization time so the per-frame driver
    // (update_text_run_shape_per_frame in text_run_driver.hpp) can
    // re-evaluate the AE-style animator stack on each render frame
    // without re-shaping.  Empty vector ⇒ static layout; the renderer
    // treats it as no-op and skips per-frame work.
    //
    // The animator list is owned by the shape so the compositor
    // doesn't have to chase references back into builder state.
    std::vector<TextAnimatorSpec> animators;

    // ── PR 9 — AnimatedTextDocument + helpers for per-frame resample ─
    //
    // When the scene author attached a transition document via
    // `TextRunBuilder::from_animated_document(...)`, the materializer
    // stores both the document (shared_ptr, non-owning extension) and
    // the FontEngine + TextLayoutSpec needed by
    // `apply_active_state_to_text_run_shape` so the per-frame driver
    // can re-sample the doc at every frame and apply transition_text
    // without going back to builder state.  Without these slots the
    // PR 8 driver would only re-evaluate the AE-style animator stack
    // and miss the doc's transition_text changes.
    //
    // `engine` is a raw pointer consistent with the prior
    // PendingTextRun::font_engine convention; the layer is responsible
    // for keeping the engine alive for the shape's lifetime.
    std::shared_ptr<const AnimatedTextDocument> animated_doc{};
    FontEngine* engine{nullptr};
    TextLayoutSpec layout_spec{};

    // ── PR 11 — CrossfadeLayouts per-glyph blend state ────────────────
    //
    // Populated by `apply_active_state_to_text_run_shape` ONLY when
    // `AnimatedTextDocument::sample_at` returns transition ==
    // CrossfadeLayouts AND mix is strictly inside (0, 1).  Cleared
    // (reset to nullptr / empty vector) when mix reaches 0 or 1, or
    // when the next keyframe takes ownership (`active` pointer changes
    // to a different keyframe).  Empty slots ⇒ no crossfade work; the
    // per-frame driver and the compositor both short-circuit on that.
    //
    // `crossfade_layout` holds the SharedTextRunLayout for the OUTGOING
    // document so the executor can read glyph positions without paying
    // a shaping cost.  It is built lazily on first frame inside the
    // gap via `build_text_run` (with the shared TextLayoutCache); the
    // cache key naturally aligns with what `prewarm_text_run_layout_for_frame`
    // populates ahead of time.
    //
    // `crossfade_glyphs` mirrors `glyphs`: a per-glyph animated state
    // vector of length `crossfade_layout->placed.glyphs.size()`, seeded
    // from `make_initial_glyph_states` and re-evaluated each frame via
    // `evaluate_animator_stack_into` so the AE-style animator stack
    // applies to BOTH sides during the gap.
    //
    // `crossfade_mix` mirrors `state.mix`: 0 = fully active side, 1 =
    // fully next side.  The compositor folds this into per-glyph
    // opacity: outgoing side fades by (1 - mix), incoming side by mix.
    // Stored so the compositor (and the hash fold) can read the value
    // without re-sampling the animated_doc — it stays stable across
    // cache-key lookups and the per-frame driver.
    //
    // Lifecycle contract — apply + driver:
    //   1. apply_active_state_to_text_run_shape (PR 11) populates
    //      crossfade_layout + crossfade_glyphs on entering the gap,
    //      updates crossfade_mix on every frame INSIDE the gap, and
    //      clears all three slots on transitioning OUT of the gap.
    //   2. update_text_run_shape_per_frame runs the animator stack
    //      against crossfade_glyphs on every frame inside the gap
    //      so per-glyph animators (transform / opacity / blur) apply
    //      to BOTH sides symmetrically.
    SharedTextRunLayout crossfade_layout{};                       // nullptr when no crossfade
    std::vector<GlyphInstanceState> crossfade_glyphs{};          // empty when no crossfade
    float crossfade_mix{0.0f};                                   // current blend factor
};

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCacheKey — key for the immutable layout cache
// ═══════════════════════════════════════════════════════════════════════════
//
// Cached per unique (text, font, size, tracking, wrap, direction, language).
// A hit avoids re-shaping with HarfBuzz + FreeType, which is the dominant
// cost for long/complex text runs.

struct TextLayoutCacheKey {
    std::string text;
    std::string font_path;
    int font_weight{400};
    std::string font_style{"normal"};
    f32 font_size{32.0f};
    f32 tracking{0.0f};
    f32 box_width{0.0f};                         // 0 = no wrapping
    TextWrap wrap{TextWrap::None};
    TextDirection direction{TextDirection::Auto};
    Bcp47LanguageTag language;                   // BCP-47 language tag (TICKET-103a: alias of std::string)
    TextShapingFeatures features;                // OT shaping features (TICKET-103a: new field)
    std::string font_family;                     // P0-2 — was missing; placed last for aggregate init compat

    /// Paragraph-level typography.  Different composer/justification/
    /// indentation/spacing/hanging-punctuation settings produce different
    /// line breaks, so they MUST be part of the cache key.
    ParagraphStyle paragraph{};

    [[nodiscard]] u64 digest() const;
    [[nodiscard]] bool operator==(const TextLayoutCacheKey& other) const = default;
};

/// std::hash for TextLayoutCacheKey.
struct TextLayoutCacheKeyHash {
    [[nodiscard]] size_t operator()(const TextLayoutCacheKey& key) const noexcept;
};

// ═══════════════════════════════════════════════════════════════════════════
// TextLayoutCache — thread-safe LRU cache for TextRunLayout objects
// ═══════════════════════════════════════════════════════════════════════════
//
// Uses the existing LruCache infrastructure (lru_cache.hpp) for eviction
// and thread safety.  Capacity defaults to 64 MiB, tunable via Config.
//
// Typical usage pattern:
//   auto& cache = shared_text_layout_cache();
//   TextLayoutCacheKey key{...};
//   auto layout = cache.find(key);
//   if (!layout) {
//       layout = build_text_run_layout(key);
//       cache.store(key, layout);
//   }

class TextLayoutCache {
public:
    using Result = SharedTextRunLayout;

    explicit TextLayoutCache(size_t capacity_bytes = 64ULL * 1024 * 1024);
    ~TextLayoutCache();

    // Non-copyable (holds unique_ptr to Impl)
    TextLayoutCache(const TextLayoutCache&) = delete;
    TextLayoutCache& operator=(const TextLayoutCache&) = delete;
    TextLayoutCache(TextLayoutCache&&) noexcept;
    TextLayoutCache& operator=(TextLayoutCache&&) noexcept;

    /// Look up a layout. Returns nullptr if not cached.
    [[nodiscard]] Result find(const TextLayoutCacheKey& key) const;

    /// Store a layout in the cache.
    void store(TextLayoutCacheKey key, Result value);

    /// Check if a layout exists without promoting in LRU order.
    [[nodiscard]] bool contains(const TextLayoutCacheKey& key) const;

    /// Erase a specific entry.
    bool erase(const TextLayoutCacheKey& key);

    /// Clear all cached layouts.
    void clear();

    /// Return the number of cached entries.
    [[nodiscard]] size_t size() const;

    /// Hit/miss/eviction stats.
    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t current_size{0};
        size_t current_weight{0};
    };
    [[nodiscard]] Stats stats() const;

    /// Resize the cache capacity. Evicts LRU entries if tightening.
    void set_capacity(size_t capacity_bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// P1-DEPRECATED — return the process-wide shared TextLayoutCache singleton.
///
/// ⚠️  DEPRECATION PATH (P1 #3):
///   Migrate callers to `RenderSession::layout_cache` (per-session
///   owned).  Each RenderSession now carries its own TextLayoutCache;
///   session isolation guarantees no cross-session cache pollution.
///
///   Migration guide:
///     auto& cache = shared_text_layout_cache();              // OLD
///     auto& cache = render_session.layout_cache;       // NEW
///
///   For functions that don't have a RenderSession& in scope:
///   thread a `TextLayoutCache*` or `RenderSession&` through the
///   call chain (post-baseline work — see docs/FOLLOWUP_TICKETS.md).
///
///   Tests may create a standalone `TextLayoutCache local_cache` or
///   use RenderSession{}.layout_cache() for isolated test fixtures.
///
/// NOTE: [[deprecated]] attribute is deferred until production callsites
/// are migrated to avoid ~30 build warnings (P1 #3 post-baseline).
[[nodiscard]] TextLayoutCache& shared_text_layout_cache();

/// P1-DEPRECATED — reset the process-wide shared TextLayoutCache singleton.
/// Use RenderSession::layout_cache.clear() instead.
void reset_shared_text_layout_cache();

/// Free function to hash a TextRunShape for content fingerprinting.
///
/// Optional overload that folds the AnimatedTextDocument state at the
/// requested integral frame.  When `s.animated_doc` is bound, this overload
/// samples the doc at `frame` and folds the per-frame transition_type +
/// active->utf8 + active->defaults.font + transition_text bytes +
/// morph_map bytes + mix value into the cache key, so Scramble / Morph /
/// CrossfadeLayouts / font-swap Cut renders correctly invalidate the
/// executor's per-frame node cache without false hits and without leaking
/// stale layout bytes.
///
/// The single-argument variant remains valid for shapes whose layout
/// never changes between frames (e.g. plain static text); its doc-fold
/// branch is a no-op.
///
/// Takes a `Frame` (not `SampleTime`) so this header does not need to
/// pull `sample_time.hpp` (FrameRate + TemporalSampleKey + math
/// headers) into every TU that uses TextRunShape.  Sub-frame precision
/// was not used by the implementation anyway.
[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s);

[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s, Frame frame);

} // namespace chronon3d
