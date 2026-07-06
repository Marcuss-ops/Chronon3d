// ─── src/registry/text_preset_register_helpers.hpp ──────────────────────────
//
// FASE 5 reviewer-fixup (TICKET-107) — promoted per-category register
// helpers from the file-local anonymous namespace in
// `src/registry/text_preset_registry.cpp` into an internal namespace
// (`chronon3d::registry::register_helpers_internal`) so tests can
// exercise each category in isolation.
//
// The 22 entry() factories + 22 compose_<id>() helpers stay colocated
// in the registry impl TU and remain anon-namespace internal.  The 4
// narrow per-category register functions are reachable from tests
// (and any sibling internal TU) by including this header.
//
// Freeze compliance (AGENTS.md v0.1): this header lives under
// `src/registry/` and is NOT installed.  No public API surface in
// `include/chronon3d/` is touched.
//
// Usage (test TU):
//     #include "src/registry/text_preset_register_helpers.hpp"
//     using chronon3d::registry::register_text_preset_cinematic;
//     chronon3d::registry::TextPresetRegistry r;
//     register_text_preset_cinematic(r);
//     CHECK(r.by_category(chronon3d::registry::TextPresetCategory::Cinematic).size() == 4);
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <chronon3d/registry/text_preset_registry.hpp>

namespace chronon3d {
namespace registry {

// ── Internal helper namespace ──────────────────────────────────────────────
//
// Marked `register_helpers_internal` to signal "internal-only registration
// surface — do NOT call from public consumers".  Tests CAN include this
// header (and link against the chronon3d_registry target) to exercise
// per-category seeding in isolation without bringing in the full
// `register_builtin_presets(r)` umbrella.
namespace register_helpers_internal {

/// Seed only the Cinematic category (4 entries) into `r`.
/// Idempotent: throws on duplicate-id (per `TextPresetRegistry::register_preset`
/// contract) so callers can guard on `r.contains(id)` before invoking.
void register_text_preset_cinematic(TextPresetRegistry& r);

/// Seed only the Reveal category (10 entries) into `r`.
void register_text_preset_reveal(TextPresetRegistry& r);

/// Seed only the Emphasis category (4 entries) into `r`.
void register_text_preset_emphasis(TextPresetRegistry& r);

/// Seed only the Subtitle category (4 entries) into `r`.
void register_text_preset_subtitle(TextPresetRegistry& r);

/// Seed all 22 built-in entries (delegates to the 4 per-category helpers,
/// preserving Cinematic → Reveal → Emphasis → Subtitle insertion order).
/// This is the umbrella previously declared in
/// `src/registry/text_preset_registry.cpp` anon namespace; now reachable
/// from sibling TUs (tests, internal authoring façades) through this
/// header.
void register_builtin_presets(TextPresetRegistry& r);

// ── Per-category factory forward-declaration (M1.5#13, 1/4) ────────────
//
// The `text_preset_factories_<category>.cpp` source TUs each define an
// external-linkage function `create_text_presets()` returning
// `std::vector<TextPresetDescriptor>` for their respective category.
// The factory TUs are descriptor-only by design (no auto-register —
// the canonical category-bridge `register_text_preset_<category>(r)`
// consumes the returned vector and forwards each descriptor to
// `r.register_preset(...)`).  Forward-declared here so each
// `text_preset_registry.cpp` consumer sees the symbol without
// per-TU declaration duplication.  Step 1/4 ships only `factory_basic`;
// Steps 2/3/4 add the corresponding declarations.
//
// ODR note: each per-category TU exposes `create_text_presets()` in
// its OWN nested namespace (`factory_basic` / `factory_kinetic` /
// `factory_cinematic` / `factory_social`).  The nested-namespace
// declaration below uses C++17 inline-nested syntax to avoid ODR
// collision across TUs.
namespace factory_basic {

/// Per-category factory surface (basic = Subtitle category).
/// Returns 4 entries: `minimal_white`, `yellow_keyword`, `glow_pulse`,
/// `caption_box` (canonical Subtitle insertion order).
/// Defined in `src/registry/text_preset_factories_basic.cpp`.
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets();

} // namespace factory_basic

namespace factory_reveal {

/// Per-category factory surface (reveal = Reveal category).
/// Returns 10 entries in canonical Reveal insertion order.
/// Defined in `src/registry/text_preset_factories_reveal.cpp`.
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets();

} // namespace factory_reveal

namespace factory_cinematic {

/// Per-category factory surface (cinematic = Cinematic category).
/// Returns 4 entries in canonical Cinematic insertion order.
/// Defined in `src/registry/text_preset_factories_cinematic.cpp`.
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets();

} // namespace factory_cinematic

} // namespace register_helpers_internal

// ── Public re-exports — DELIBERATELY OMITTED (reviewer finding #6) ────────
//
// The 4 per-category helpers + the umbrella are NOT re-exported at the
// parent `chronon3d::registry` namespace.  Tests / sibling internal TUs
// reach them through the verbose `register_helpers_internal::` path:
//
//     chronon3d::registry::register_helpers_internal::register_text_preset_cinematic(r);
//
// or via a namespace alias at the call site:
//
//     namespace reg_helpers = chronon3d::registry::register_helpers_internal;
//     reg_helpers::register_text_preset_cinematic(r);
//
// Rationale: re-exports at the parent `chronon3d::registry` namespace
// widen the public surface (any TU that includes this header would
// expose `chronon3d::registry::register_text_preset_cinematic` as a
// callable symbol).  AGENTS.md v0.1 freeze prohibits public API surface
// expansion.  The verbose path is mildly less ergonomic but keeps the
// internal-only intent explicit at the call site.
//

} // namespace registry
} // namespace chronon3d
