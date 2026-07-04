// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#3 — text_run.hpp is now an UMBRELLA header.
//
// Single 409-LOC canonical header decomposed into five single-responsibility
// sub-headers under `include/chronon3d/text/`.  No public types were
// added, removed, or renamed; every existing
// `#include <chronon3d/text/text_run.hpp>` keeps the same visibility as
// before (verified across 28 downstream TUs).
//
// Sub-headers:
//   1. text_layout_identity.hpp — FontIdentity + FontLayoutIdentity
//                                  (re-export from font_engine.hpp until
//                                  the M1.5 sub-ticket physically moves
//                                  the type definitions into this slot).
//   2. text_run_layout.hpp     — TextRunLayout + SharedTextRunLayout +
//                                  ShapedFontSpan + Bcp47LanguageTag +
//                                  TextShapingFeatures.
//   3. text_layout_cache.hpp    — TextLayoutCacheKey + TextLayoutCacheKeyHash
//                                  + TextLayoutCache class.
//   4. text_run_shape.hpp      — TextRunShape (PR 8/9/B3/11 fields) +
//                                  forward decls for AnimatedTextDocument
//                                  + TextLayoutCache.
//   5. text_run_hash.hpp       — hash_text_run_shape overload declarations
//                                  (bodies remain in src/text/text_run.cpp).
//
// Dependency order rationale:
//   - text_layout_identity  ← depends only on font_engine.hpp
//   - text_run_layout       ← depends on FontSpec / FontIdentity / PlacedGlyphRun / TextUnitMap
//   - text_layout_cache     ← depends on SharedTextRunLayout + Bcp47LanguageTag
//   - text_run_shape        ← depends on SharedTextRunLayout + TextLayoutCache*
//                              (forward-decl OK to keep impl lighter)
//   - text_run_hash         ← depends on TextRunShape + Frame
//
// Fase B3 status (carried-forward from pre-M1.5#3): the process-wide
// global `shared_text_layout_cache()` is REMOVED from public API.  All
// call sites route through `TextLayoutCache*` parameters on the public
// driver functions (`update_text_run_shape_per_frame`,
// `apply_active_state_to_text_run_shape`,
// `prewarm_text_run_layout_for_frame`) or via the per-shape
// `TextRunShape::cache` slot.  Verified zero residual references
// (canonical checker: tools/check_architecture_boundaries.sh).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_layout_identity.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <chronon3d/text/text_layout_cache.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/text/text_run_hash.hpp>
