#include <chronon3d/backends/vulkan/vulkan_backend.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "composite_comp_spv.hpp"
#include "transform_comp_spv.hpp"
#include "affine_transform_comp_spv.hpp"
#include "blur_comp_spv.hpp"
#include "color_adjust_comp_spv.hpp"
#include "matte_comp_spv.hpp"
#endif

#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

#ifdef CHRONON3D_ENABLE_VULKAN
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string{"Vulkan "} + operation +
                                 " failed with VkResult=" +
                                 std::to_string(static_cast<int>(result)));
    }
}

// Default tint for plain composite passes (no tint applied).
constexpr float kIdentityTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

struct VulkanBackend::Impl {
    struct Image {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        std::uint32_t width{0};
        std::uint32_t height{0};
        bool initialized{false};
    };

    /// Per-frame descriptor set allocator, one per frame-batch ring slot.
    /// Allocates one descriptor set per recorded pass from chunked pools
    /// that grow geometrically (64, 128, 256, ...) when a frame exceeds the
    /// current chunk.  reset() is called when the owning ring slot is
    /// reused, returning every chunk to its initial state so the pools are
    /// recycled across frames instead of reallocated.
    class FrameDescriptorAllocator {
    public:
        static constexpr std::size_t kInitialChunkSets = 64;
        // Worst case per set: the layout exposes 3 storage-image bindings
        // (matte uses all three; the other kernels use two).  Pool sizing
        // over-reserves so any pass can allocate safely from the chunk.
        static constexpr std::size_t kDescriptorsPerSet = 3;

        void create(VkDevice device, VkDescriptorSetLayout layout) {
            device_ = device;
            layout_ = layout;
        }

        void destroy() {
            for (auto pool : pools_) {
                if (pool != VK_NULL_HANDLE) {
                    vkDestroyDescriptorPool(device_, pool, nullptr);
                }
            }
            pools_.clear();
            active_pool_ = 0;
        }

        // Return every chunk to its initial state.  Only valid once the
        // owning ring slot's submission has completed (begin_frame_batch()
        // waits on that slot's fence first).
        void reset() {
            for (auto pool : pools_) {
                check(vkResetDescriptorPool(device_, pool, 0),
                      "vkResetDescriptorPool(frame descriptor chunk)");
            }
            active_pool_ = 0;
        }

        VkDescriptorSet allocate() {
            if (active_pool_ >= pools_.size()) grow();
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkDescriptorSetAllocateInfo allocation{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                pools_[active_pool_], 1, &layout_};
            const VkResult result =
                vkAllocateDescriptorSets(device_, &allocation, &set);
            if (result == VK_ERROR_OUT_OF_POOL_MEMORY ||
                result == VK_ERROR_FRAGMENTED_POOL) {
                // The current chunk is exhausted: move to the next chunk,
                // growing it geometrically if no chunk is left.
                ++active_pool_;
                return allocate();
            }
            check(result, "vkAllocateDescriptorSets(frame descriptor chunk)");
            return set;
        }

