#pragma once

// ---------------------------------------------------------------------------
// scene_fingerprint.hpp
//
// Fingerprint computation for the scene pipeline.  Computes the three
// fingerprint types used by frame-reuse fast-paths and graph structure
// change detection:
//
//   static_fp     — hashes layer structure + static properties (no frame
//                   dependence).  Used to detect structural scene changes.
//   active_at_fp  — hashes which layers are active at a given frame.
//                   Combined with static_fp to prevent false-positive
//                   reuse when layers activate/deactivate.
//   structure_fp  — hashes the topological structure of the scene graph
//                   (layer order, hierarchy, node topology).  Used to
//                   detect graph structure changes.
// ===========================================================================

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <cstdint>

namespace chronon3d::graph {

/// Aggregated fingerprints for the current frame.
/// Combined fingerprint = static_fp ^ (active_at_fp * 0x9e3779b97f4a7c15ULL)
/// to match the format stored in FrameHistory for consistent comparison.
struct FrameFingerprints {
    uint64_t static_fp{0};      // layer structure + static properties
    uint64_t active_at_fp{0};   // layer active-at mask for this frame
    uint64_t structure_fp{0};   // topological graph structure
    uint64_t combined_fp{0};    // static_fp ^ (active_at_fp * C)
};

/// Compute all three fingerprints for the given scene + frame.
/// Returns zeros when the hasher has no history (first frame).
/// NOTE: SceneHasher methods are NOT const (they modify internal state
/// for incremental hashing), so requires a non-const reference.
[[nodiscard]] FrameFingerprints compute_frame_fingerprints(
    graph::SceneHasher& hasher,
    const Scene& scene,
    Frame frame);

} // namespace chronon3d::graph
