#pragma once

// Canonical compiler extension surface. The implementation remains compiled
// with the graph pass objects until Wave 4 finishes moving those passes under
// src/compiler; there is intentionally one registry, not a second catalog.
#include <chronon3d/render_graph/builder/graph_build_registry.hpp>

namespace chronon3d::compiler {
using CompilerPassRegistry = chronon3d::graph::CompilerPassRegistry;
}