    private:
        void grow() {
            const std::size_t sets = kInitialChunkSets * (1u << pools_.size());
            const VkDescriptorPoolSize pool_size{
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                static_cast<std::uint32_t>(sets * kDescriptorsPerSet)};
            const VkDescriptorPoolCreateInfo pool_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
                static_cast<std::uint32_t>(sets),
                static_cast<std::uint32_t>(sets * kDescriptorsPerSet),
                &pool_size};
            VkDescriptorPool pool = VK_NULL_HANDLE;
            check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool),
                  "vkCreateDescriptorPool(frame descriptor chunk)");
            pools_.push_back(pool);
        }

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
        std::vector<VkDescriptorPool> pools_{};
        std::size_t active_pool_{0};
    };

    /// State of the frame-batch ring.  While active, surface operations
    /// only record commands into the current slot's command buffer (never
    /// submit); the single vkQueueSubmit for the frame happens in
    /// submit_batch() called from end_frame_batch().  Each slot owns its own
    /// command buffer, fence and descriptor allocator so begin_frame_batch()
    /// waits ONLY on the fence of the slot it is about to reuse (never
    /// vkDeviceWaitIdle per frame) and resets only that slot's allocator —
    /// batches still in flight on the other slots keep their descriptor
    /// sets valid.  The ring bounds CPU-GPU overlap: slot N is reused after
    /// kSlotCount batches, so recording can run up to kSlotCount - 1 frames
    /// ahead of GPU execution.
    struct FrameBatchState {
        static constexpr std::size_t kSlotCount = 3;
        bool active{false};
        std::size_t next_slot{0};
        std::array<VkCommandBuffer, kSlotCount> command_buffers{};
        std::array<VkFence, kSlotCount> fences{};
        std::array<bool, kSlotCount> in_flight{};
        std::array<FrameDescriptorAllocator, kSlotCount> descriptor_allocators{};
        std::vector<VkDescriptorSet> descriptor_sets;
        std::size_t pass_count{0};
    };

    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    std::uint32_t queue_family;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore timeline_semaphore{VK_NULL_HANDLE};
    std::uint64_t next_timeline_value{0};
    std::uint64_t pending_timeline_value{0};
    VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, 3> glow_descriptor_sets{};
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    GpuKernelRegistry kernel_registry{};
    VkBuffer staging{VK_NULL_HANDLE};
    VkDeviceMemory staging_memory{VK_NULL_HANDLE};
    VkDeviceSize staging_capacity{0};
    struct UploadSlot {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkCommandBuffer command_buffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkDeviceSize capacity{0};
        std::uint64_t ticket{0};
        bool in_flight{false};
    };
    static constexpr std::size_t kUploadSlotCount = 3;
    std::array<UploadSlot, kUploadSlotCount> upload_slots{};
    std::size_t next_upload_slot{0};
    Image dst{};
    Image src{};
    std::unordered_map<runtime::RenderSurfaceHandle, Image> surfaces;
    FrameBatchState frame_batch{};
    VulkanBackendStats stats{};

    Impl(VkPhysicalDevice physical, VkDevice logical, VkQueue graphics,
         std::uint32_t family, VkCommandPool pool)
        : physical_device(physical), device(logical), queue(graphics),
          queue_family(family), command_pool(pool) {
        VkPhysicalDeviceProperties device_properties{};
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);
        stats.device_name = device_properties.deviceName;
        stats.discrete_gpu =
            device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        const VkDescriptorSetLayoutCreateInfo layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings};
        check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_layout),
              "vkCreateDescriptorSetLayout");

        // One persistent set serves the single-pass operations; glow reuses
        // three additional sets so all three dispatches in its one command
        // buffer retain distinct image bindings until execution.  Frame
        // batches allocate one descriptor set per recorded pass from the
        // current ring slot's own allocator (see FrameBatchState), so the
        // shared pool only ever serves the persistent standalone sets and
        // never needs to be reset on the frame boundary.
        const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512};
        const VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 256, 512, &pool_size};
        check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool),
              "vkCreateDescriptorPool");

        const VkPushConstantRange push_constants{
            VK_SHADER_STAGE_COMPUTE_BIT, 0, 64};
        const VkPipelineLayoutCreateInfo pipeline_layout_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &descriptor_layout,
            1, &push_constants};
        check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout),
              "vkCreatePipelineLayout");

        const VkShaderModuleCreateInfo shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_composite_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_composite_comp_spv)};
        VkShaderModule shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &shader_info, nullptr, &shader),
              "vkCreateShaderModule");
        const VkPipelineShaderStageCreateInfo stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            shader, "main", nullptr};
        const VkComputePipelineCreateInfo pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline composite_pipeline = VK_NULL_HANDLE;
        const VkResult pipeline_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &composite_pipeline);
        vkDestroyShaderModule(device, shader, nullptr);
        check(pipeline_result, "vkCreateComputePipelines");
        if (!kernel_registry.register_kernel(
                GpuKernelId::Composite,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(composite_pipeline))) {
            vkDestroyPipeline(device, composite_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Composite pipeline");
        }

        const VkShaderModuleCreateInfo transform_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_transform_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_transform_comp_spv)};
        VkShaderModule transform_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &transform_shader_info, nullptr, &transform_shader),
              "vkCreateShaderModule(transform)");
        const VkPipelineShaderStageCreateInfo transform_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            transform_shader, "main", nullptr};
        const VkComputePipelineCreateInfo transform_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            transform_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline transform_pipeline = VK_NULL_HANDLE;
        const VkResult transform_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &transform_pipeline_info, nullptr, &transform_pipeline);
        vkDestroyShaderModule(device, transform_shader, nullptr);
        check(transform_result, "vkCreateComputePipelines(transform)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::Transform,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(transform_pipeline))) {
            vkDestroyPipeline(device, transform_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Transform pipeline");
        }

        const VkShaderModuleCreateInfo affine_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_affine_transform_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_affine_transform_comp_spv)};
        VkShaderModule affine_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &affine_shader_info, nullptr, &affine_shader),
              "vkCreateShaderModule(affine transform)");
        const VkPipelineShaderStageCreateInfo affine_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            affine_shader, "main", nullptr};
        const VkComputePipelineCreateInfo affine_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            affine_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline affine_transform_pipeline = VK_NULL_HANDLE;
        const VkResult affine_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &affine_pipeline_info, nullptr, &affine_transform_pipeline);
        vkDestroyShaderModule(device, affine_shader, nullptr);
        check(affine_result, "vkCreateComputePipelines(affine transform)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::AffineTransform,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(affine_transform_pipeline))) {
            vkDestroyPipeline(device, affine_transform_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected AffineTransform pipeline");
        }

        const VkShaderModuleCreateInfo blur_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_blur_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_blur_comp_spv)};
        VkShaderModule blur_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &blur_shader_info, nullptr, &blur_shader),
              "vkCreateShaderModule(blur)");
        const VkPipelineShaderStageCreateInfo blur_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            blur_shader, "main", nullptr};
        const VkComputePipelineCreateInfo blur_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            blur_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline blur_pipeline = VK_NULL_HANDLE;
        const VkResult blur_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &blur_pipeline_info, nullptr, &blur_pipeline);
        vkDestroyShaderModule(device, blur_shader, nullptr);
        check(blur_result, "vkCreateComputePipelines(blur)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::Blur,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(blur_pipeline))) {
            vkDestroyPipeline(device, blur_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Blur pipeline");
        }

        const VkShaderModuleCreateInfo color_adjust_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_color_adjust_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_color_adjust_comp_spv)};
        VkShaderModule color_adjust_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &color_adjust_shader_info, nullptr, &color_adjust_shader),
              "vkCreateShaderModule(color adjust)");
        const VkPipelineShaderStageCreateInfo color_adjust_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            color_adjust_shader, "main", nullptr};
        const VkComputePipelineCreateInfo color_adjust_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            color_adjust_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline color_adjust_pipeline = VK_NULL_HANDLE;
        const VkResult color_adjust_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &color_adjust_pipeline_info, nullptr, &color_adjust_pipeline);
        vkDestroyShaderModule(device, color_adjust_shader, nullptr);
        check(color_adjust_result, "vkCreateComputePipelines(color adjust)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::ColorAdjust,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(color_adjust_pipeline))) {
            vkDestroyPipeline(device, color_adjust_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected ColorAdjust pipeline");
        }

        const VkShaderModuleCreateInfo matte_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_matte_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_matte_comp_spv)};
        VkShaderModule matte_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &matte_shader_info, nullptr, &matte_shader),
              "vkCreateShaderModule(matte)");
        const VkPipelineShaderStageCreateInfo matte_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            matte_shader, "main", nullptr};
        const VkComputePipelineCreateInfo matte_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            matte_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline matte_pipeline = VK_NULL_HANDLE;
        const VkResult matte_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &matte_pipeline_info, nullptr, &matte_pipeline);
        vkDestroyShaderModule(device, matte_shader, nullptr);
        check(matte_result, "vkCreateComputePipelines(matte)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::Matte,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(matte_pipeline))) {
            vkDestroyPipeline(device, matte_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected Matte pipeline");
        }

        const VkCommandBufferAllocateInfo command_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        check(vkAllocateCommandBuffers(device, &command_info, &command_buffer),
              "vkAllocateCommandBuffers");
        // Dedicated command buffers + fences + descriptor pools for the
        // frame-batch ring so batch recording never conflicts with the
        // standalone command buffer used by uploads, downloads and
        // single-pass operations, and each slot can be synchronized and its
        // descriptor pool reset independently of the others.
        const VkCommandBufferAllocateInfo batch_command_info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        for (auto& slot_buffer : frame_batch.command_buffers) {
            check(vkAllocateCommandBuffers(device, &batch_command_info, &slot_buffer),
                  "vkAllocateCommandBuffers(frame batch slot)");
        }
        const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
        for (auto& slot_fence : frame_batch.fences) {
            check(vkCreateFence(device, &fence_info, nullptr, &slot_fence),
                  "vkCreateFence(frame batch slot)");
        }
        for (auto& allocator : frame_batch.descriptor_allocators) {
            allocator.create(device, descriptor_layout);
        }
        check(vkCreateFence(device, &fence_info, nullptr, &fence), "vkCreateFence");
        const VkSemaphoreTypeCreateInfo timeline_type{
            VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr,
            VK_SEMAPHORE_TYPE_TIMELINE, 0};
        const VkSemaphoreCreateInfo timeline_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timeline_type, 0};
        check(vkCreateSemaphore(device, &timeline_info, nullptr, &timeline_semaphore),
              "vkCreateSemaphore(timeline)");
    }

    ~Impl() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        for (auto& [handle, image] : surfaces) {
            (void)handle;
            destroy_image(image);
        }
        destroy_image(dst);
        destroy_image(src);
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, staging_memory, nullptr);
        for (auto& slot : upload_slots) destroy_upload_slot(slot);
        for (auto& allocator : frame_batch.descriptor_allocators) {
            allocator.destroy();
        }
        for (auto& slot_fence : frame_batch.fences) {
            if (slot_fence != VK_NULL_HANDLE) vkDestroyFence(device, slot_fence, nullptr);
        }
        for (auto& slot_buffer : frame_batch.command_buffers) {
            if (slot_buffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, command_pool, 1, &slot_buffer);
            }
        }
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        if (timeline_semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline_semaphore, nullptr);
        for (const auto id : {GpuKernelId::Composite, GpuKernelId::Transform,
                              GpuKernelId::AffineTransform, GpuKernelId::Blur,
                              GpuKernelId::ColorAdjust, GpuKernelId::Matte}) {
            const auto handle = kernel_registry.resolve(id);
            if (handle != 0) {
                vkDestroyPipeline(device,
                    reinterpret_cast<VkPipeline>(handle), nullptr);
            }
        }
        if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    }

    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & required) == required) return i;
        }
        throw std::runtime_error("Vulkan: no compatible memory type");
    }

    void destroy_image(Image& target) {
        if (target.view != VK_NULL_HANDLE) vkDestroyImageView(device, target.view, nullptr);
        if (target.image != VK_NULL_HANDLE) vkDestroyImage(device, target.image, nullptr);
        if (target.memory != VK_NULL_HANDLE) vkFreeMemory(device, target.memory, nullptr);
        target = {};
    }

    void destroy_upload_slot(UploadSlot& slot) {
        if (slot.command_buffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, command_pool, 1, &slot.command_buffer);
        }
        if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(device, slot.fence, nullptr);
        if (slot.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, slot.buffer, nullptr);
        if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(device, slot.memory, nullptr);
        slot = {};
    }

    void make_image(Image& target, std::uint32_t width, std::uint32_t height) {
        const VkImageCreateInfo info{
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D,
            VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, 1, 1,
            VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
        check(vkCreateImage(device, &info, nullptr, &target.image), "vkCreateImage");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, target.image, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &target.memory), "vkAllocateMemory(image)");
        check(vkBindImageMemory(device, target.image, target.memory, 0), "vkBindImageMemory");
        const VkImageViewCreateInfo view{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, target.image,
            VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        check(vkCreateImageView(device, &view, nullptr, &target.view), "vkCreateImageView");
        target.width = width;
        target.height = height;
    }

    void ensure_descriptor_set() {
        if (descriptor_set != VK_NULL_HANDLE) return;
        const VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptor_pool, 1, &descriptor_layout};
        check(vkAllocateDescriptorSets(device, &allocation, &descriptor_set), "vkAllocateDescriptorSets");
    }

    void bind_descriptors(const Image& destination, const Image& source) {
        ensure_descriptor_set();
        write_descriptors(descriptor_set, destination, source);
    }

    void write_descriptors(VkDescriptorSet set,
                           const Image& destination, const Image& source) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo src_info{VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &src_info, nullptr, nullptr}};
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    void write_matte_descriptors(VkDescriptorSet set, const Image& destination,
                                 const Image& target, const Image& matte) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo target_info{VK_NULL_HANDLE, target.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo matte_info{VK_NULL_HANDLE, matte.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &target_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &matte_info, nullptr, nullptr}};
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    VkDescriptorSet ensure_glow_descriptor_set(std::size_t index) {
        auto& set = glow_descriptor_sets[index];
        if (set != VK_NULL_HANDLE) return set;
        const VkDescriptorSetAllocateInfo allocation{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
            descriptor_pool, 1, &descriptor_layout};
        check(vkAllocateDescriptorSets(device, &allocation, &set),
              "vkAllocateDescriptorSets(glow)");
        return set;
    }

    // The command buffer of the slot currently being recorded.  next_slot is
    // stable during a batch; it only advances in submit_batch(), so this is
    // the correct target for every record_* call of the active frame.
    [[nodiscard]] VkCommandBuffer active_command_buffer() const noexcept {
        return frame_batch.command_buffers[frame_batch.next_slot];
    }

    // Allocate a descriptor set for one recorded pass of the active frame
    // batch from the CURRENT slot's allocator.  Each pass binds its own set
    // so the image bindings written now stay valid until end_frame_batch()
    // submits the whole batch; the sets are tracked so begin_frame_batch()
    // invalidates them with the slot's next allocator reset.
    VkDescriptorSet allocate_pass_descriptor_set() {
        const auto set =
            frame_batch.descriptor_allocators[frame_batch.next_slot].allocate();
        frame_batch.descriptor_sets.push_back(set);
        return set;
    }

    // ── record-only kernel primitives ────────────────────────────────────────
    // These functions only append Vulkan commands to the given command
    // buffer: pipeline bind, descriptor bind, push constants, dispatch.
    // They NEVER submit; the caller owns submission (standalone submit() or
    // submit_batch() from end_frame_batch()).  They also never mutate
    // surface state (the `initialized` flags are updated by the operation
    // wrappers).

    void record_composite(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          std::int32_t blend_mode, float source_scale,
                          const float tint[4]) {
        transition(command, destination.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t blend_mode;
            float source_scale;
            float tint[4];
        } params{blend_mode, source_scale, {tint[0], tint[1], tint[2], tint[3]}};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_transform(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          int offset_x, int offset_y, float opacity) {
        transition(command, destination.image,
                   destination.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Transform)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t offset_x;
            std::int32_t offset_y;
            float opacity;
            float padding;
        } push{offset_x, offset_y, opacity, 0.0f};
        vkCmdPushConstants(command, pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_transform_affine(VkCommandBuffer command, VkDescriptorSet descriptors,
                                 const Image& destination, const Image& source,
                                 const runtime::SurfaceAffineTransform& transform) {
        transition(command, destination.image,
                   destination.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::AffineTransform)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(transform), &transform);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_blur(VkCommandBuffer command, VkDescriptorSet descriptors,
                     const Image& destination, const Image& source,
                     float radius, bool horizontal) {
        transition(command, destination.image,
                   destination.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Blur)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float radius;
            std::int32_t horizontal;
        } params{radius, horizontal ? 1 : 0};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_color_adjust(VkCommandBuffer command, VkDescriptorSet descriptors,
                             const Image& destination, const Image& source,
                             float brightness, float contrast,
                             const Color& tint, float tint_amount) {
        transition(command, destination.image,
                   destination.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::ColorAdjust)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            float brightness;
            float contrast;
            float tint_amount;
            float padding;
            float tint[4];
        } params{brightness, contrast, tint_amount, 0.0f,
                 {tint.r, tint.g, tint.b, tint.a}};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_matte(VkCommandBuffer command, VkDescriptorSet descriptors,
                      const Image& destination, const Image& target,
                      const Image& matte, bool luma, bool inverted) {
        transition(command, destination.image,
                   destination.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, target.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        transition(command, matte.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Matte)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t luma;
            std::int32_t inverted;
        } params{luma ? 1 : 0, inverted ? 1 : 0};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    // End the active frame batch's command buffer and submit it exactly once
    // with the current slot's fence.  No wait-for-completion happens here:
    // the caller waits only when that slot is reused (begin_frame_batch())
    // or before a readback (wait_for_pending()).
    void submit_batch() {
        const auto slot = frame_batch.next_slot;
        check(vkEndCommandBuffer(frame_batch.command_buffers[slot]),
              "vkEndCommandBuffer(frame batch)");
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSemaphore signal_semaphores[] = {timeline_semaphore};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit, 0, nullptr, nullptr,
            1, &frame_batch.command_buffers[slot], 1, signal_semaphores};
        check(vkQueueSubmit(queue, 1, &submit_info, frame_batch.fences[slot]),
              "vkQueueSubmit(frame batch)");
        ++stats.submissions;
        frame_batch.in_flight[slot] = true;
        frame_batch.pass_count = 0;
        frame_batch.next_slot = (slot + 1) % FrameBatchState::kSlotCount;
    }

    Image& ensure_surface(runtime::RenderSurfaceHandle handle,
                          const runtime::SurfaceDesc& desc) {
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            desc.format != runtime::PixelFormat::Rgba32Float ||
            desc.width == 0 || desc.height == 0) {
            throw std::invalid_argument("Vulkan surface requires a non-empty Rgba32Float description");
        }
        auto [it, inserted] = surfaces.try_emplace(handle);
        if (inserted || it->second.width != desc.width || it->second.height != desc.height) {
            if (!inserted) destroy_image(it->second);
            make_image(it->second, desc.width, desc.height);
        }
        ensure_descriptor_set();
        return it->second;
    }

    void wait_upload_slot(UploadSlot& slot) {
        if (!slot.in_flight) return;
        check(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences(upload slot)");
        check(vkResetFences(device, 1, &slot.fence), "vkResetFences(upload slot)");
        slot.in_flight = false;
    }

    UploadSlot& acquire_upload_slot(bool wait_for_completion) {
        // Synchronous callers retain the warmed first slot.  Asynchronous
        // callers rotate through the ring and only wait when all slots are
        // occupied, allowing several decoded assets to be queued together.
        if (wait_for_completion) {
            wait_upload_slot(upload_slots[0]);
            return upload_slots[0];
        }
        for (std::size_t attempt = 0; attempt < kUploadSlotCount; ++attempt) {
            auto& slot = upload_slots[next_upload_slot];
            next_upload_slot = (next_upload_slot + 1) % kUploadSlotCount;
            if (!slot.in_flight) return slot;
            if (vkGetFenceStatus(device, slot.fence) == VK_SUCCESS) {
                wait_upload_slot(slot);
                return slot;
            }
        }
        auto& oldest = upload_slots[next_upload_slot];
        wait_upload_slot(oldest);
        next_upload_slot = (next_upload_slot + 1) % kUploadSlotCount;
        return oldest;
    }

    void ensure_upload_slot(UploadSlot& slot, VkDeviceSize bytes) {
        wait_upload_slot(slot);
        if (slot.capacity < bytes) {
            if (slot.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, slot.buffer, nullptr);
            if (slot.memory != VK_NULL_HANDLE) vkFreeMemory(device, slot.memory, nullptr);
            const VkBufferCreateInfo info{
                VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
            check(vkCreateBuffer(device, &info, nullptr, &slot.buffer),
                  "vkCreateBuffer(upload slot)");
            ++stats.staging_allocations;
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, slot.buffer, &requirements);
            const VkMemoryAllocateInfo allocation{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
                memory_type(requirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
            check(vkAllocateMemory(device, &allocation, nullptr, &slot.memory),
                  "vkAllocateMemory(upload slot)");
            check(vkBindBufferMemory(device, slot.buffer, slot.memory, 0),
                  "vkBindBufferMemory(upload slot)");
            slot.capacity = bytes;
        }
        if (slot.command_buffer == VK_NULL_HANDLE) {
            const VkCommandBufferAllocateInfo command_info{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, command_pool,
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
            check(vkAllocateCommandBuffers(device, &command_info, &slot.command_buffer),
                  "vkAllocateCommandBuffers(upload slot)");
            const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
            check(vkCreateFence(device, &fence_info, nullptr, &slot.fence),
                  "vkCreateFence(upload slot)");
        }
    }

    std::uint64_t submit_upload(UploadSlot& slot, bool wait_for_completion) {
        check(vkEndCommandBuffer(slot.command_buffer), "vkEndCommandBuffer(upload slot)");
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSemaphore signal_semaphores[] = {timeline_semaphore};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit, 0, nullptr, nullptr,
            1, &slot.command_buffer, 1, signal_semaphores};
        check(vkQueueSubmit(queue, 1, &submit_info, slot.fence),
              "vkQueueSubmit(upload slot)");
        ++stats.submissions;
        slot.ticket = signal_value;
        slot.in_flight = true;
        if (wait_for_completion) wait_upload_slot(slot);
        return signal_value;
    }

    std::uint64_t upload(runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
                         std::span<const float> rgba, bool wait_for_completion) {
        auto& image = ensure_surface(handle, desc);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(desc.width) * desc.height * sizeof(float) * 4;
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan upload size does not match surface");
        ++stats.upload_calls;
        stats.upload_bytes += bytes;
        auto& slot = acquire_upload_slot(wait_for_completion);
        ensure_upload_slot(slot, bytes);
        void* mapped = nullptr;
        check(vkMapMemory(device, slot.memory, 0, bytes, 0, &mapped), "vkMapMemory(surface upload)");
        std::memcpy(mapped, rgba.data(), static_cast<std::size_t>(bytes));
        vkUnmapMemory(device, slot.memory);
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(slot.command_buffer, 0),
              "vkResetCommandBuffer(upload slot)");
        check(vkBeginCommandBuffer(slot.command_buffer, &begin),
              "vkBeginCommandBuffer(upload slot)");
        transition(slot.command_buffer, image.image,
                   image.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy copy{0, desc.width, desc.height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {desc.width, desc.height, 1}};
        vkCmdCopyBufferToImage(slot.command_buffer, slot.buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(slot.command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        image.initialized = true;
        const auto ticket = submit_upload(slot, wait_for_completion);
        return ticket;
    }

    void wait_upload_ticket(std::uint64_t ticket) {
        const VkSemaphoreWaitInfo wait_info{
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1,
            &timeline_semaphore, &ticket};
        check(vkWaitSemaphores(device, &wait_info, UINT64_MAX),
              "vkWaitSemaphores(upload ticket)");
        for (auto& slot : upload_slots) {
            if (slot.in_flight && slot.ticket == ticket) wait_upload_slot(slot);
        }
    }

    void download(runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
        auto it = surfaces.find(handle);
        if (it == surfaces.end() || !it->second.initialized) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        auto& image = it->second;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(image.width) * image.height * sizeof(float) * 4;
        if (rgba.size_bytes() != bytes) throw std::invalid_argument("Vulkan download size does not match surface");
        ++stats.readback_calls;
        stats.readback_bytes += bytes;
        ensure_staging(bytes);
        begin_command_buffer();
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy copy{0, image.width, image.height,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {image.width, image.height, 1}};
        vkCmdCopyImageToBuffer(command_buffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
        transition(command_buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL);
        submit();
        void* mapped = nullptr;
        check(vkMapMemory(device, staging_memory, 0, bytes, 0, &mapped), "vkMapMemory(surface download)");
        std::memcpy(rgba.data(), mapped, static_cast<std::size_t>(bytes));
        vkUnmapMemory(device, staging_memory);
    }

    void composite(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source, BlendMode mode) {
        auto dst_it = surfaces.find(destination);
        auto src_it = surfaces.find(source);
        if (dst_it == surfaces.end() || src_it == surfaces.end() ||
            !src_it->second.initialized ||
            dst_it->second.width != src_it->second.width ||
            dst_it->second.height != src_it->second.height) {
            throw std::invalid_argument("Vulkan composite references incompatible surfaces");
        }
        auto& dst_image = dst_it->second;
        auto& src_image = src_it->second;
        const std::int32_t blend_mode = mode == BlendMode::Add ? 1 : 0;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            record_composite(active_command_buffer(), descriptors,
                             dst_image, src_image, blend_mode, 1.0f, kIdentityTint);
            ++frame_batch.pass_count;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        record_composite(command_buffer, descriptor_set, dst_image, src_image,
                         blend_mode, 1.0f, kIdentityTint);
        submit();
    }

    void transform(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source,
                   int offset_x, int offset_y, float opacity) {
        auto dst_it = surfaces.find(destination);
        auto src_it = surfaces.find(source);
        if (dst_it == surfaces.end() || src_it == surfaces.end() ||
            !src_it->second.initialized ||
            dst_it->second.width == 0 || dst_it->second.height == 0) {
            throw std::invalid_argument("Vulkan transform references incompatible surfaces");
        }
        auto& dst_image = dst_it->second;
        auto& src_image = src_it->second;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            record_transform(active_command_buffer(), descriptors,
                             dst_image, src_image, offset_x, offset_y, opacity);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        record_transform(command_buffer, descriptor_set, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        submit();
    }

    void transform_affine(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle source,
                          const runtime::SurfaceAffineTransform& transform) {
        auto dst_it = surfaces.find(destination);
        auto src_it = surfaces.find(source);
        if (dst_it == surfaces.end() || src_it == surfaces.end() ||
            !src_it->second.initialized || dst_it->second.width == 0 || dst_it->second.height == 0) {
            throw std::invalid_argument("Vulkan affine transform references incompatible surfaces");
        }
        auto& dst_image = dst_it->second;
        auto& src_image = src_it->second;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            record_transform_affine(active_command_buffer(), descriptors,
                                    dst_image, src_image, transform);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        record_transform_affine(command_buffer, descriptor_set,
                                dst_image, src_image, transform);
        dst_image.initialized = true;
        submit();
    }

    void blur(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source, float radius, bool horizontal) {
        if (!(radius >= 0.0f) || radius > 32.0f) {
            throw std::invalid_argument("Vulkan blur radius must be within [0, 32]");
        }
        auto dst_it = surfaces.find(destination);
        auto src_it = surfaces.find(source);
        if (dst_it == surfaces.end() || src_it == surfaces.end() ||
            !src_it->second.initialized ||
            dst_it->second.width != src_it->second.width ||
            dst_it->second.height != src_it->second.height) {
            throw std::invalid_argument("Vulkan blur references incompatible surfaces");
        }
        auto& dst_image = dst_it->second;
        auto& src_image = src_it->second;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            record_blur(active_command_buffer(), descriptors,
                        dst_image, src_image, radius, horizontal);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        record_blur(command_buffer, descriptor_set,
                    dst_image, src_image, radius, horizontal);
        dst_image.initialized = true;
        submit();
    }

    void glow(runtime::RenderSurfaceHandle destination,
              runtime::RenderSurfaceHandle source,
              runtime::RenderSurfaceHandle scratch_horizontal,
              runtime::RenderSurfaceHandle scratch_vertical,
              float radius, float intensity, const Color& tint) {
        if (!(radius >= 0.0f) || radius > 32.0f) {
            throw std::invalid_argument("Vulkan glow radius must be within [0, 32]");
        }
        auto find_surface = [&](runtime::RenderSurfaceHandle handle) -> Image& {
            const auto it = surfaces.find(handle);
            if (it == surfaces.end()) {
                throw std::invalid_argument("Vulkan glow references an unknown surface");
            }
            return it->second;
        };
        auto& dst = find_surface(destination);
        auto& src = find_surface(source);
        auto& horizontal = find_surface(scratch_horizontal);
        auto& vertical = find_surface(scratch_vertical);
        if (!src.initialized || !dst.initialized ||
            src.width != dst.width || src.height != dst.height ||
            horizontal.width != src.width || horizontal.height != src.height ||
            vertical.width != src.width || vertical.height != src.height) {
            throw std::invalid_argument("Vulkan glow surfaces have incompatible dimensions");
        }
        const float tint_rgba[4] = {tint.r, tint.g, tint.b, tint.a};

        if (frame_batch.active) {
            const auto horizontal_descriptor = allocate_pass_descriptor_set();
            write_descriptors(horizontal_descriptor, horizontal, src);
            const auto vertical_descriptor = allocate_pass_descriptor_set();
            write_descriptors(vertical_descriptor, vertical, horizontal);
            const auto composite_descriptor = allocate_pass_descriptor_set();
            write_descriptors(composite_descriptor, dst, vertical);
            record_blur(active_command_buffer(), horizontal_descriptor,
                        horizontal, src, radius, true);
            record_blur(active_command_buffer(), vertical_descriptor,
                        vertical, horizontal, radius, false);
            record_composite(active_command_buffer(), composite_descriptor,
                             dst, vertical, 1, intensity, tint_rgba);
            horizontal.initialized = true;
            vertical.initialized = true;
            frame_batch.pass_count += 3;
            return;
        }

        begin_command_buffer();
        const auto horizontal_descriptor = ensure_glow_descriptor_set(0);
        write_descriptors(horizontal_descriptor, horizontal, src);
        const auto vertical_descriptor = ensure_glow_descriptor_set(1);
        write_descriptors(vertical_descriptor, vertical, horizontal);
        const auto composite_descriptor = ensure_glow_descriptor_set(2);
        write_descriptors(composite_descriptor, dst, vertical);
        record_blur(command_buffer, horizontal_descriptor,
                    horizontal, src, radius, true);
        record_blur(command_buffer, vertical_descriptor,
                    vertical, horizontal, radius, false);
        record_composite(command_buffer, composite_descriptor,
                         dst, vertical, 1, intensity, tint_rgba);
        horizontal.initialized = true;
        vertical.initialized = true;
        submit();
    }

    void color_adjust(runtime::RenderSurfaceHandle destination,
                      runtime::RenderSurfaceHandle source,
                      float brightness, float contrast,
                      const Color& tint, float tint_amount) {
        auto dst_it = surfaces.find(destination);
        auto src_it = surfaces.find(source);
        if (dst_it == surfaces.end() || src_it == surfaces.end() ||
            !src_it->second.initialized ||
            dst_it->second.width != src_it->second.width ||
            dst_it->second.height != src_it->second.height) {
            throw std::invalid_argument("Vulkan color adjust references incompatible surfaces");
        }
        auto& dst = dst_it->second;
        auto& src = src_it->second;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst, src);
            record_color_adjust(active_command_buffer(), descriptors,
                                dst, src, brightness, contrast, tint, tint_amount);
            dst.initialized = true;
            ++frame_batch.pass_count;
            return;
        }
        bind_descriptors(dst, src);
        begin_command_buffer();
        record_color_adjust(command_buffer, descriptor_set,
                            dst, src, brightness, contrast, tint, tint_amount);
        dst.initialized = true;
        submit();
    }

    void matte(runtime::RenderSurfaceHandle destination,
               runtime::RenderSurfaceHandle target,
               runtime::RenderSurfaceHandle matte_surface,
               bool luma, bool inverted) {
        auto dst_it = surfaces.find(destination);
        auto target_it = surfaces.find(target);
        auto matte_it = surfaces.find(matte_surface);
        if (dst_it == surfaces.end() || target_it == surfaces.end() || matte_it == surfaces.end() ||
            !target_it->second.initialized || !matte_it->second.initialized ||
            dst_it->second.width != target_it->second.width ||
            dst_it->second.height != target_it->second.height ||
            dst_it->second.width != matte_it->second.width ||
            dst_it->second.height != matte_it->second.height) {
            throw std::invalid_argument("Vulkan matte references incompatible surfaces");
        }
        auto& dst = dst_it->second;
        auto& target_image = target_it->second;
        auto& matte_image = matte_it->second;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_matte_descriptors(descriptors, dst, target_image, matte_image);
            record_matte(active_command_buffer(), descriptors,
                         dst, target_image, matte_image, luma, inverted);
            dst.initialized = true;
            ++frame_batch.pass_count;
            return;
        }
        ensure_descriptor_set();
        write_matte_descriptors(descriptor_set, dst, target_image, matte_image);
        begin_command_buffer();
        record_matte(command_buffer, descriptor_set,
                     dst, target_image, matte_image, luma, inverted);
        dst.initialized = true;
        submit();
    }

    void ensure_images(std::uint32_t width, std::uint32_t height) {
        if (dst.width == width && dst.height == height && src.image != VK_NULL_HANDLE) return;
        destroy_image(dst);
        destroy_image(src);
        check(vkResetDescriptorPool(device, descriptor_pool, 0), "vkResetDescriptorPool");
        make_image(dst, width, height);
        make_image(src, width, height);
        descriptor_set = VK_NULL_HANDLE;
        glow_descriptor_sets = {};
        bind_descriptors(dst, src);
    }

    void ensure_staging(VkDeviceSize bytes) {
        if (staging_capacity >= bytes) return;
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, staging_memory, nullptr);
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &staging), "vkCreateBuffer(staging)");
        ++stats.staging_allocations;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &staging_memory), "vkAllocateMemory(staging)");
        check(vkBindBufferMemory(device, staging, staging_memory, 0), "vkBindBufferMemory");
        staging_capacity = bytes;
    }

    static void transition(VkCommandBuffer command, VkImage image,
                           VkImageLayout old_layout, VkImageLayout new_layout) {
        const VkImageMemoryBarrier barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            old_layout, new_layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void begin_command_buffer() {
        wait_for_pending();
        const VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
    }

    void wait_for_pending() {
        if (pending_timeline_value != 0) {
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            check(vkResetFences(device, 1, &fence), "vkResetFences");
            pending_timeline_value = 0;
        }
        // Synchronize any frame-batch slots still in flight.  This is a sync
        // point (release/readback path), distinct from begin_frame_batch()
        // which waits ONLY on the fence of the slot being reused.
        for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
            if (!frame_batch.in_flight[i]) continue;
            check(vkWaitForFences(device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(frame batch slot)");
            check(vkResetFences(device, 1, &frame_batch.fences[i]),
                  "vkResetFences(frame batch slot)");
            frame_batch.in_flight[i] = false;
        }
    }

    std::uint64_t submit(bool wait_for_completion = true) {
        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
        const auto signal_value = ++next_timeline_value;
        const VkTimelineSemaphoreSubmitInfo timeline_submit{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
            0, nullptr, 1, &signal_value};
        const VkSemaphore signal_semaphores[] = {timeline_semaphore};
        const VkSubmitInfo submit_info{
            VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit, 0, nullptr, nullptr,
            1, &command_buffer, 1, signal_semaphores};
        check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
        ++stats.submissions;
        pending_timeline_value = signal_value;
        if (wait_for_completion) wait_for_pending();
        return signal_value;
    }

    void composite(Framebuffer& destination, const Framebuffer& source) {
        const auto width = static_cast<std::uint32_t>(destination.width());
        const auto height = static_cast<std::uint32_t>(destination.height());
        const VkDeviceSize image_bytes = static_cast<VkDeviceSize>(width) * height * sizeof(float) * 4;
        ensure_images(width, height);
        ensure_staging(image_bytes * 3);

        std::vector<float> packed(static_cast<std::size_t>(width) * height * 8);
        auto pack = [&](const Framebuffer& framebuffer, std::size_t offset) {
            std::size_t index = offset / sizeof(float);
            for (int y = 0; y < framebuffer.height(); ++y) {
                for (int x = 0; x < framebuffer.width(); ++x) {
                    const auto color = framebuffer.get_pixel(x, y);
                    packed[index++] = color.r;
                    packed[index++] = color.g;
                    packed[index++] = color.b;
                    packed[index++] = color.a;
                }
            }
        };
        pack(source, 0);
        pack(destination, static_cast<std::size_t>(image_bytes));
        void* mapped = nullptr;
        check(vkMapMemory(device, staging_memory, 0, image_bytes * 2, 0, &mapped), "vkMapMemory(upload)");
        std::memcpy(mapped, packed.data(), static_cast<std::size_t>(image_bytes * 2));
        vkUnmapMemory(device, staging_memory);

        const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
        transition(command_buffer, src.image,
                   src.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        transition(command_buffer, dst.image,
                   dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkBufferImageCopy source_copy{0, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        const VkBufferImageCopy destination_copy{image_bytes, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        vkCmdCopyBufferToImage(command_buffer, staging, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &source_copy);
        vkCmdCopyBufferToImage(command_buffer, staging, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &destination_copy);
        transition(command_buffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
        vkCmdDispatch(command_buffer, (width + 15) / 16, (height + 15) / 16, 1);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkBufferImageCopy output_copy{image_bytes * 2, width, height, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {width, height, 1}};
        vkCmdCopyImageToBuffer(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &output_copy);
        transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        src.initialized = true;
        dst.initialized = true;
        submit();

        check(vkMapMemory(device, staging_memory, image_bytes * 2, image_bytes, 0, &mapped), "vkMapMemory(readback)");
        const float* output = static_cast<const float*>(mapped);
        for (int y = 0; y < destination.height(); ++y) {
            for (int x = 0; x < destination.width(); ++x) {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
                destination.set_pixel(x, y, Color{output[index], output[index + 1], output[index + 2], output[index + 3]});
            }
        }
        vkUnmapMemory(device, staging_memory);
    }
};
#endif

VulkanBackend::VulkanBackend() {
#ifdef CHRONON3D_ENABLE_VULKAN
    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Chronon3D",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Chronon3D",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr};
    check(vkCreateInstance(&instance_info, nullptr, &m_instance),
          "vkCreateInstance");

    std::uint32_t device_count = 0;
    check(vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr),
          "vkEnumeratePhysicalDevices(count)");
    if (device_count == 0) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        throw std::runtime_error("Vulkan: no physical device available");
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    check(vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data()),
          "vkEnumeratePhysicalDevices");

    int best_score = -1;
    for (const auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        int device_score = 0;
        switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_score = 300; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_score = 200; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: device_score = 100; break;
            default: device_score = 0; break;
        }
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count,
                                                 families.data());
        for (std::uint32_t i = 0; i < family_count; ++i) {
            if (families[i].queueCount != 0 &&
                (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                if (device_score > best_score) {
                    m_physical_device = device;
                    m_queue_family = i;
                    best_score = device_score;
                }
                break;
            }
        }
    }
    if (m_physical_device == VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        throw std::runtime_error("Vulkan: no graphics queue family available");
    }

    constexpr float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = m_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority};
    const VkPhysicalDeviceFeatures features{};
    const VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        nullptr, VK_TRUE};
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &timeline_features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr,
        .pEnabledFeatures = &features};
    check(vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device),
          "vkCreateDevice");
    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_queue_family};
    check(vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool),
          "vkCreateCommandPool");
    m_impl = std::make_unique<Impl>(m_physical_device, m_device, m_queue,
                                    m_queue_family, m_command_pool);
