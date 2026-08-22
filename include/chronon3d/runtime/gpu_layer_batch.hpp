#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// gpu_layer_batch.hpp — Fase F: backend-neutral GPU layer instance IR
//
// Each LayerInstance describes one authored layer primitive (image, text,
// shape) at the IR level — not at the per-kernel GpuPass level.  A
// GpuLayerBatch bundles instances so the backend (Vulkan compute/raster →
// RGB, or CUDA fused kernel → NV12) can consume the SAME IR.
//
// Resource/transform/paint handles are opaque uint32_t indices resolved by
// the runtime backend (texture table, transform buffer, paint uniform).
//
// Ticket: TICKET-VIDEO-COMPILER-ARCH-V1 §Fase F
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>  // CompiledLayerBatch

#include <cstdint>
#include <vector>

namespace chronon3d::runtime {

// ── PrimitiveKind ────────────────────────────────────────────────────────────

enum class PrimitiveKind : std::uint8_t {
    Image        = 0,
    Text         = 1,
    Rect         = 2,
    RoundedRect  = 3,
    Other        = 15,
};

// ── LayerInstance ────────────────────────────────────────────────────────────
//
/// One authored layer primitive, backend-neutral.  `resource_index`,
/// `transform_index`, and `paint_index` are opaque resolves (runtime
/// resource table / transform buffer / paint uniform buffer).
struct LayerInstance {
    PrimitiveKind   kind{PrimitiveKind::Image};

    std::uint32_t   resource_index{0};   // texture/atlas/glyph handle
    std::uint32_t   transform_index{0};  // transform buffer offset
    std::uint32_t   paint_index{0};      // paint uniform / color index

    float           src_x0{0}, src_y0{0}, src_x1{0}, src_y1{0};   // texture coords
    float           dst_x0{0}, dst_y0{0}, dst_x1{0}, dst_y1{0};   // framebuffer coords

    BlendMode       blend{BlendMode::Normal};

    [[nodiscard]] bool operator==(const LayerInstance&) const noexcept = default;
};

// ── GpuLayerBatch ────────────────────────────────────────────────────────────
//
/// A batch of layer instances that the backend can consume as a single
/// submission.  The backend (Vulkan or CUDA) iterates `instances`, resolves
/// resource/transform/paint handles, and dispatches the appropriate kernel.
struct GpuLayerBatch {
    std::vector<LayerInstance> instances;
    std::uint32_t              output_physical_slot{0};
    bool                       is_gpu_fused{false};

    [[nodiscard]] bool empty() const noexcept { return instances.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return instances.size(); }
};

/// Lift CompiledLayerBatch (graph-compiler level) to GpuLayerBatch.
/// `output_physical_slot` is copied from the compiled batch.
inline GpuLayerBatch make_gpu_batch(
    const graph::CompiledLayerBatch& compiled_batch) {
    GpuLayerBatch batch;
    batch.output_physical_slot = compiled_batch.output_physical_slot;
    batch.is_gpu_fused         = compiled_batch.is_gpu_fused;
    return batch;
}

} // namespace chronon3d::runtime