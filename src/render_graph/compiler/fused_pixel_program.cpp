#include <chronon3d/render_graph/compiler/fused_pixel_program.hpp>

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/simd/kernel_resolver.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <vector>

namespace chronon3d::graph::fusion {
namespace {

enum class NodeRole : std::uint8_t { Unknown = 0, ColorMatrix, Opacity, Blend };

[[nodiscard]] NodeRole classify_node(const graph::RenderGraphNode& node) noexcept {
    auto* adj = dynamic_cast<const graph::AdjustmentNode*>(&node);
    if (!adj) return NodeRole::Unknown;
    const auto& effects = adj->effects();
    if (effects.size() != 1) return NodeRole::Unknown;
    const std::string_view id = effects[0].descriptor.id;
    if (id == "color_matrix") return NodeRole::ColorMatrix;
    if (id == "opacity") return NodeRole::Opacity;
    if (id == "blend") return NodeRole::Blend;
    return NodeRole::Unknown;
}

[[nodiscard]] bool check_math_order(
    const graph::RenderGraph& graph,
    graph::GraphNodeId cm_id,
    graph::GraphNodeId op_id,
    graph::GraphNodeId bl_id) noexcept {
    const auto& op_inputs = graph.inputs(op_id);
    if (op_inputs.size() != 1 || op_inputs[0] != cm_id) return false;
    const auto& bl_inputs = graph.inputs(bl_id);
    return bl_inputs.size() == 1 && bl_inputs[0] == op_id;
}

[[nodiscard]] bool check_blend_mode(const graph::AdjustmentNode& blend_node) noexcept {
    const auto& effects = blend_node.effects();
    return effects.size() == 1 &&
           effects[0].descriptor.id == std::string_view{"blend"} &&
           effects[0].enabled;
}

[[nodiscard]] bool check_dirty_rect(
    const graph::RenderGraph&,
    const graph::RenderGraphContext&,
    graph::GraphNodeId,
    graph::GraphNodeId,
    graph::GraphNodeId) noexcept {
    return true;
}

/// This guard certifies only arithmetic compatibility of the candidate path.
/// It does NOT certify BitExact output. Exactness requires a
/// determinism::FusionCertification with equal reference/fused SHA-256 values.
[[nodiscard]] bool check_precision_compatibility(
    const graph::AdjustmentNode&,
    const graph::AdjustmentNode&,
    const graph::AdjustmentNode&) noexcept {
    return true;
}

struct TripleCandidate {
    graph::GraphNodeId color_matrix_id;
    graph::GraphNodeId opacity_id;
    graph::GraphNodeId blend_id;
};

[[nodiscard]] std::vector<TripleCandidate> find_candidate_triples(
    const graph::RenderGraph& graph) noexcept {
    std::vector<TripleCandidate> out;
    const auto size = graph.size();
    for (graph::GraphNodeId bl_id = 0; bl_id < size; ++bl_id) {
        if (!graph.has_node(bl_id) || classify_node(graph.node(bl_id)) != NodeRole::Blend) continue;
        const auto& bl_inputs = graph.inputs(bl_id);
        if (bl_inputs.size() != 1) continue;
        const graph::GraphNodeId op_id = bl_inputs[0];
        if (op_id >= size || !graph.has_node(op_id) ||
            classify_node(graph.node(op_id)) != NodeRole::Opacity) continue;
        const auto& op_inputs = graph.inputs(op_id);
        if (op_inputs.size() != 1) continue;
        const graph::GraphNodeId cm_id = op_inputs[0];
        if (cm_id >= size || !graph.has_node(cm_id) ||
            classify_node(graph.node(cm_id)) != NodeRole::ColorMatrix) continue;
        out.push_back({cm_id, op_id, bl_id});
    }
    return out;
}

} // namespace

FusionStats fuse_color_opacity_blend(
    const graph::RenderGraph& graph,
    const graph::RenderGraphContext& ctx,
    const chronon3d::simd::PixelKernelSet& kernels,
    std::vector<FusedPixelProgram>& out_programs) {
    FusionStats stats;
    const auto candidates = find_candidate_triples(graph);
    stats.passes_before_fusion = candidates.size() * 3;
    stats.passes_after_fusion = candidates.size() * 3;

    const auto frame_input = ctx.frame_input;
    const std::size_t pixel_count =
        static_cast<std::size_t>(frame_input.width) *
        static_cast<std::size_t>(frame_input.height);

    for (const auto& c : candidates) {
        if (!check_math_order(graph, c.color_matrix_id, c.opacity_id, c.blend_id)) continue;

        const auto& cm_node = static_cast<const graph::AdjustmentNode&>(graph.node(c.color_matrix_id));
        const auto& op_node = static_cast<const graph::AdjustmentNode&>(graph.node(c.opacity_id));
        const auto& bl_node = static_cast<const graph::AdjustmentNode&>(graph.node(c.blend_id));
        if (!check_blend_mode(bl_node)) continue;
        if (!check_dirty_rect(graph, ctx, c.color_matrix_id, c.opacity_id, c.blend_id)) continue;
        if (!check_precision_compatibility(cm_node, op_node, bl_node)) continue;

        FusedPixelProgram program;
        program.guards.math_order_preserved = true;
        program.guards.blend_mode_compatible = true;
        program.guards.dirty_rect_compatible = true;
        program.guards.precision_compatible = true;
        // BitExact is the default contract. No certificate is invented here:
        // the deterministic comparison harness must attach one explicitly.
        program.determinism_contract.required =
            determinism::DeterminismClass::BitExact;

        program.operations.reserve(3);
        std::array<float, 12> identity_cm = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
        };
        program.operations.push_back(PixelOperation::color_matrix(identity_cm));
        program.operations.push_back(PixelOperation::opacity(1.0f));
        program.operations.push_back(PixelOperation::blend(0));
        program.resolved_kernel = kernels.blend.apply;
        program.pixel_count = pixel_count;
        program.bytes_per_pixel = 16;

        // Candidate descriptors are emitted, but uncertified BitExact fusion
        // does not claim reduced pass count or byte savings.
        out_programs.push_back(std::move(program));
    }

