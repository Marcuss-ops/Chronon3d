// ==============================================================================
// src/registry/animator_resolver.cpp — AnimatorResolver static bodies
//
// Phase-3.2 refactor.  The three static member bodies (spec_is_rich,
// rich_paint_anchor, compose_for) that previously lived inline in
// include/chronon3d/registry/animator_resolver.hpp now live here so
// the public header is a pure declaration contract.
//
// ## TEXT-RES-01 refactor (this commit)
// `compose_for(preset_id)` is now an O(log n) registry lookup + dispatcher
// (5 lines).  There is NO per-id ELSE-IF table in this TU anymore.  The
// hardcoded 22-branch dispatch moved to per-entry `animator_factory`
// closures stored in `TextPresetDescriptor` (see
// src/registry/text_preset_registry.cpp).
//
// Anti-duplication-guardrail: this TU DOES NOT maintain its own per-id
// table — it queries the canonical `builtin_text_preset_registry()`
// accessor from `text_preset_registry.hpp`.  Adding a 23rd preset
// requires ZERO changes here.  See docs/ANTI_DUPLICATION_RULES.md
// §registry/resolver.
//
// IMPORTANT — anti-circular dependency
//   This .cpp is part of the chronon3d_registry OBJECT library.
//   It must NOT include any `content/text/text_*.hpp` or any
//   other TU that drags content-side state.  The canonical TextSpec
//   type is pulled in via the single include of
//   `<chronon3d/registry/animator_resolver.hpp>`, which the header
//   already declared #include-ing
//   `<chronon3d/scene/builders/builder_params.hpp>`.
// ==============================================================================
#include <chronon3d/registry/animator_resolver.hpp>  // struct AnimatorResolver + types from builder_params.hpp.
#include <chronon3d/registry/text_preset_registry.hpp>  // TEXT-RES-01 — canonical accessor
                                                        // `builtin_text_preset_registry()` from
                                                        // the registry TU (the SINGLE source
                                                        // of truth for per-id mappings).

namespace chronon3d::registry {

// ── spec_is_rich — Stage 4 contract (unchanged from Phase 3.2) ─────────────
//
// Returns true when the caller-authored TextSpec carries at least one
// "richly-painted" signal (stroke_enabled OR fill_style OR stroke_style).
// This is the wire-up trigger that the cinematic factories use to
// push a `ctc_rich_<preset_id>` TextAnimatorSpec onto the TextRun BEFORE
// the canonical motion chain runs.
bool AnimatorResolver::spec_is_rich(const TextSpec& spec) noexcept {
    return spec.appearance.paint.stroke_enabled
        || spec.appearance.paint.fill_style.has_value()
        || spec.appearance.paint.stroke_style.has_value();
}

// ── rich_paint_anchor — Stage 4 contract (unchanged from Phase 3.2) ────────
//
// Returns the canonical rich-paint TextAnimatorSpec anchor — pushed onto
// the TextRun when the spec is rich (see spec_is_rich).  The id is
// `ctc_rich_<preset_id>` so the Stage-4 deterministic probe in
// test_text_preset_registry.cpp can correlate the wiring.
TextAnimatorSpec AnimatorResolver::rich_paint_anchor(std::string_view preset_id) {
    TextAnimatorSpec a;
    a.id             = std::string{"ctc_rich_"} + std::string{preset_id};
    a.enabled        = true;
    a.transform_mode = TextPropertyBlendMode::Add;
    a.color_mode     = TextPropertyBlendMode::Replace;

    // Global glyph selector — every glyph receives weight=1 (the
    // After Effects-style "entire text as one unit" pattern).  The
    // id on the selector is the parent animator's id with a
    // `_sel_global` suffix so diagnostics can correlate the two.
    GlyphSelectorSpec sel;
    sel.id    = a.id + "_sel_global";
    sel.unit  = TextSelectorUnit::Glyph;
    sel.shape = TextSelectorShape::Square;
    sel.start = {0.0f};
    sel.end   = {100.0f};
    sel.amount = {100.0f};
    a.selectors.push_back(sel);

    // No-op-at-render property: OpacityProperty{1.0f} keeps the
    // glyphs at the standard full opacity (1.0) so the resolver
    // entry is observable AND semantically full (it composes with
    // the canonical motion-preset canon via `evaluate_animator_stack`
    // rather than id-only).
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// ── compose_for — Stage 5 (Cluster A DoD #2 — registry-only dispatcher) ────
//
// TEXT-RES-01 query path.  Three lines of business logic;
// everything that previously lived here as a 22-branch ELSE-IF chain
// now lives in per-entry `animator_factory` closures inside
// `TextPresetDescriptor` (see text_preset_registry.cpp).
//
// Returns std::nullopt for any one of:
//   1. Unknown preset id (not in `builtin_text_preset_registry()`);
//   2. Preset with no canonical motion (e.g. `minimal_white`,
//      whose `animator_factory` returns std::nullopt internally);
//   3. Preset whose `animator_factory` field is null (defensive —
//      should never happen for the 22 built-ins but the check is
//      cheap).
//
// Performance: each invocation is
//   (a) one std::map::contains(O(log n));
//   (b) one std::map::find(O(log n));
//   (c) one std::function::operator() (inlinable for non-capturing
//       closures — most `compose_*` helpers qualify).
// The pre-TEXT-RES-01 hot path was a 22-branch ELSE-IF chain, also
// O(1) but with a much larger compiled footprint; the registry path
// has the same asymptotic cost on the hot path with a smaller binary
// AND a single source of truth (the registry).
std::optional<TextAnimatorSpec>
AnimatorResolver::compose_for(std::string_view preset_id) {
    const TextPresetRegistry& r = builtin_text_preset_registry();
    if (!r.contains(preset_id)) return std::nullopt;
    const TextPresetDescriptor& d = r.get(preset_id);
    if (!d.animator_factory) return std::nullopt;
    return d.animator_factory(d.metadata);
}

} // namespace chronon3d::registry
