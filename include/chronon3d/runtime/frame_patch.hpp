// SPDX-License-Identifier: MIT
#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::runtime {

/// Resource delta for swapping input video frames / textures without graph recompilation.
struct ResourceDelta {
    std::uint32_t       resource_index{0};
    RenderSurfaceHandle surface_handle{kInvalidRenderSurfaceHandle};
};

/// Instance delta for updating dynamic instances (e.g. text glyphs in preallocated slot).
struct InstanceDelta {
    std::uint32_t slot_index{0};
    std::uint32_t instance_count{0};
    std::span<const std::uint8_t> data;
};

/// High-throughput runtime frame patch.
/// Production frame loop evaluates parameters and updates this patch:
/// patch_parameters(program, frame_patch) -> submit(program).
struct FramePatch {
    Frame                          frame{0};
    std::span<const float>         float_parameters;
    std::span<const ResourceDelta> resource_deltas;
    std::span<const InstanceDelta> instance_deltas;
};

} // namespace chronon3d::runtime
