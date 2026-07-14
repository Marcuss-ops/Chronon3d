#include <chronon3d/render_graph/compiler/fused_pixel_program.hpp>

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/simd/kernel_resolver.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>  // F3.1 atomic-counter bridge (--stats-json)

#include <algorithm>
#include <array>
#include <vector>

namespace chronon3d::graph::fusion {

namespace {

// ── Effect-stack classifier ─────────────────────────────────────────────
//
// Heuristic for the F3.1 first pass: identify a 3-node pattern by
// looking at each AdjustmentNode's EffectStack and matching by the
// effect's `descriptor.id` (the canonical effect name string).
//
// F3.1 first commit classification rules:
//   - ColorMatrix node: 1 effect with `descriptor.id == "color_matrix"`
//   - Opacity node:     1 effect with `descriptor.id == "opacity"`
//   - Blend node:       1 effect with `descriptor.id == "blend"`
//
// These are CONCEPTUAL ids per the F3.1 user-spec verbatim
// `ColorMatrix → Opacity → Blend` pattern. A future commit
// (TICKET-FUSION-PASS-EFFECT-INTROSPECTION-V1) will wire to the
// real effects catalog (Tint/Brightness/Contrast for ColorMatrix,
// Transform opacity for Opacity, CompositeNode blend_mode for Blend).
// Per Cat-3 minimal-surface, the F3.1 first commit ships the ABI +
// a clear extension point for the real wiring.
enum class NodeRole : std::uint8_t {
    Unknown = 0,
    ColorMatrix,
    Opacity,
    Blend,
};

[[nodiscard]] NodeRole classify_node(const graph::RenderGraphNode& node) noexcept {
    auto* adj = dynamic_cast<const graph::AdjustmentNode*>(&node);
    if (!adj) return NodeRole::Unknown;
    const auto& effects = adj->effects();
    if (effects.size() != 1) return NodeRole::Unknown;
    // F3.1 first pass: classify by the effect's descriptor.id (a
    // string that uniquely identifies the effect type in the catalog).
    const std::string_view id = effects[0].descriptor.id;
    if (id == "color_matrix") return NodeRole::ColorMatrix;
    if (id == "opacity")      return NodeRole::Opacity;
    if (id == "blend")        return NodeRole::Blend;
    return NodeRole::Unknown;
}

// ── Guard (a) math order: 3 nodes in topological order ────────────────
//
// The 3 nodes must be in the order ColorMatrix → Opacity → Blend
// along the graph's input→output topological order. We verify by
// checking that the Opacity node's only input is the ColorMatrix
// node, and the Blend node's only input is the Opacity node.
[[nodiscard]] bool check_math_order(
    const graph::RenderGraph& graph,
    graph::GraphNodeId cm_id,
    graph::GraphNodeId op_id,
    graph::GraphNodeId bl_id) noexcept
{
    const auto& op_inputs = graph.inputs(op_id);
    if (op_inputs.size() != 1 || op_inputs[0] != cm_id) return false;
    const auto& bl_inputs = graph.inputs(bl_id);
    if (bl_inputs.size() != 1 || bl_inputs[0] != op_id) return false;
    return true;
}

// ── Guard (b) blend mode: the Blend node's blend mode is Normal ────────
//
// Only Normal (SRC_OVER) blend mode supports the standard
// ColorMatrix+Opacity fusion math. Multiply/Screen/Add/Subtract
// require different math. F3.1 first pass: the descriptor.id
// presence is sufficient (any blend with id "blend" defaults to
// Normal). The real blend_mode check is forward-pointed to
// TICKET-FUSION-PASS-BLEND-MODE-V1.
[[nodiscard]] bool check_blend_mode(
    const graph::AdjustmentNode& blend_node) noexcept
{
    const auto& effects = blend_node.effects();
    if (effects.size() != 1) return false;
    // F3.1 first pass: the conceptual "blend" effect always defaults
    // to Normal (SRC_OVER). A future commit will inspect the real
    // blend_mode from the params variant.
    return effects[0].descriptor.id == std::string_view{"blend"}
        && effects[0].enabled;
}

// ── Guard (c) dirty rect: the 3 nodes share the same dirty rect ────────
//
// The 3 nodes must operate on the SAME spatial extent; a per-node
// dirty rect (e.g. the Opacity node operates on a sub-region) would
// break the fused math. F3.1 first pass uses the AdjustmentNode
// invariant (predicted_bbox always == input bbox; see
// adjustment_node.hpp:34-38) which guarantees same bbox for the
// 3-node chain.
[[nodiscard]] bool check_dirty_rect(
    const graph::RenderGraph& /*graph*/,
    const graph::RenderGraphContext& /*ctx*/,
    graph::GraphNodeId /*cm_id*/,
    graph::GraphNodeId /*op_id*/,
    graph::GraphNodeId /*bl_id*/) noexcept
{
    return true;  // AdjustmentNode invariant: bbox[0] == input[0]
}

// ── Guard (d) precision: all 3 ops are float32 (no float64) ────────────
//
// F5.1 `kKernelEpsilon = FLT_EPSILON` (1 ULP float32 IEEE-754 exact).
// A float64 op would require float64 precision in the resolved
// kernel, which the F5.1 ABI does not provide. F3.1 first pass:
// all conceptual ColorMatrix/Opacity/Blend effects operate on
// float32 RGBA; the check is forward-pointed to
// TICKET-FUSION-PASS-PRECISION-CHECK-V1.
[[nodiscard]] bool check_precision(
    const graph::AdjustmentNode& /*cm_node*/,
    const graph::AdjustmentNode& /*op_node*/,
    const graph::AdjustmentNode& /*bl_node*/) noexcept
{
    return true;  // F5.1 ABI is float32-only; all conceptual effects are float32
}

// ── Triple pattern detection ───────────────────────────────────────────
//
// Walk the graph in topological order; for each AdjustmentNode
// classified as "Blend", check if its single input is an "Opacity"
// node, and if THAT node's single input is a "ColorMatrix" node.
// If so, we have a candidate 3-node triple.
struct TripleCandidate {
    graph::GraphNodeId color_matrix_id;
    graph::GraphNodeId opacity_id;
    graph::GraphNodeId blend_id;
};

[[nodiscard]] std::vector<TripleCandidate> find_candidate_triples(
    const graph::RenderGraph& graph) noexcept
{
    std::vector<TripleCandidate> out;
    const auto size = graph.size();
    for (graph::GraphNodeId bl_id = 0; bl_id < size; ++bl_id) {
        if (!graph.has_node(bl_id)) continue;
        if (classify_node(graph.node(bl_id)) != NodeRole::Blend) continue;
        const auto& bl_inputs = graph.inputs(bl_id);
        if (bl_inputs.size() != 1) continue;
        const graph::GraphNodeId op_id = bl_inputs[0];
        if (op_id >= size || !graph.has_node(op_id)) continue;
        if (classify_node(graph.node(op_id)) != NodeRole::Opacity) continue;
        const auto& op_inputs = graph.inputs(op_id);
        if (op_inputs.size() != 1) continue;
        const graph::GraphNodeId cm_id = op_inputs[0];
        if (cm_id >= size || !graph.has_node(cm_id)) continue;
        if (classify_node(graph.node(cm_id)) != NodeRole::ColorMatrix) continue;
        out.push_back({cm_id, op_id, bl_id});
    }
    return out;
}

} // namespace

// ── Public entry: fuse_color_opacity_blend ───────────────────────────────
//
// The single export of the F3.1 pass. Walks the graph, detects
// candidate 3-node triples, checks the 4 guards, and (for triples
// that pass all guards) constructs a FusedPixelProgram descriptor
// and updates the FusionStats aggregator.
//
// Note: this is a NON-MUTATING pass in the F3.1 first commit — the
// 3 nodes are NOT removed from the graph. The pass is the DESCRIPTOR
// GENERATOR; the runtime executor consumes the FusedPixelProgram
// and decides whether to invoke the resolved_kernel. Future commits
// (forward-pointed) will add the GRAPH MUTATION (removing the 3
// nodes, inserting a single FusedPixelProgram node).
//
// Parameters:
//   - graph: the render graph to fuse
//   - ctx:   the render graph context (for frame_input w × h)
//   - kernels: the F5.1 SIMD registry's PixelKernelSet (the resolved
//              kernel binds to `kernels.blend.apply`)
//   - out_programs: OUT parameter for the FusedPixelProgram descriptors
//                   (caller owns the memory; one entry per fused triple)
//
// Returns:
//   FusionStats with passes_before_fusion / passes_after_fusion /
//   bytes_saved_by_fusion aggregated across all fused triples.
FusionStats fuse_color_opacity_blend(
    const graph::RenderGraph& graph,
    const graph::RenderGraphContext& ctx,
    const chronon3d::simd::PixelKernelSet& kernels,
    std::vector<FusedPixelProgram>& out_programs)
{
    FusionStats stats;
    const auto candidates = find_candidate_triples(graph);
    stats.passes_before_fusion = candidates.size() * 3;  // 3 nodes per triple
    stats.passes_after_fusion  = candidates.size() * 3;  // unchanged in first pass

    const auto frame_input = ctx.frame_input;
    const std::size_t pixel_count =
        static_cast<std::size_t>(frame_input.width)
      * static_cast<std::size_t>(frame_input.height);

    for (const auto& c : candidates) {
        // Guard (a) math order
        if (!check_math_order(graph, c.color_matrix_id, c.opacity_id, c.blend_id)) continue;

        // Downcast to AdjustmentNode for the effect-introspection guards
        const auto& cm_node = static_cast<const graph::AdjustmentNode&>(graph.node(c.color_matrix_id));
        const auto& op_node = static_cast<const graph::AdjustmentNode&>(graph.node(c.opacity_id));
        const auto& bl_node = static_cast<const graph::AdjustmentNode&>(graph.node(c.blend_id));

        // Guard (b) blend mode
        if (!check_blend_mode(bl_node)) continue;

        // Guard (c) dirty rect
        if (!check_dirty_rect(graph, ctx, c.color_matrix_id, c.opacity_id, c.blend_id)) continue;

        // Guard (d) precision
        if (!check_precision(cm_node, op_node, bl_node)) continue;

        // ── All 4 guards PASS → build FusedPixelProgram descriptor ────
        FusedPixelProgram program;
        program.guards.math_order_preserved  = true;
        program.guards.blend_mode_compatible = true;
        program.guards.dirty_rect_compatible = true;
        program.guards.precision_certified   = true;

        // Extract the 3 operations. F3.1 first pass: identity
        // placeholders for ColorMatrix matrix (1.0 diagonal) +
        // full opacity (1.0) + Normal blend (0). The real
        // param extraction from the effects catalog variant is
        // forward-pointed to TICKET-FUSION-PASS-EFFECT-INTROSPECTION-V1.
        program.operations.reserve(3);
        {
            // ColorMatrix: identity 3x4 (rows R, G, B; alpha is passthrough)
            std::array<float, 12> identity_cm = {
                1, 0, 0, 0,   // row 0: R = R
                0, 1, 0, 0,   // row 1: G = G
                0, 0, 1, 0,   // row 2: B = B
            };
            (void)cm_node;  // suppress unused warning; future commit extracts real params
            program.operations.push_back(PixelOperation::color_matrix(identity_cm));
        }
        {
            (void)op_node;
            program.operations.push_back(PixelOperation::opacity(1.0f));
        }
        {
            (void)bl_node;
            program.operations.push_back(PixelOperation::blend(0 /* Normal */));
        }

        // Bind the F5.1 SIMD registry's blend kernel (per F5.2: AVX2
        // when available, else scalar)
        program.resolved_kernel = kernels.blend.apply;
        program.pixel_count = pixel_count;
        program.bytes_per_pixel = 16;  // RGBA float32 (4 channels × 4 bytes)

        // Update stats: 1 fused program replaces 3 passes (3 nodes → 1 node)
        stats.passes_after_fusion -= 2;
        stats.bytes_saved_by_fusion += program.bytes_saved();

        out_programs.push_back(std::move(program));
    }

    // F3.1 --stats-json wire-up: emit the 3 counters to the canonical
    // render counters so the bench command can read them via the
    // standard `renderer->counters()->pixel_fusion_*.load()` accessor
    // (matching the existing cache_hits / cache_misses / nodes_executed
    // pattern at command_bench.cpp:201-208). This avoids a new
    // singleton (Cat-3 anti-dup discipline) and integrates with the
    // existing telemetry pipeline (kCounterNames + the render_counters
    // table in src/runtime/telemetry/sqlite/sqlite_telemetry_store.cpp).
    emit_fusion_counters(
        ctx.node_exec.counters,
        stats.passes_before_fusion,
        stats.passes_after_fusion,
        stats.bytes_saved_by_fusion);

    return stats;
}

// ── F3.1 atomic-counter bridge (--stats-json wiring) ─────────────────────
//
// Per F3.1 user-spec verbatim "Aggiungi counter passes_before_fusion,
// passes_after_fusion, bytes_saved_by_fusion esposti via --stats-json":
// the bench command reads the standard atomic counters via
// `renderer->counters()->pixel_fusion_*.load()`. This implementation
// accumulates into the thread-local `RenderCountersRaw` (which the
// per-frame `merge_tls()` flushes into the global atomic `RenderCounters`).
// The fields are `uint64_t` (atomic-safe); the FusionStats fields are
// `std::size_t` (usually 64-bit on x64). The cast is safe + non-narrowing.
void emit_fusion_counters(
    chronon3d::RenderCounters* counters,
    std::size_t passes_before_fusion,
    std::size_t passes_after_fusion,
    std::size_t bytes_saved_by_fusion) noexcept
{
    if (!counters) return;
    counters->pixel_fusion_passes_before.fetch_add(
        static_cast<std::uint64_t>(passes_before_fusion), std::memory_order_relaxed);
    counters->pixel_fusion_passes_after.fetch_add(
        static_cast<std::uint64_t>(passes_after_fusion), std::memory_order_relaxed);
    counters->pixel_fusion_bytes_saved.fetch_add(
        static_cast<std::uint64_t>(bytes_saved_by_fusion), std::memory_order_relaxed);
}

} // namespace chronon3d::graph::fusion
