// SPDX-License-Identifier: MIT
#pragma once

#include <chronon3d/render_graph/compiler/pixel_domain.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <cstdint>
#include <string_view>
#include <string>

namespace chronon3d::graph {

/// Legitimate reasons for materializing an intermediate framebuffer surface.
/// Invariant: Every surface in the compiled physical allocation plan MUST
/// declare a valid MaterializationReason. If it cannot justify its existence,
/// it is eliminated.
enum class MaterializationReason : std::uint8_t {
    NeighborhoodOperation,   // Convolution, Gaussian blur, dilation, halo sampling
    TemporalHistory,         // Motion blur, optical flow, frame accumulation across time
    DependentMask,           // Complex mask requiring raster evaluation before application
    ColorDomainTransition,   // Explicit boundary between distinct color spaces (e.g. YUV -> RGBA16F)
    BackendCapability,       // Hardware or API constraint requiring intermediate texture
    ExternalInterop,         // NVDEC/NVENC/Vulkan/CUDA/GL interop surface
    DebugReadback,           // Explicit diagnostic capture requested by caller
};

[[nodiscard]] constexpr std::string_view materialization_reason_name(MaterializationReason reason) noexcept {
    switch (reason) {
        case MaterializationReason::NeighborhoodOperation: return "NeighborhoodOperation";
        case MaterializationReason::TemporalHistory:       return "TemporalHistory";
        case MaterializationReason::DependentMask:         return "DependentMask";
        case MaterializationReason::ColorDomainTransition: return "ColorDomainTransition";
        case MaterializationReason::BackendCapability:     return "BackendCapability";
        case MaterializationReason::ExternalInterop:       return "ExternalInterop";
        case MaterializationReason::DebugReadback:         return "DebugReadback";
    }
    return "Unknown";
}

/// Explicit materialization barrier recording producer, consumer, format, lifetime and physical slot.
struct MaterializationBarrier {
    MaterializationReason reason{MaterializationReason::NeighborhoodOperation};
    GraphNodeId producer{k_invalid_node};
    GraphNodeId consumer{k_invalid_node};
    PixelDomain domain{PixelDomain::RGBA16F};
    std::uint32_t lifetime_start_level{0};
    std::uint32_t lifetime_end_level{0};
    std::uint32_t physical_slot{0};
    std::string detail;
};

} // namespace chronon3d::graph
