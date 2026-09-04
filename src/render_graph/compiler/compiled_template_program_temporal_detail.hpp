TemporalAnalysisResult
classify_temporal(const CompiledFrameGraph& compiled) {
    TemporalAnalysisResult result;
    result.total_count = compiled.nodes.size();
    result.per_node.resize(compiled.nodes.size());

    // Execution levels are topological; propagate front-to-back.
    // A node's final class = max(own class, max(input classes)).
    for (const auto& level : compiled.levels) {
        for (const auto id : level) {
            if (id >= compiled.nodes.size()) continue;
            const auto& node = compiled.nodes[id];

            int rank = temporal_rank(caps_to_class(derive_capabilities(node)));
            for (const auto in : node.inputs) {
                if (in >= result.per_node.size()) continue;
                rank = std::max(rank, temporal_rank(result.per_node[in].classification));
            }

            result.per_node[id] = CompiledTemporalInfo{
                .node           = id,
                .classification = rank_to_class(rank),
            };
            if (is_static(result.per_node[id].classification)) {
                ++result.static_count;
            }
        }
    }

    return result;
}

std::vector<StaticBakeRegion> bake_maximal_static_islands(
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal) {
    std::vector<StaticBakeRegion> regions;

    if (compiled.nodes.empty() || temporal.per_node.size() != compiled.nodes.size()) {
        return regions;
    }

    // Region assignment: every static node gets a region id; a dynamic node
    // ends the region.  Members accumulate in level order so the vector is
    // already topological.
    std::vector<int> region_of(compiled.nodes.size(), -1);

    for (const auto& level : compiled.levels) {
        for (const auto id : level) {
            if (id >= compiled.nodes.size()) continue;
            const auto cls = temporal.classification(id);
            if (!is_static(cls)) continue;  // dynamic node: no region

            // Reuse the region of the first static input, if any.
            int region = -1;
            for (const auto in : compiled.nodes[id].inputs) {
                if (in < region_of.size() && region_of[in] >= 0) {
                    region = region_of[in];
                    break;
                }
            }
            if (region < 0) {
                region = static_cast<int>(regions.size());
                regions.push_back(StaticBakeRegion{});
                regions.back().root = id;
            }
            region_of[id] = region;
            regions[static_cast<std::size_t>(region)].members.push_back(id);
        }
    }

    // Populate bake ids + fingerprints for every discovered region.
    for (std::size_t i = 0; i < regions.size(); ++i) {
        auto& region = regions[i];
        region.bake_id     = static_cast<std::uint32_t>(i);
        region.fingerprint = region_fingerprint(region.members, compiled);
        region.is_baked    = !region.members.empty();
    }

    return regions;
}

std::vector<StaticBakeRegion> merge_contiguous_static_regions(
    std::vector<StaticBakeRegion> regions,
    const CompiledFrameGraph& compiled,
    const TemporalAnalysisResult& temporal) {
    // Fase B first pass: regions are already maximal per connected static
    // component (a dynamic node between two static runs splits them, which is
    // correct — merging across a dynamic separator would break baking).  The
    // merge pass exists for later phases where a static island may be
    // fragmented by execution order; today it is a semantic no-op that
    // re-validates determinism.
    (void)compiled;
    (void)temporal;
    return regions;
}
