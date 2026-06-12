#include "scene_fingerprint.hpp"

namespace chronon3d::graph {

FrameFingerprints compute_frame_fingerprints(
    graph::SceneHasher& hasher,
    const Scene& scene,
    Frame frame)
{
    FrameFingerprints fps;
    fps.static_fp     = hasher.compute_static_fingerprint(scene);
    fps.active_at_fp  = hasher.compute_active_at_fingerprint(scene, frame);
    fps.structure_fp  = hasher.compute_structure_fingerprint(scene);
    fps.combined_fp   = fps.static_fp ^ (fps.active_at_fp * 0x9e3779b97f4a7c15ULL);
    return fps;
}

} // namespace chronon3d::graph
