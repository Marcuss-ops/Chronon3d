#pragma once

// ---------------------------------------------------------------------------
// design_kit.hpp
//
// Aggregator header — includes all design kit sub-modules.
//
// Previously a single 470-line monolithic header, now split into:
//   layout/layout_stack.hpp  — hstack, vstack, BoxPadding, box_fit_content
//   layout/stroked_shapes.hpp — stroked shape helpers, PostEffectPipeline, Materials
//
// Existing consumers that #include <chronon3d/layout/design_kit.hpp> continue
// to work unchanged — all symbols are re-exported via this aggregator.
// ---------------------------------------------------------------------------

#include <chronon3d/layout/layout_stack.hpp>
#include <chronon3d/layout/stroked_shapes.hpp>
