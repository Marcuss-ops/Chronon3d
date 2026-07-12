#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// render_graph.hpp — Public forwarding header for chronon3d::graph::RenderGraph
//
// Historical rot fix (2026-07-12): the canonical RenderGraph class lives at
// `include/chronon3d/internal/render_graph/render_graph.hpp` (the `internal/`
// namespace is the project convention for implementation headers that should
// not be used by external SDK consumers). However, ~12+ files in the
// repository include the class via the PUBLIC path
// `<chronon3d/render_graph/render_graph.hpp>` (which never existed), causing
// a hard build rot that blocked the Text V1 golden test re-bake + machine-
// verification on this VPS (and was the root cause of the errors logged in
// the CHANGELOG lineage at text_definition.hpp:170, content/text/
// text_helpers_typewriter.hpp + text_helpers_centered.hpp, src/text/
// text_definition.cpp — all 4 sites transitively include this header
// through compiled_frame_graph.hpp:3).
//
// This forwarding header re-exports the canonical class WITHOUT introducing
// a new public SDK symbol (per AGENTS.md v0.1 Cat-3 anti-duplication: ZERO
// new public symbols, just a convenience include). The actual class
// definition lives at `chronon3d/internal/render_graph/render_graph.hpp`
// and is unchanged.
//
// Anti-duplication: this is a pure re-export, not a duplicate definition.
// The internal path remains the single source of truth.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/internal/render_graph/render_graph.hpp>
