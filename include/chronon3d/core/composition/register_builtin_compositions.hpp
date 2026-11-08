#pragma once

// ---------------------------------------------------------------------------
// register_builtin_compositions.hpp
//
// PR 2: Explicit registration — each function receives a CompositionRegistry&
// instead of pushing into a global static vector.
//
// Call register_builtin_compositions(registry) once at startup to populate
// non-content built-in composition (DarkGridBackground).
// ---------------------------------------------------------------------------

namespace chronon3d {

class CompositionRegistry;

/// Register DarkGridBackground (src/scene/utils/dark_grid_background.cpp).
void register_dark_grid_background(CompositionRegistry& registry);

/// Register HdrBokehFocusPull
/// (src/scene/utils/hdr_bokeh_focus_pull.cpp).
void register_hdr_bokeh_focus_pull(CompositionRegistry& registry);

/// Register the non-content built-in composition.
/// Safe to call multiple times — CompositionRegistry rejects duplicates.
inline void register_builtin_compositions(CompositionRegistry& registry) {
    register_dark_grid_background(registry);
    register_hdr_bokeh_focus_pull(registry);
}

} // namespace chronon3d
