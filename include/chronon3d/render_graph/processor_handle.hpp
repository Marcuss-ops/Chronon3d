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

} // namespace chronon3d::renderer