#else
    throw std::runtime_error("Vulkan backend was not compiled");
#endif
}

VulkanBackend::~VulkanBackend() {
#ifdef CHRONON3D_ENABLE_VULKAN
    m_impl.reset();
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        if (m_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);
        }
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
#endif
}

VulkanBackend::VulkanBackend(VulkanBackend&& other) noexcept {
    *this = std::move(other);
}

VulkanBackend& VulkanBackend::operator=(VulkanBackend&& other) noexcept {
    if (this == &other) return *this;
#ifdef CHRONON3D_ENABLE_VULKAN
    std::swap(m_impl, other.m_impl);
    std::swap(m_instance, other.m_instance);
    std::swap(m_physical_device, other.m_physical_device);
    std::swap(m_device, other.m_device);
    std::swap(m_queue, other.m_queue);
    std::swap(m_command_pool, other.m_command_pool);
    std::swap(m_queue_family, other.m_queue_family);
#else
    (void)other;
#endif
    return *this;
}

graph::RenderCapabilities VulkanBackend::capabilities() const noexcept {
    return graph::RenderCapabilities{};
}

VulkanBackendStats VulkanBackend::stats() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return m_impl ? m_impl->stats : VulkanBackendStats{};
#else
    return {};
#endif
}

const GpuKernelRegistry& VulkanBackend::kernel_registry() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    static const GpuKernelRegistry empty{};
    return m_impl ? m_impl->kernel_registry : empty;
