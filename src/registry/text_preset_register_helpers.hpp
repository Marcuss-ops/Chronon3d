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
