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
namespace chronon3d::graph { class GraphExecutor; class CompiledGraphCache; }
namespace chronon3d { class AssetRegistry; }

namespace chronon3d::runtime {
class ExecutionPlanCache;
}

namespace chronon3d::runtime {

struct SessionServices {
    chronon3d::graph::GraphExecutor*         executor{nullptr};
    chronon3d::runtime::ExecutionPlanCache*  plan_cache{nullptr};
    chronon3d::cache::NodeCache*             node_cache{nullptr};
    chronon3d::cache::FramebufferPool*       framebuffer_pool{nullptr};
    chronon3d::graph::CompiledGraphCache*    graph_cache{nullptr};
    chronon3d::AssetRegistry*                asset_registry{nullptr};
    /// Pointer to the runtime's `default_assets_root`.  Non-owning;
    /// valid for the lifetime of the runtime.
    const std::string*                       default_assets_root{nullptr};
};

} // namespace chronon3d::runtime