#else
    static const GpuKernelRegistry empty{};
    return empty;
#endif
}

void VulkanBackend::begin_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    auto& batch = m_impl->frame_batch;
    if (batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_frame_batch: a frame batch is already active");
    }
    const auto slot = batch.next_slot;
    // Wait ONLY on the fence of the slot being reused.  The other slots may
    // still be in flight; this is what bounds CPU-GPU overlap to the ring
    // size instead of stalling the whole device every frame.
    if (batch.in_flight[slot]) {
        check(vkWaitForFences(m_impl->device, 1, &batch.fences[slot], VK_TRUE, UINT64_MAX),
              "vkWaitForFences(frame batch slot)");
        check(vkResetFences(m_impl->device, 1, &batch.fences[slot]),
              "vkResetFences(frame batch slot)");
        batch.in_flight[slot] = false;
    }
    // Every recorded pass owns a descriptor set from this slot's allocator;
    // resetting it now is safe because the slot's previous submission (the
    // only one referencing those sets) has completed.
    batch.descriptor_allocators[slot].reset();
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr};
    check(vkResetCommandBuffer(batch.command_buffers[slot], 0),
          "vkResetCommandBuffer(frame batch slot)");
    check(vkBeginCommandBuffer(batch.command_buffers[slot], &begin),
          "vkBeginCommandBuffer(frame batch slot)");
    batch.active = true;
    batch.pass_count = 0;
    batch.descriptor_sets.clear();
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::end_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    auto& batch = m_impl->frame_batch;
    if (!batch.active) return;
    m_impl->submit_batch();
    batch.active = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

