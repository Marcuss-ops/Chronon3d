#pragma once

// Statistics authority for VulkanBackend::Impl.
//
// Counters are mutated by the domain that performs the measured work: surface
// counters under SurfaceAuthority::mutex and execution/timing counters under
// SubmissionAuthority::mutex. This authority owns the state and provides the
// snapshot boundary; it deliberately does not add a third global data-plane
// lock.

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#include <vector>

namespace chronon3d::backends::vulkan {

struct VulkanStatsAuthority : VulkanBackendStats {
    std::vector<VulkanFrameGpuTiming> completed_frame_timings{};

    [[nodiscard]] VulkanBackendStats snapshot() const noexcept {
        return static_cast<const VulkanBackendStats&>(*this);
    }
};

} // namespace chronon3d::backends::vulkan
