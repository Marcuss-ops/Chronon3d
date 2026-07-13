// ─── src/registry/text_preset_factories_reveal.cpp ──────────────────────────
//
// FASE 1 Step 1 → split (Stage 1) — Reveal-category text-preset factory TU,
// THIN AGGREGATOR.  Originally 518 LoC with 10 entry() functions colocated
// with the `create_text_presets()` return site; split into 2 sub-buckets:
//
//   • `text_preset_factories_reveal_basic.cpp`     → 6 entrance-animation
//     presets (text_animations / fade_in / blur_in / slide_up / slide_down /
//     scale_in), exposed as `create_basic_reveal_presets()`.
//   • `text_preset_factories_reveal_selectors.cpp` → 4 selector-driven
//     presets (tracking_close / masked_line_reveal / word_cascade /
//     character_cascade), exposed as `create_selector_reveal_presets()`.
//
// This aggregator preserves the canonical Reveal insertion order
// (basic precedes selectors → 6 + 4 = 10 descriptors in stable order).
// The per-category factory surface (`create_text_presets()`) is unchanged
// so the consumer `src/registry/text_preset_registry.cpp:202` keeps
// compiling un-modified.
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//
//  (1) SINGLE central registry.  The aggregator here is descriptor-only:
//      it returns `std::vector<TextPresetDescriptor>` from
//      `create_text_presets()`; the canonical category-bridge
//      `register_text_preset_reveal(r)` consumes the returned vector and
//      forwards each descriptor to `r.register_preset(...)`.
//
//  (2) Sub-bucket factories are sibling TU-level declarations inside the
//      SAME `factory_reveal` namespace as the aggregator.  Distinct
//      symbol names (`create_basic_reveal_presets` vs
//      `create_selector_reveal_presets` vs `create_text_presets`) avoid
//      ODR collision inside the shared namespace.
//
//  (3) Freeze compliance: no public API surface in `include/chronon3d/`
//      is touched — the forward declarations live TU-locally here so the
//      aggregator file is the single internal binding point.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/registry/text_preset_descriptor.hpp>
// TextPresetDescriptor (return type only; selection logic delegated to
// the two sub-bucket TUs which own the entry() factory bodies).

#include <vector>

namespace chronon3d::registry::register_helpers_internal::factory_reveal {

// ── Sub-bucket forward declarations ────────────────────────────────────────
//
// These symbols are defined in the sibling TUs that live in the same
// `factory_reveal` namespace.  Forward-declared TU-locally so this
// aggregator remains the SINGLE internal binding point and no header
// surface is widened.

[[nodiscard]] std::vector<TextPresetDescriptor> create_basic_reveal_presets();
[[nodiscard]] std::vector<TextPresetDescriptor> create_selector_reveal_presets();

// ── public factory surface (Reveal category, aggregator) ──────────────────
//
// `create_text_presets()` returns the 10 Reveal-category descriptors in
// canonical insertion order: 6 BASIC (sub-bucket 1) followed by 4
// SELECTORS (sub-bucket 2).  The total count + order is preserved
// byte-identical to the pre-split implementation.
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets() {
    std::vector<TextPresetDescriptor> out = create_basic_reveal_presets();
    const auto selectors = create_selector_reveal_presets();
    out.insert(out.end(), selectors.begin(), selectors.end());
    return out;
}

} // namespace chronon3d::registry::register_helpers_internal::factory_reveal