graph::RenderOpResult VulkanBackend::create_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        (void)m_impl->ensure_surface(handle, desc);
        ++m_impl->stats.surface_creations;
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput, error.what()});
    }
#else
    (void)handle; (void)desc;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::create_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::release_surface(
    runtime::RenderSurfaceHandle handle) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::release_surface: invalid handle"});
        }
        const auto it = m_impl->surfaces.find(handle);
        if (it == m_impl->surfaces.end()) {
            return graph::RenderOpResult(graph::RenderOpOutcome{});
        }
        // Surface memory may be referenced by the last asynchronous upload
        // or render submission.  Reclaim only after the backend's completion
        // fence, otherwise cache eviction can destroy an in-flight VkImage.
        m_impl->wait_for_pending();
        m_impl->destroy_image(it->second);
        m_impl->surfaces.erase(it);
        ++m_impl->stats.surface_releases;
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::release_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::span<const float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        (void)m_impl->upload(handle, desc, rgba, true);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)desc; (void)rgba;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::upload_surface_async(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc,
    std::span<const float> rgba, runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        ticket.value = m_impl->upload(handle, desc, rgba, false);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        ticket = {};
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)desc; (void)rgba; (void)ticket;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::upload_surface_async: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::wait_upload(const runtime::UploadTicket& ticket) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        if (!ticket.valid()) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::wait_upload: invalid ticket"});
        }
        if (ticket.value > m_impl->next_timeline_value) {
            return graph::RenderOpResult(graph::RenderBackendError{
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::wait_upload: unknown ticket"});
        }
        m_impl->wait_upload_ticket(ticket.value);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)ticket;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::wait_upload: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::download_surface(
    runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->download(handle, rgba);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)handle; (void)rgba;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::download_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::composite_surfaces(
    runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
    BlendMode mode, CompositeOperator op) {
    if ((mode != BlendMode::Normal && mode != BlendMode::Add) ||
        op != CompositeOperator::SourceOver) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "VulkanBackend::composite_surfaces: only Normal/Add SourceOver is implemented"});
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->composite(destination, source, mode);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::composite_surfaces: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    int offset_x, int offset_y, float opacity) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->transform(destination, source, offset_x, offset_y, opacity);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)offset_x; (void)offset_y; (void)opacity;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->transform_affine(destination, source, transform);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)transform;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface_affine: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::blur_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source, float radius, bool horizontal) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->blur(destination, source, radius, horizontal);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)radius; (void)horizontal;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::blur_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::glow_surfaces(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    float radius, float intensity, const Color& tint) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->glow(destination, source, scratch_horizontal, scratch_vertical,
                     radius, intensity, tint);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)scratch_horizontal;
    (void)scratch_vertical; (void)radius; (void)intensity; (void)tint;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::glow_surfaces: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::color_adjust_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float brightness, float contrast, const Color& tint, float tint_amount) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->color_adjust(destination, source, brightness, contrast, tint, tint_amount);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)source; (void)brightness; (void)contrast;
    (void)tint; (void)tint_amount;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::color_adjust_surface: Vulkan support is disabled"});
