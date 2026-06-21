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
// without holding a Session*" can use `chronon3d::runtime::session_session_
// services(session)` (declared in render_runtime.hpp) instead of
// reaching directly into the field.
// ----------------------------------------------------------------------

namespace chronon3d::cache { class NodeCache; class FramebufferPool; }
namespace chronon3d::graph {
    class GraphExecutor;
    class CompiledGraphCache;
    struct SceneHasher;
    class  SceneProgramStore;
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
    /// WP-8 follow-up — pointer to runtime-owned scene hasher
    /// (was a value member on RenderSession via render_session.hpp's
    /// scene_hasher.hpp include; relocated to runtime).  Non-owning.
    chronon3d::graph::SceneHasher*           scene_hasher{nullptr};
    /// WP-8 follow-up — pointer to runtime-owned program store
    /// (was a unique_ptr member on RenderSession; relocated to runtime).
    /// Non-owning.
    chronon3d::graph::SceneProgramStore*    program_store{nullptr};
};

} // namespace chronon3d::runtime
