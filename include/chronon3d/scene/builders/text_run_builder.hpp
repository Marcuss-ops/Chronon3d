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
// `TextRunSpec` and returns the parent `LayerBuilder&` so layer-level
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
// TextRunSpec — a single pending `LayerBuilder::text_run(...)` entry.
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
// build path treats every entry as ready, so consumption tracking is a
// no-op but preserved so PR 5+ can add deferred-commit semantics.
// ═══════════════════════════════════════════════════════════════════════════

struct TextRunSpec {
    std::string name;
    TextRunParams params;
    bool consumed{false};
};

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
struct TextRunParams;


class TextRunBuilder {
public:
    // ── Constructor ──────────────────────────────────────────────────
    /// Builds a TextRunBuilder attached to `parent`, holding a
    /// non-owning pointer to `spec` (owned by LayerBuilder::m_text_runs).
    /// Public constructor (was private + friend) to avoid unity-build
    /// friend-graph resolution issues.
    TextRunBuilder(LayerBuilder* parent, TextRunSpec* spec);

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
    /// If no animator has been added yet, implicitly creates one with
    /// an empty property list (so the selector remains attachable to
    /// a follow-up `.animator()` call).
    TextRunBuilder& selector(GlyphSelectorSpec spec);

    // ── Meta ──
    TextRunBuilder& font_engine(FontEngine* engine);
    TextRunBuilder& cache_layout(bool value);
    TextRunBuilder& name(std::string n); // re-name the entry post-hoc

    // ── Commit ──
    /// Resolve the implicit + explicit animators into the pending
    /// `TextRunSpec` and return the parent `LayerBuilder&`.  Implicit
    /// auto-commit on `LayerBuilder::build()` does the same thing.
    LayerBuilder& commit();

    // ── Read-only accessors ──
    [[nodiscard]] const TextRunParams& spec() const noexcept { return m_spec->params; }
    [[nodiscard]] LayerBuilder& parent() const noexcept { return *m_parent; }

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

    LayerBuilder* m_parent;
    TextRunSpec*  m_spec;             // non-owning pointer into LayerBuilder::m_text_runs
    FontEngine*   m_font_engine{nullptr};
    bool          m_cache_layout{true};
    // Counter to give each implicit animator a unique diagnostics id.
    int m_implicit_id_seq{0};
    // Pending selectors accumulated via `.selector()` before the next
    // `.animator()` call.  Preprended to the animator's selector list on append.
    std::vector<GlyphSelectorSpec> m_pending_selectors;
};

// ═══════════════════════════════════════════════════════════════════════════
// Public free function: materialize a TextRunShape from TextRunParams.
//
// Both `LayerBuilder::build()` (PR 4 commit step) and
// `RenderNodeFactory::text_run()` (PR 4 shape-registry route) consume
// this same core routine.  Keeping it in one place ensures the
// text_run registry is identical to text_run builder output (modulo
// sample time).
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
