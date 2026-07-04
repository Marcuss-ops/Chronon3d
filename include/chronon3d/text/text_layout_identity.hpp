// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#3 — text_layout_identity.hpp
//
// Single-responsibility sub-header for the "identity" semantic slot.
// Lives next to text_run_layout.hpp, text_run_shape.hpp,
// text_layout_cache.hpp, text_run_hash.hpp under include/chronon3d/text/.
//
// This header is currently a thin re-export of font_engine.hpp.  The
// underlying types (FontIdentity, FontLayoutIdentity, and the
// `font_layout_identity_of` projection family) live in
// font_engine.hpp because that's where font-side types semantically
// belong; this sub-header gives callers a stable import path
// consistent with the other M1.5#3 sub-headers (layout / shape /
// cache / hash).
//
// No new public types are introduced here, and no canonical header is
// de-duplicated.  This re-export preserves the existing visibility
// contract of `text_run.hpp` post-umbrella conversion: every TU that
// used to #include <chronon3d/text/text_run.hpp> and reached
// FontFamilyIdentity via that path still does, transitively, via
// text_run.hpp → text_layout_identity.hpp → font_engine.hpp.
//
// Future evolution: a follow-up M1.5 sub-ticket (DEFERRED post-baseline
// verde) may physically move the FontIdentity / FontLayoutIdentity
// type definitions from font_engine.hpp to this header and re-include
// them from font_engine.hpp to keep ABBR-side consumers compiling.
// Until then this header is a documentation slot + include re-export.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/font_engine.hpp>

namespace chronon3d {
// Re-exported through transitive include — no namespace pollution;
// consumers that need these symbols can explicitly state a deeper
// dependency in their own .hpp/.cpp to avoid surprise refactors later.
} // namespace chronon3d
