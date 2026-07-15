// ═══════════════════════════════════════════════════════════════════════════
//  launches.cpp — Domain registration wrapper for the `launches` composition
//  group.  Bridges `register_content_modules()` (which lives in
//  `content/register_content_modules.cpp`) with the per-file factory
//  function exposed in `content/launches/product_launch.hpp`.
//
//  Pattern mirrors `content/sequence_v2/sequence_v2_compositions.cpp`:
//  one free function `register_launches_compositions(CompositionRegistry&)`
//  that forwards calls to `Registry::add()` with the canonical ProductLaunch
//  factory (Returned-by-value Composition objects; the registry stores the
//  factory closure, evaluated at render time).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/composition/composition_registry.hpp>
#include <content/launches/product_launch.hpp>

namespace chronon3d::content::launches {

void register_launches_compositions(CompositionRegistry& registry) {
    registry.add(CompositionDescriptor{.id = "ProductLaunch", .factory = [](const CompositionProps&) -> Composition {
        return product_launch();
    }});
}

} // namespace chronon3d::content::launches