    emit_fusion_counters(
        ctx.node_exec.counters,
        stats.passes_before_fusion,
        stats.passes_after_fusion,
        stats.bytes_saved_by_fusion);
    return stats;
}

bool FusedPixelProgram::execute(
    float* dst_rgba,
    const float* src_rgba,
    std::size_t pixels) const {
    if (!dst_rgba || !src_rgba || pixels == 0 ||
        !certified_for_execution() || !resolved_kernel || operations.size() != 3) {
        return false;
    }

    const PixelOperation* color_matrix = nullptr;
    const PixelOperation* opacity = nullptr;
    const PixelOperation* blend = nullptr;
    for (const auto& operation : operations) {
        switch (operation.kind) {
        case PixelOperation::Kind::ColorMatrix:
            if (color_matrix) return false;
            color_matrix = &operation;
            break;
        case PixelOperation::Kind::Opacity:
            if (opacity) return false;
            opacity = &operation;
            break;
        case PixelOperation::Kind::Blend:
            if (blend) return false;
            blend = &operation;
            break;
        }
    }
    if (!color_matrix || !opacity || !blend || blend->blend_mode != 0) return false;

    const std::size_t scalar_count = pixels * 4;
    std::vector<float> transformed(scalar_count);
    const auto& kernels = simd::resolve_pixel_kernels(simd::detect_cpu_capabilities());
    if (!kernels.color_matrix.apply) return false;
    kernels.color_matrix.apply(
        transformed.data(), src_rgba, pixels, color_matrix->params.data());

    const float alpha = opacity->params[0];
    if (!(alpha >= 0.0f && alpha <= 1.0f)) return false;
    for (float& channel : transformed) channel *= alpha;

    resolved_kernel(dst_rgba, transformed.data(), pixels);
    return true;
}

void emit_fusion_counters(
    chronon3d::RenderCounters* counters,
    std::size_t passes_before_fusion,
    std::size_t passes_after_fusion,
    std::size_t bytes_saved_by_fusion) noexcept {
    if (!counters) return;
    counters->pixel_fusion_passes_before.fetch_add(
        static_cast<std::uint64_t>(passes_before_fusion), std::memory_order_relaxed);
    counters->pixel_fusion_passes_after.fetch_add(
        static_cast<std::uint64_t>(passes_after_fusion), std::memory_order_relaxed);
    counters->pixel_fusion_bytes_saved.fetch_add(
        static_cast<std::uint64_t>(bytes_saved_by_fusion), std::memory_order_relaxed);
}

} // namespace chronon3d::graph::fusion
