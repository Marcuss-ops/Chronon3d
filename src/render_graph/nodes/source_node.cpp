// ═══════════════════════════════════════════════════════════════════════════
// source_node.cpp — SourceNode lifecycle (construction, refresh).
//
// Bounds and cache-key queries live in source_node_geometry.cpp, per-frame
// execution in source_node_execute.cpp, and the native (GPU) fast paths in
// source_node_native_rect.cpp / source_node_native_image.cpp.  Shared
// internals are in source_node_native.hpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/nodes/source_node.hpp>

#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <span>
#include <utility>

namespace chronon3d::graph {

// (construction is inline in the public header; this TU anchors the class's
// virtual-table and exception-frame emissions for the graph-nodes object
// library.)

} // namespace chronon3d::graph
