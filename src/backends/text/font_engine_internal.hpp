// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/font_engine_internal.hpp
//
// INTERNAL supplementary header for FontEngine internals.
// NOT part of the public ABI — lives in src/backends/text/ (no include/
// chronon3d/ propagation). Forward declarations + friend access live in
// the public header to grant this free function access to FontEngine's
// `m_impl`; the symbol itself is in `chronon3d::text::font_engine_internal`
// and is intended for internal resolver code only (e.g. FontFallbackResolver).
//
// Cat-5 compliance: the symbol is NOT a member of FontEngine, so adding it
// here does NOT expand the public ABI (no new public method on FontEngine).
// The forward declaration + friend declaration required in the public header
// are minimal access mechanisms and live in the `font_engine_internal`
// sub-namespace, clearly segregated from the public-facing FontEngine API.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace chronon3d {

class FontEngine;
struct FontSpec;

} // namespace chronon3d

namespace chronon3d::text::font_engine_internal {

/// Internal-only: probe whether the given font provides a glyph for the
/// given Unicode codepoint. Used by the cluster-level fallback resolver
/// (src/text/resolver/font_fallback_resolver.cpp) to group consecutive
/// codepoints by covered font without shaping. Loads/caches the font via
/// the same shared_mutex dance as FontEngine::can_load.
///
/// @return true iff the font has a non-zero glyph for `codepoint`.
[[nodiscard]] bool has_glyph_for_codepoint(
    FontEngine&        engine,
    const FontSpec&    spec,
    char32_t            codepoint
);

/// Internal-only: load/cache the face for `spec` and return its metadata
/// via the out-parameters. Used by the bundled fallback scanner to fill
/// family/style/weight for fonts discovered from disk.
///
/// @return true iff the font was loadable. On false the out-parameters
///         are left untouched.
[[nodiscard]] bool inspect_font(
    FontEngine&     engine,
    const FontSpec& spec,
    std::string&    out_family,
    std::string&    out_style,
    int&            out_weight
);

} // namespace chronon3d::text::font_engine_internal
