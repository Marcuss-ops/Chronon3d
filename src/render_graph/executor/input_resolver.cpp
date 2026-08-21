#include "execution_state.hpp"
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

void resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    PreResolvedNode& pr
) {
    (void)consumer_remaining;
    const auto& input_ids = graph.inputs(id);
    // The caller constructs this object directly in the level-owned PMR
    // vector.  Reuse its storage when a level vector is recycled, avoiding a
    // temporary PreResolvedNode plus a second vector move/assignment for
    // every node on every frame.
    pr.inputs.clear();
    pr.input_bboxes.clear();
    pr.inputs.resize(input_ids.size());
    pr.input_bboxes.resize(input_ids.size());
    pr.inputs_frame_dependent = false;
    pr.has_cacheable_inputs = false;
    pr.input_hash = 0;

    for (size_t j = 0; j < input_ids.size(); ++j) {
        const GraphNodeId input_id = input_ids[j];
        if (contains_index(state.temp, input_id) && state.temp[input_id]) {
            // Extract non-owning raw pointer — no atomic refcounting.
            pr.inputs[j] = FramebufferRef(state.temp[input_id].get());
        }
        if (contains_index(state.resolved_bboxes, input_id)) {
            pr.input_bboxes[j] = state.resolved_bboxes[input_id];
        }
        if (contains_index(state.resolved_frame_dependent, input_id)) {
            pr.inputs_frame_dependent |= (state.resolved_frame_dependent[input_id] != 0);
            pr.has_cacheable_inputs = true;
        }
        if (contains_index(state.resolved_key_digest, input_id)) {
            pr.input_hash = hash_combine(pr.input_hash, state.resolved_key_digest[input_id]);
        }
    }
}

} // namespace chronon3d::graph
