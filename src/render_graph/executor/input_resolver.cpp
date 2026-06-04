#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph_hashing.hpp>

namespace chronon3d::graph {

PreResolvedNode resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    std::pmr::memory_resource* res
) {
    (void)consumer_remaining;
    const auto& input_ids = graph.inputs(id);
    PreResolvedNode pr(res);
    pr.inputs.resize(input_ids.size());
    pr.input_bboxes.resize(input_ids.size());
    pr.inputs_all_cache_hits = !input_ids.empty();

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
        if (contains_index(state.resolved_cache_hit, input_id)) {
            pr.inputs_all_cache_hits &= (state.resolved_cache_hit[input_id] != 0);
        } else {
            pr.inputs_all_cache_hits = false;
        }
        if (contains_index(state.resolved_key_digest, input_id)) {
            pr.input_hash = hash_combine(pr.input_hash, state.resolved_key_digest[input_id]);
        }
    }
    return pr;
}

} // namespace chronon3d::graph
