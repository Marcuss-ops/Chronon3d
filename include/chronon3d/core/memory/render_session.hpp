#pragma once

// ---------------------------------------------------------------------------
// core/memory/render_session.hpp
//
// DEPRECATED forwarding location.
//
// After the RenderSession extraction refactor (architecture plan section
// 8.2), the canonical header for `chronon3d::RenderSession` is
// `<chronon3d/runtime/render_session.hpp>` in the `chronon3d_runtime`
// CMake target.  This file is kept as a forwarding include so that
// legacy code paths (e.g. `<chronon3d/core/memory/render_session.hpp>`
// written before phase 3) continue to compile unchanged.
//
// New code SHOULD include `<chronon3d/runtime/render_session.hpp>`
// directly.  This stub will be REMOVED in a future cleanup phase.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/render_session.hpp>
