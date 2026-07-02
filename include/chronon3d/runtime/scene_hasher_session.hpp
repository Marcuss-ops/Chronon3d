#pragma once

// ---------------------------------------------------------------------------
// runtime/scene_hasher_session.hpp
//
// Per-session SceneHasher.
//
// Per design decision, SceneHasher is NOT a member of `RenderSession.common`
// and NOT a member of `SoftwareSessionResources`.  It lives "a parte" as
// its own scaffolding header, so that callers and tests can refer to a
// stable `SceneHasherSession` name.  Today the type is a thin alias over
// the canonical `graph::SceneHasher` defined in
// `<chronon3d/internal/render_graph/core/scene_hasher.hpp>`.
//
// In the future this header is the right place to introduce any
// session-local cache (e.g. memoized fingerprints) that should remain on
// the render job, without polluting either RenderSession or the
// software-specific resources.
// ---------------------------------------------------------------------------

#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>

namespace chronon3d {

/// Per-session scene hasher.
///
/// Re-exported as a stable name distinct from `RendererSession::scene_hasher`
/// so that callers can begin referring to a session-scoped type without
/// depending on `RenderSession.common`.
using SceneHasherSession = ::chronon3d::graph::SceneHasher;

} // namespace chronon3d
