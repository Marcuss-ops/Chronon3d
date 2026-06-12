#pragma once

// ---------------------------------------------------------------------------
// scene_dirty_helpers.hpp
//
// Convenience header that includes all dirty-helper policy modules:
//   - scroll_optimization  (try_scroll_optimization)
//   - layer_bbox_collector (compute_layer_bboxes_parallel)
//   - root_bbox_collector  (compute_scene_root_bboxes)
//
// Split from the original monolithic scene_dirty_helpers.cpp.
// ---------------------------------------------------------------------------

#include "dirty/scroll_optimization.hpp"
#include "dirty/layer_bbox_collector.hpp"
#include "dirty/root_bbox_collector.hpp"
