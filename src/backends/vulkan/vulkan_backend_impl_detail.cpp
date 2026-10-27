// ============================================================================
// vulkan_backend_impl_detail.cpp — out-of-line definitions extracted from
// vulkan_backend_impl.hpp (issue #52, C3D-15).
//
// Only bodies that do not need to live in the header moved here. The ABI
// layout structs (VulkanGpuGlyphStatic / VulkanGpuGlyphDynamic) and their
// static_asserts stay in vulkan_backend_impl.hpp, the header that defines the
// layouts, as required by the ticket.
// ============================================================================

#include "vulkan_backend_impl.hpp"

namespace chronon3d::backends::vulkan {

bool surface_lifecycle_diag_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_SURFACE_LIFECYCLE_DIAG");
        return value && *value && std::string_view(value) != "0";
    }();
    return enabled;
}

void check(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return;
    if (result == VK_ERROR_DEVICE_LOST) {
        runtime::poison_gpu_worker();
        throw runtime::GpuDeviceLostError{
            std::string{"Vulkan device lost during "} + operation};
    }
    throw std::runtime_error(std::string{"Vulkan "} + operation +
                             " failed with VkResult=" +
                             std::to_string(static_cast<int>(result)));
}

VulkanBackend::Impl::Image& VulkanBackend::Impl::bind_surface_to_slot(
    runtime::RenderSurfaceHandle handle,
    std::size_t slot,
    const runtime::SurfaceDesc& desc,
    std::span<const runtime::RenderSurfaceHandle> planned_occupants) {
    return surfaces.bind(handle, slot, desc, planned_occupants);
}

// Fixed acquisition order (surfaces → submission), the order the displaced
// scoped_lock used, so no inversion is introduced.
VulkanBackend::Impl::DomainLock::DomainLock(Impl& impl, LockDomain domain)
    : impl_(impl), domain_(domain), wait_start_(profiling::now()),
      first_(domain == LockDomain::Submission ? impl.submission_ring.mutex
                                              : impl.surfaces.mutex),
      second_(impl.submission_ring.mutex, std::defer_lock) {
    if (domain == LockDomain::Dual) second_.lock();
    impl_.record_lock_wait(domain_, static_cast<std::uint64_t>(
        profiling::elapsed_us(wait_start_)));
    hold_start_ = profiling::now();
}

VulkanBackend::Impl::DomainLock::~DomainLock() {
    impl_.record_lock_hold(domain_, static_cast<std::uint64_t>(
        profiling::elapsed_us(hold_start_)));
}

VulkanBackend::Impl::RecordingResolveScope::RecordingResolveScope() noexcept
    : previous(recording_resolve_context()) {
    recording_resolve_context() = true;
}

VulkanBackend::Impl::RecordingResolveScope::~RecordingResolveScope() {
    recording_resolve_context() = previous;
}

VkFormat VulkanBackend::Impl::to_vk_format(runtime::PixelFormat fmt) noexcept {
    switch (fmt) {
        case runtime::PixelFormat::Rgba32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case runtime::PixelFormat::Rgba8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case runtime::PixelFormat::R8Unorm:
            return VK_FORMAT_R8_UNORM;
        case runtime::PixelFormat::Nv12:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case runtime::PixelFormat::P010:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        default:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}

bool VulkanBackend::Impl::surface_compatible(const runtime::SurfaceDesc& a,
                                             const runtime::SurfaceDesc& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.residency == b.residency;
}


// Single authority for the create-pipeline → register-in-registry →
// destroy-on-reject dance each compute kernel block shares.
void register_kernel_or_throw(
    GpuKernelRegistry& registry, VkDevice device,
    GpuKernelId id, VkPipeline pipeline, const char* pipeline_name) {
    if (!registry.register_kernel(
            id, reinterpret_cast<GpuKernelRegistry::PipelineHandle>(pipeline))) {
        vkDestroyPipeline(device, pipeline, nullptr);
        throw std::runtime_error(
            std::string("Vulkan kernel registry rejected ") +
            pipeline_name + " pipeline");
    }
}

} // namespace chronon3d::backends::vulkan
