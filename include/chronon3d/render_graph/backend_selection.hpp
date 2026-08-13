#pragma once

#include <cstdint>

namespace chronon3d::graph {

/// Backend identity and user-facing selection policy. Kept independent from
/// RenderBackend so Config and job values do not import render implementation.
enum class BackendType { Software, Vulkan };

enum class BackendPreference { Auto, Software, GPU };

/// Device-level capabilities published by a backend descriptor.
/// Zero limits mean that a backend did not publish a limit.
struct BackendCapabilities {
    bool graphics{false};
    bool compute{false};
    bool hardware_encode{false};
    std::uint32_t max_texture_width{0};
    std::uint32_t max_texture_height{0};
    std::uint64_t device_memory_bytes{0};
};

} // namespace chronon3d::graph
