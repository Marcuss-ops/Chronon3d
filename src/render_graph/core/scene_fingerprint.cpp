#include "scene_fingerprint.hpp"
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>

namespace chronon3d::graph {

FrameFingerprints compute_frame_fingerprints(
    const Scene& scene,
    Frame frame,
    std::uint64_t registry_generation)
{
    graph::SceneHasher hasher;
    FrameFingerprints fps;
    fps.static_fp     = hasher.compute_static_fingerprint(scene);
    fps.active_at_fp  = hasher.compute_active_at_fingerprint(scene, frame);
    fps.structure_fp  = hasher.compute_structure_fingerprint(scene, registry_generation);
    // `active_at_fp` is topology-relevant only. Use the dynamic payload
    // fingerprint for pixel-reuse decisions so video/transition samples do
    // not unnecessarily force a compiled graph rebuild.
    const auto dynamic_fp = hasher.compute_dynamic_fingerprint(scene, frame);
    fps.combined_fp   = fps.static_fp ^ (dynamic_fp * 0x9e3779b97f4a7c15ULL);
    return fps;
}

bool scene_is_static_at(const Scene& scene, Frame frame) {
    return graph::SceneHasher{}.is_static_scene_at(scene, frame);
}

std::uint64_t scene_structure_fingerprint(
    const Scene& scene, std::uint64_t registry_generation) {
    return graph::SceneHasher{}.compute_structure_fingerprint(
        scene, registry_generation);
}

} // namespace chronon3d::graph
