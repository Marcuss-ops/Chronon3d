#pragma once

// Public surface-identity handle.  Extracted from runtime/render_surface.hpp
// so public headers that only need the handle (e.g. core/memory/framebuffer.hpp)
// can depend on this tiny header instead of the full surface registry/descriptor
// surface — which is not installed with the SDK.

#include <cstdint>

namespace chronon3d::runtime {

/// Opaque logical surface identity. Backends resolve this handle to a CPU
/// framebuffer, VkImage, or another device-local representation.
using RenderSurfaceHandle = std::uint64_t;

inline constexpr RenderSurfaceHandle kInvalidRenderSurfaceHandle = 0;

} // namespace chronon3d::runtime
