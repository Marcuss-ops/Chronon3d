#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// content/compositions/chronon_glow_final.hpp
//
// TICKET-CHRONON-GLOW-FINAL — production public surface (decl-only header).
//
// Slimmed from the previous 287-LoC header-only inline TU to a thin
// declarative header + implementations companion `chronon_glow_final.cpp`.
// Single production factory (Step 8 §C, AGENT 2 closure).  All heavy
// implementation detail (GlowLayout, resolve_layout, cinematic-glow preset
// helper, per-frame envelope, inner scene composer) lives in the .cpp as
// anonymous-namespace TU-local symbols.
//
// Public surface (canonical contract — consumers must depend ONLY on these):
//   default_landscape_props()              → AR 16:9 default ChrononGlowProps
//   default_portrait_props()               → AR 9:16 default ChrononGlowProps
//   make_chronon_glow_final(props)         → chronon3d::Composition factory
//
// AGENTS.md v0.1 freeze compliance: zero new public SDK API in include/.
// All surface lives in content/.  Header is decl-only; implementation link
// time adds the corresponding .so/.o target surface on the
// `chronon3d_content` OBJECT library.
//
#include <string>

#include <chronon3d/core/types/frame.hpp>
// chronon3d::f32 type alias (primary numeric wrapper used in
// ChrononGlowProps::glow_strength default + consumers' `props.foo = f32{...}`).

#include <chronon3d/timeline/composition.hpp>
// chronon3d::Composition — return type of make_chronon_glow_final; consumers
// must be able to construct/decompose the returned Composition without
// explicitly including this header themselves.

namespace chronon3d::content::glow_final {

// ── Public default-strength constant ────────────────────────────────────
//
// Kept in this header because `ChrononGlowProps::glow_strength` uses it
// as in-class default initializer (C++ requires the initializer to be a
// constant expression visible at the point of struct definition).
// `inline constexpr` (C++17+) makes the symbol ODR-safe across multiple
// consumer TUs that include this header.
inline constexpr chronon3d::f32 kDefaultGlowStrength =
    chronon3d::f32{0.55f};

// ── Format enum (Step 8 §B — replaces the legacy bool portrait flag) ─────
enum class GlowFormat {
    Landscape,
    Portrait,
};

// ── Composition parameter struct (Step 8 §B — 5 fields) ─────────────────
//
// No `portrait` flag, no `font_size` / `box` / `canvas_size` (those are
// derived from `format` via the TU-local `resolve_layout()` in the .cpp —
// single source of truth; impossible to construct
// `portrait=true, canvas_size={1920,1080}`).
struct ChrononGlowProps {
    std::string            text{"PULSE GLOW"};
    GlowFormat             format{GlowFormat::Landscape};
    bool                   glow_enabled{true};
    chronon3d::f32         glow_strength{kDefaultGlowStrength};
    bool                   scale_breath{true};
};

// ── Public factory surface (definitions live in chronon_glow_final.cpp) ─

/// 16:9 AR default props (PULSE GLOW landscape, glow enabled, breath on).
ChrononGlowProps default_landscape_props();

/// 9:16 AR default props (PULSE GLOW portrait, glow enabled, breath on).
ChrononGlowProps default_portrait_props();

/// Production factory.  Single canonical entry point (Step 8 §C; the
/// legacy `make_chronon_glow_final_for_test(props, Fe&)` has been REMOVED).
chronon3d::Composition make_chronon_glow_final(ChrononGlowProps props);

} // namespace chronon3d::content::glow_final
