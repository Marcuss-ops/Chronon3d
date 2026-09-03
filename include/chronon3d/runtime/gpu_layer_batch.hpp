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
#include <chronon3d/runtime/render_surface_handle.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace chronon3d::graph {
struct CompiledNodeInfo;
struct CompiledLayerBatch;
} // namespace chronon3d::graph

namespace chronon3d::runtime {

// ── PrimitiveKind ────────────────────────────────────────────────────────────

enum class PrimitiveKind : std::uint8_t {
    Image        = 0,
    Text         = 1,
    Rect         = 2,
    RoundedRect  = 3,
    Video        = 4,
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
    float           opacity{1.0f};

    BlendMode       blend{BlendMode::Normal};
    float           corner_radius{0.0f};

    [[nodiscard]] bool operator==(const LayerInstance&) const noexcept = default;
};

// ── GpuLayerBatch ────────────────────────────────────────────────────────────
//
/// A batch of layer instances that the backend can consume as a single
/// submission.  The backend (Vulkan or CUDA) iterates `instances`, resolves
/// resource/transform/paint handles, and dispatches the appropriate kernel.
struct GpuLayerBatch {
    std::vector<LayerInstance> instances;
    // Frame-local resolved resource table; this is not a second registry.
    std::vector<RenderSurfaceHandle> resources;
    std::uint32_t              output_physical_slot{0};
    bool                       is_gpu_fused{false};

    [[nodiscard]] bool empty() const noexcept { return instances.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return instances.size(); }
};

[[nodiscard]] std::optional<LayerInstance> lower_node_to_layer_instance(
    const graph::CompiledNodeInfo& node,
    std::uint32_t resource_index = 0,
    std::uint32_t transform_index = 0,
    std::uint32_t paint_index = 0,
    float opacity = 1.0f,
    BlendMode blend = BlendMode::Normal);

[[nodiscard]] GpuLayerBatch make_gpu_batch(
    const graph::CompiledLayerBatch& compiled_batch);

} // namespace chronon3d::runtime
