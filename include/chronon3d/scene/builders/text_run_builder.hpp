#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// TextRunBuilder — fluent chainable builder for `LayerBuilder::text_run(...)`.
//
// Returned by reference from `LayerBuilder::text_run(name, TextRunParams)`.
// Each mutator returns `TextRunBuilder&` so multiple setters can be
// chained:
//
//     layer.text_run("hello", TextRunParams{...})
//          .position({10, 20, 0})
//          .opacity(0.8f)
//          .animator(slide_in_animator)
//          .selector(stagger_selector);
//
// ── Chaining semantics ──────────────────────────────────────────────────
//
// `.position()`, `.opacity()`, `.anchor()`, `.rotate()`, `.scale()`,
// `.font_size()`, `.blur()`, `.tracking()`, `.baseline_shift()` do NOT
// mutate the LAYER's transform (those are owned by LayerBuilder methods
// like `LayerBuilder::position(...)`).  Instead, every TextRunBuilder
// mutator injects an implicit internal `TextAnimatorSpec` whose only
// selector is a global "weight = 1 for every glyph" selector — this is
// the After Effects-style "entire text as one unit" action.
//
// This keeps the API uniform: every TextRunBuilder mutator is
// interchangeable with a hand-written `TextAnimatorSpec` carrying a
// global selector.  Example:
//
//     builder.opacity(0.5f);          // implicit animator weight=1, opacity=0.5
//     // is equivalent to
//     builder.animator(TextAnimatorSpec{
//         .id = "_trb_opacity",
//         .selectors = {{.id = "_trb_opacity_sel",
//                        .unit = TextSelectorUnit::Glyph,
//                        .shape = TextSelectorShape::Square,
//                        .start = {0.0f}, .end = {100.0f}, .amount = {100.0f}}},
//         .properties = {OpacityProperty{0.5f}}
//     });
//
// `.animator(spec)` and `.selector(spec)` append user-supplied animators
// and selectors to the spec.  These are evaluated AFTER the implicit
// per-mutator animator, so user animators can OVERRIDE the per-mutator
// value (great for entrance/exit animations that override resting
// position).  In compound expressions the user can call `.animator(...)`
// with `transform_mode = Replace` to fully take over a property.
//
// `.commit()` resolves the implicit animator stack into the pending
// `TextRunPendingSpec` and returns the parent `LayerBuilder&` so layer-level
// methods can be chained again.  `commit()` MUST be called explicitly
// OR the spec will be auto-committed by `LayerBuilder::build()`.
//
// ── Non-owning ──────────────────────────────────────────────────────────
//
// The builder holds a non-owning pointer back to the LayerBuilder so it
// can mutate the layer's pending-spec slots without violating PMR
// ownership rules.  The builder does NOT allocate from a custom memory
// resource; it just mutates fields on the parent's `std::vector<…>` of
// pending specs.
//
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/text/text_run.hpp>

#include <string>
#include <memory>
#include <utility>
#include <vector>

namespace chronon3d {

class LayerBuilder;
class FontEngine;  // forward decl

// ═══════════════════════════════════════════════════════════════════════════
// TextRunPendingSpec — a single pending `LayerBuilder::text_run(...)` entry.
//
// Stored by `LayerBuilder::m_text_runs` (a vector of unique_ptr so the
// references TextRunBuilder hands back stay stable across push_backs).
// Read by `LayerBuilder::build()` to materialize a `RenderNode` with
// `is_text_run_shape = true`.
//
// `params.animators` accumulates both implicit mutator-driven animators
// (added by TextRunBuilder per setter) and explicit user-supplied
// `.animator(...)` / `.selector(...)` calls.
//
// `consumed` is reserved for explicit-commit semantics — at present the
// build path treats every entry as ready.
// ═══════════════════════════════════════════════════════════════════════════

struct TextRunPendingSpec {
    std::string name;
    TextRunSpec pending;     // canonical composable spec; renamed from `spec` to `pending` to disambiguate from the TextRunBuilder class accessor `spec()` and member `m_spec`.
    FontEngine* font_engine{nullptr};  // per-spec engine override; falls back to LayerBuilder::m_font_engine at build() (PR 2 — was previously only on the TextRunBuilder member, never propagated).
    bool consumed{false};
};

// Backwards-compatible alias for callers that still use the original name.
// `TextRunBuildSpec` was the pre-PR-2 name; `TextRunPendingSpec` is the
// canonical name post-PR-2.  All in-tree code uses `TextRunPendingSpec`.
using TextRunBuildSpec = TextRunPendingSpec;

// ═══════════════════════════════════════════════════════════════════════════
// Text-run materialization helper (free function, shared by
// LayerBuilder::build() and RenderNodeFactory::text_run).
//
// Resolves a TextRunParams into a TextRunShape by:
//   1. Shape via FontEngine (cached when cache_layout=true)
//   2. Resolve glyph positions via resolve_placed_glyph_run()
//   3. Build unit map
//   4. Evaluate the animator stack at `sample_time`
//   5. Materialize into a `std::shared_ptr<TextRunShape>`
//
// Returns nullptr on shaping failure (with spdlog::warn diagnostic).
// The caller decides the world-transform / layer placement.
// ═══════════════════════════════════════════════════════════════════════════

class RenderNode;
struct TextRunBindingsContext;  // forward to avoid circle

/// Pure materializer — produces a TextRunShape (layout + resolved
/// glyph states) for a given TextRunParams + SampleTime + FontEngine.
/// No RenderNode involvement; safe to use from any backend.
struct SampleTime;
class FontEngine;
struct TextRunLayout;
struct TextRunShape;
struct TextRunSpec;  // TextRunParams was the prior 30-field struct; merged into composable TextRunSpec via PR.


class TextRunBuilder {
public:
    // ── Constructor ──────────────────────────────────────────────────
    /// Builds a TextRunBuilder attached to `parent`, holding a
    /// non-owning pointer to `spec` (owned by LayerBuilder::m_text_runs).
    /// Public constructor (was private + friend) to avoid unity-build
    /// friend-graph resolution issues.
    TextRunBuilder(LayerBuilder* parent, TextRunPendingSpec* spec);

