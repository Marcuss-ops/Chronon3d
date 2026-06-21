#pragma once

// ============================================================================
// extension_context.hpp — Context passed to ExtensionModule::register_all().
//
// PR 2: Explicit registration.  Each ExtensionModule receives this context
// and registers its factories into the appropriate registries without
// reaching into static globals or singletons.
//
// PR 3.5: Ambient authoring registries.  `style_registry` and
// `motion_registry` are optional pointer fields.  When non-null, the
// `chronon3d::authoring` façade (Layer / Text) can resolve `.style(id)`
// and `.motion(id)` ambient without the host needing to pass the
// registries explicitly per call.  Both default to nullptr so the
// existing 12+ ExtensionContext construction sites compile unchanged —
// adoption is opt-in.
// ============================================================================

namespace chronon3d::authoring {
    // Forward-declarations only — keeps this header include-light.
    // The types live in chronon3d::authoring::style_registry.hpp / motion_registry.hpp
    // which the Layer / Text handle files transitively pull in when needed.
    class StyleRegistry;
    class MotionRegistry;
}

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
///
/// The two authoring-registry pointers are:
///   - optional (host not required to populate them)
///   - default-initialised to nullptr
///   - consumed ambient by the `chronon3d::authoring` façade when present
struct ExtensionContext {
    CompositionRegistry&           compositions;
    graph::GraphNodeCatalog&       graph_nodes;
    effects::EffectCatalog&        effects;
    AssetRegistry&                 assets;

    // ── PR 3.5 — Ambient authoring registries (optional) ─────────────────
    //
    // When non-null, the `chronon3d::authoring::Text` handle resolves
    // `.style(id)` / `.motion(id)` against these pointers instead of
    // requiring an explicit registry method argument.  When null, the
    // ambient methods gracefully no-op (the explicit-param variants
    // `style(id, registry)` / `motion(id, registry)` still work).
    //
    // Lifetime: these pointers reference registries the host owns.
    // ExtensionContext outlives every authoring handle that reads
    // them — same invariant the existing 4 references rely on.
    authoring::StyleRegistry*      style_registry{nullptr};
    authoring::MotionRegistry*     motion_registry{nullptr};
};

} // namespace chronon3d
