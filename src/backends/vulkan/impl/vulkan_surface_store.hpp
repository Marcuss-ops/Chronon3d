#pragma once

// Fragment included inside VulkanBackend::Impl. This remains a source
// compatibility façade; VulkanSurfaceAuthority owns the actual state.
class VulkanSurfaceStore final
    : public VulkanSurfaceAuthority<Image, VulkanBackend::Impl> {
public:
    void destroy_all(VulkanBackend::Impl& owner) noexcept;
} surfaces;