    // ── Per-glyph transform mutators (inject implicit TextAnimatorSpec) ──
    TextRunBuilder& position(Vec3 v);
    TextRunBuilder& opacity(f32 v);
    TextRunBuilder& anchor(Vec3 a);
    TextRunBuilder& rotate(Vec3 euler_deg);
    TextRunBuilder& scale(Vec3 s);
    TextRunBuilder& font_size(f32 v);
    TextRunBuilder& blur(f32 radius);
    TextRunBuilder& tracking(f32 px);
    TextRunBuilder& baseline_shift(f32 px);

    // ── Explicit user-supplied animators / selectors ──
    /// Append a TextAnimatorSpec to the spec's evaluator stack.
    TextRunBuilder& animator(TextAnimatorSpec spec);

    /// Append a GlyphSelectorSpec to the LAST animator's selector list.
    /// PR 3 — if NO animator exists yet (chained from `.text_run(...)`
    /// directly), the selector is queued in `m_pending_selectors` and
    /// will be PREPENDED to the next explicit `.animator(...)` call's
    /// selector list.  This unifies both patterns:
    ///
    ///   `.selector(s).animator(a)`  → s queues, drains onto a.selectors.
    ///   `.animator(a).selector(s)`  → s is appended to a.selectors
    ///                                  (no phantom animator created).
    ///   `.position(p).selector(s)`  → s attaches to position's implicit
    ///                                  animator (the last/current one).
    TextRunBuilder& selector(GlyphSelectorSpec spec);

    // ── Meta ──
    TextRunBuilder& font_engine(FontEngine* engine);
    TextRunBuilder& cache_layout(bool value);
    TextRunBuilder& name(std::string n); // re-name the entry post-hoc

    // ── Commit ──
    /// Resolve the implicit + explicit animators into the pending
    /// `TextRunPendingSpec` and return the parent `LayerBuilder&`.  Implicit
    /// auto-commit on `LayerBuilder::build()` does the same thing.
    LayerBuilder& commit();

    // ── Read-only accessors ──
    [[nodiscard]] const TextRunSpec& spec() const noexcept { return m_spec->pending; }
    [[nodiscard]] LayerBuilder& parent() const noexcept { return *m_parent; }
    /// Full wrapper access — exposes `name`, `pending.*`, `font_engine` (PR 2), `consumed`.
    /// Used to observe per-spec FontEngine overrides from tests and introspection.
    [[nodiscard]] const TextRunPendingSpec& build_spec() const noexcept { return *m_spec; }

private:
    // `friend class LayerBuilder;` was REMOVED — the declaration now
    // lives one block above as a public ctor.  See the rationale on
    // that declaration: the friend model failed in Unity-build TU
    // boundaries even when both classes were complete.
    // `append_animator` and `make_global_glyph_selector` remain
    // private; they're only called from this class's own methods
    // (no external friend needed).

    /// Append a fully-formed TextAnimatorSpec to the spec's animator
    /// vector.  Internal helper that both `.animator()` and the
    /// implicit per-mutator animator injection use.
    void append_animator(TextAnimatorSpec spec);

    /// A 0..100 (always-on) global selector for implicit mutators.
    /// Every glyph receives weight=1 for properties set via mutators
    /// other than `.animator()` / `.selector()`.
    [[nodiscard]] static GlyphSelectorSpec make_global_glyph_selector(std::string id);

    LayerBuilder*    m_parent;
    TextRunBuildSpec* m_spec;        // non-owning pointer into LayerBuilder::m_text_runs
    // NB: per-PR-2, `m_font_engine` and `m_cache_layout` have been REMOVED
    // from this class.  Builder mutators now write directly into
    // `m_spec->font_engine` and `m_spec->pending.cache_layout` so that
    // state stays correct even when `LayerBuilder::build()` auto-commits
    // without a prior explicit `.commit()` call.
    // Counter to give each implicit animator a unique diagnostics id.
    int m_implicit_id_seq{0};
    // Pending selectors accumulated via `.selector()` before the next
    // `.animator()` call.  Preprended to the animator's selector list on append.
    std::vector<GlyphSelectorSpec> m_pending_selectors;
};

// ═══════════════════════════════════════════════════════════════════════════
// Public free function: materialize a TextRunShape from TextRunSpec
// (`TextRunParams` was the prior deprecated alias of `TextRunSpec`).
//
// Both `LayerBuilder::build()` and `RenderNodeFactory::text_run()`
// consume this same core routine.
//
//   1. Resolve FontEngine (preferred: caller-supplied; fallback:
//      `&shared_font_engine()`).
//   2. Shape text via FontEngine::shape_text() (cached via
//      `shared_text_layout_cache()` when `params.cache_layout=true`).
//   3. Resolve glyph placements via `resolve_placed_glyph_run()`.
//   4. Build unit map.
//   5. Evaluate `params.animators` at `sample_time`.
//   6. Materialize `std::shared_ptr<TextRunShape>` with paint,
//      material, shadows populated.
// 7. Returns nullptr (with spdlog::warn) on shaping failure.
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::shared_ptr<TextRunShape> materialize_text_run_shape(
    const TextRunParams& params,
    FontEngine* engine,
    SampleTime sample_time
);

} // namespace chronon3d