#endif
}

graph::RenderOpResult VulkanBackend::matte_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle target,
    runtime::RenderSurfaceHandle matte,
    bool luma, bool inverted) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->matte(destination, target, matte, luma, inverted);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)target; (void)matte; (void)luma; (void)inverted;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::matte_surface: Vulkan support is disabled"});
#endif
}

void VulkanBackend::unsupported(const char* operation) {
    throw std::runtime_error(std::string{"VulkanBackend::"} + operation +
                             ": RenderSurface execution is not wired yet");
}

void VulkanBackend::apply_per_pixel_dof(Framebuffer&, std::span<const float>,
                                        const DepthOfFieldSettings&,
                                        const LensModel&,
                                        const std::optional<raster::BBox>&) {
    unsupported("apply_per_pixel_dof");
}
void VulkanBackend::draw_node(Framebuffer&, const RenderNode&, const RenderState&,
                              const Camera&, int, int) {
    unsupported("draw_node");
}
void VulkanBackend::apply_effect_stack(Framebuffer&, const EffectStack&,
                                       const effects::EffectExecutionContext&) {
    unsupported("apply_effect_stack");
}
void VulkanBackend::composite_layer(Framebuffer& destination, const Framebuffer& source,
                                    BlendMode mode, const std::optional<raster::BBox>& clip,
                                    CompositeOperator op) {
    if (mode != BlendMode::Normal || op != CompositeOperator::SourceOver) {
        throw std::runtime_error("VulkanBackend::composite_layer: only Normal/SourceOver is implemented");
    }
    if (clip) {
        throw std::runtime_error("VulkanBackend::composite_layer: clipped surfaces are not implemented");
    }
    if (destination.width() != source.width() || destination.height() != source.height()) {
        throw std::runtime_error("VulkanBackend::composite_layer: surface dimensions differ");
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    m_impl->composite(destination, source);
#else
    unsupported("composite_layer");
#endif
}
void VulkanBackend::apply_blur(Framebuffer&, float,
                               const std::optional<raster::BBox>&) {
    unsupported("apply_blur");
}

std::unique_ptr<graph::RenderBackend> make_vulkan_backend() {
    return std::make_unique<VulkanBackend>();
}

} // namespace chronon3d::backends::vulkan
