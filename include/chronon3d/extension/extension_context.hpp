#pragma once

// ============================================================================
// extension_context.hpp — Context passed to ExtensionModule::register_all().
//
// PR 2: Explicit registration.  Each ExtensionModule receives this context
// and registers its factories into the appropriate registries without
// reaching into static globals or singletons.
//
// The host (CLI, external program, or test) creates a CompositionRegistry
// and passes it here.  Extension modules inject their compositions and
// graph nodes via the typed references in this struct.
// ============================================================================

namespace chronon3d {

class CompositionRegistry;
class AssetRegistry;

namespace graph {
    class GraphNodeCatalog;
}

namespace effects {
    class EffectCatalog;
}

/// Context passed to every ExtensionModule during registration.
///
/// Each reference points to a registry owned by the host.
/// Extension modules are expected to call add() on the appropriate
/// registry — `register_all()` is called during the single-threaded
/// startup phase, so no locking is needed here.
struct ExtensionContext {
    CompositionRegistry&      compositions;
    graph::GraphNodeCatalog&  graph_nodes;
    effects::EffectCatalog&   effects;
    AssetRegistry&            assets;
};

} // namespace chronon3d
