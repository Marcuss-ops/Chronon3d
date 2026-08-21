#pragma once

// Public processor-handle value types.  These are the stable, public-facing
// identifiers carried by RenderState / compiled frame graphs to reference a
// shape or effect processor captured in a renderer::ProcessorRegistrySnapshot.
//
// The snapshot itself stays internal (internal/render_graph/...); only these
// small PODs are exposed so public headers (e.g. math/transform.hpp) can hold
// a handle BY VALUE without depending on the internal/ tree — which is not
// installed with the SDK.

#include <cstdint>

namespace chronon3d::renderer {

struct ShapeProcessorHandle {
    std::uint32_t index{invalid_index()};

    [[nodiscard]] static constexpr std::uint32_t invalid_index() noexcept {
        return static_cast<std::uint32_t>(-1);
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index();
    }

    friend constexpr bool operator==(ShapeProcessorHandle,
                                     ShapeProcessorHandle) = default;
};

struct EffectProcessorHandle {
    std::uint32_t index{invalid_index()};

    [[nodiscard]] static constexpr std::uint32_t invalid_index() noexcept {
        return static_cast<std::uint32_t>(-1);
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index();
    }

    friend constexpr bool operator==(EffectProcessorHandle,
                                     EffectProcessorHandle) = default;
};

struct ProcessorCapabilities {
    bool gpu : 1 {false};
    bool in_place : 1 {false};
    bool fusible : 1 {false};
    bool pixel_local : 1 {false};
    bool native_surface_input : 1 {false};
    bool native_surface_output : 1 {false};

    [[nodiscard]] constexpr bool is_gpu_fusible() const noexcept {
        return gpu && fusible && pixel_local;
    }

    friend constexpr bool operator==(ProcessorCapabilities,
                                     ProcessorCapabilities) = default;
};

} // namespace chronon3d::renderer
