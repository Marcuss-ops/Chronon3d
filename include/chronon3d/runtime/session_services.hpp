#pragma once

// ----------------------------------------------------------------------
// runtime/session_services.hpp
//
// Typed back-pointer bundle bound to a `RenderSession`.  Each field
// is non-owning; lifetime is the runtime's responsibility (i.e. the
// RenderRuntime outlives every SoftwareRenderSession referencing it
// via this record).
//
// Why this lives in its own header (not inside render_runtime.hpp):
// render_session.hpp needs the FULL type definition to hold a
// `runtime::SessionServices services` field by-value inside the
// engine-generic RenderSession.  A central header avoids the
// render_runtime ↔ render_session include cycle.
//
// Future code that needs to consult "what services back this session
// without holding a Session*" can use `chronon3d::runtime::session_services(session)`
// (declared in render_runtime.hpp) instead of reaching directly into
// the field.
//
// WP-3 PR 3.1 — `scene_hasher` and `program_store` pointer fields
// were REMOVED.  Both state engines are now per-session owned (see
// `RenderSession::scene_hasher` and `RenderSession::program_store`),
// so reaching into them via the SessionServices table is no longer
// the correct approach — callers should now read the session
// directly (`session.scene_hasher()` / `session.program_store()`).
// ----------------------------------------------------------------------

namespace chronon3d::cache { class NodeCache; class FramebufferPool; }
namespace chronon3d::graph {
    class GraphExecutor;
    class CompiledGraphCache;
}
namespace chronon3d { class AssetRegistry; }

namespace chronon3d::runtime {

struct SessionServices {
    chronon3d::graph::GraphExecutor*         executor{nullptr};
    chronon3d::cache::NodeCache*             node_cache{nullptr};
    chronon3d::cache::FramebufferPool*       framebuffer_pool{nullptr};
    chronon3d::graph::CompiledGraphCache*    graph_cache{nullptr};
    chronon3d::AssetRegistry*                asset_registry{nullptr};
    /// Pointer to the runtime's `default_assets_root`.  Non-owning;
    /// valid for the lifetime of the runtime.
    const std::string*                       default_assets_root{nullptr};
    // WP-3 PR 3.1 — `scene_hasher` and `program_store` pointer fields
    // were REMOVED here.  Both state engines are per-session owned.
    // Callers that previously read `services.scene_hasher` /
    // `services.program_store` must now read the session directly.
};

} // namespace chronon3d::runtime
