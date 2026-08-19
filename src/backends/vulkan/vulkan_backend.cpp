#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "composite_comp_spv.hpp"
#include "transform_comp_spv.hpp"
#include "affine_transform_comp_spv.hpp"
#include "blur_comp_spv.hpp"
#include "color_adjust_comp_spv.hpp"
#include "matte_comp_spv.hpp"
#include "text_run_comp_spv.hpp"
#include "fill_rect_comp_spv.hpp"
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
        // (matte uses all three; the other kernels use two) plus one
        // storage-buffer binding (the text-run kernel's glyph instances).
        // Pool sizing over-reserves so any pass can allocate safely.
        static constexpr std::size_t kStorageImagesPerSet = 3;
        static constexpr std::size_t kStorageBuffersPerSet = 1;

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
            const VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 static_cast<std::uint32_t>(sets * kStorageImagesPerSet)},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 static_cast<std::uint32_t>(sets * kStorageBuffersPerSet)}};
            const VkDescriptorPoolCreateInfo pool_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
                static_cast<std::uint32_t>(sets), 2, pool_sizes};
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
        // When set, the batch is plan-driven: ops synchronize through this
        // BarrierPlan (via begin_plan_batch) instead of the conservative
        // fallback.  pass_count doubles as the plan pass index: ops are
        // called in plan order and each op advances it by one.
        const runtime::BarrierPlan* sync_plan{nullptr};
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
    // Device-local storage buffer for the text-run kernel's glyph instances.
    // Updated in-band via vkCmdUpdateBuffer so each recorded dispatch reads
    // its own instance data even when several text runs accumulate in one
    // command batch (a host-visible reuse would clobber earlier overlays).
    VkBuffer glyph_instance_buffer{VK_NULL_HANDLE};
    VkDeviceMemory glyph_instance_memory{VK_NULL_HANDLE};
    VkDeviceSize glyph_instance_capacity{0};
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

    // ── Logical→physical surface ownership ───────────────────────────────
    // A physical slot owns exactly one VkImage (PhysicalSurface); logical
    // handles bind to slots, and several handles may bind the SAME slot when
    // their lifetimes never overlap (plan-driven aliasing).  Ownership is
    // therefore separated from identity: destroying a handle binding must
    // never destroy a VkImage still referenced by another handle.
    // resolve_image() is the single lookup path for every operation.
    struct PhysicalSurface {
        Image image;
        runtime::SurfaceDesc desc{};
    };
    // slot → backing image (ownership lives here, exactly once per slot)
    std::unordered_map<std::size_t, PhysicalSurface> physical_surfaces;
    // handle → slot (identity only; no ownership)
    std::unordered_map<runtime::RenderSurfaceHandle, std::size_t> surface_bindings;
    // Temporary native passes may release a logical handle while the active
    // frame batch is still recording commands that reference its image. Keep
    // the Vulkan binding alive until the batch has been submitted.
    std::vector<runtime::RenderSurfaceHandle> deferred_surface_releases;
    std::unordered_set<runtime::RenderSurfaceHandle> unplanned_surface_handles;
    std::size_t next_slot{0};
    // Last access kind per PHYSICAL SLOT within the current frame's
    // plan-driven batch, consumed by emit_plan_pass_barriers() to derive the
    // precise write→read / read→write / write→write memory dependencies.
    // Keyed by slot (not by logical handle) because aliased handles share
    // one image: the barrier chain must follow the image, not the identity.
    // Cleared at begin_frame_batch(); conservative batches never touch it.
    std::unordered_map<std::size_t, runtime::ResourceAccess> m_slot_last_access{};
    FrameBatchState frame_batch{};
    // Command-batch state: while active, end_frame_batch() defers the single
    // submission it would otherwise perform and keeps recording into the SAME
    // command buffer, so N overlays (N frame batches) accumulate and are
    // submitted with exactly one vkQueueSubmit at end_command_batch().
    // command_batch_started distinguishes the first frame (which opens the
    // buffer) from subsequent frames (which only flush a boundary barrier and
    // reset per-frame bookkeeping).
    bool command_batch_active{false};
    bool command_batch_started{false};
    // Frame-level GPU timing: a VkQueryPool of VK_QUERY_TYPE_TIMESTAMP with
    // two queries per frame-batch ring slot (start + end).  Null when the
    // device does not expose timestamp support.
    VkQueryPool timestamp_pool{VK_NULL_HANDLE};
    float timestamp_period_ns{0.0f};
    std::uint32_t timestamp_valid_bits{0};
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
        timestamp_period_ns = device_properties.limits.timestampPeriod;
        // timestampValidBits is a property of the selected queue family, not
        // VkPhysicalDeviceLimits.  Read it from the same family used for the
        // graphics queue so timestamp queries are enabled only when that
        // queue can actually report them.
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
        if (queue_family < families.size()) {
            timestamp_valid_bits = families[queue_family].timestampValidBits;
        }
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        const VkDescriptorSetLayoutCreateInfo layout_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 4, bindings};
        check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_layout),
              "vkCreateDescriptorSetLayout");

        // One persistent set serves the single-pass operations; glow reuses
        // three additional sets so all three dispatches in its one command
        // buffer retain distinct image bindings until execution.  Frame
        // batches allocate one descriptor set per recorded pass from the
        // current ring slot's own allocator (see FrameBatchState), so the
        // shared pool only ever serves the persistent standalone sets and
        // never needs to be reset on the frame boundary.
        const VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64}};
        const VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            256, 2, pool_sizes};
        check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool),
              "vkCreateDescriptorPool");

        const VkPushConstantRange push_constants{
            VK_SHADER_STAGE_COMPUTE_BIT, 0, 96};
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

        const VkShaderModuleCreateInfo text_run_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_text_run_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_text_run_comp_spv)};
        VkShaderModule text_run_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &text_run_shader_info, nullptr, &text_run_shader),
              "vkCreateShaderModule(text run)");
        const VkPipelineShaderStageCreateInfo text_run_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            text_run_shader, "main", nullptr};
        const VkComputePipelineCreateInfo text_run_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            text_run_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline text_run_pipeline = VK_NULL_HANDLE;
        const VkResult text_run_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &text_run_pipeline_info, nullptr, &text_run_pipeline);
        vkDestroyShaderModule(device, text_run_shader, nullptr);
        check(text_run_result, "vkCreateComputePipelines(text run)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::TextRun,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(text_run_pipeline))) {
            vkDestroyPipeline(device, text_run_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected TextRun pipeline");
        }

        const VkShaderModuleCreateInfo fill_rect_shader_info{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0,
            shaders::chronon3d_fill_rect_comp_spv_size,
            reinterpret_cast<const std::uint32_t*>(shaders::chronon3d_fill_rect_comp_spv)};
        VkShaderModule fill_rect_shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &fill_rect_shader_info, nullptr, &fill_rect_shader),
              "vkCreateShaderModule(fill rect)");
        const VkPipelineShaderStageCreateInfo fill_rect_stage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
            VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
            fill_rect_shader, "main", nullptr};
        const VkComputePipelineCreateInfo fill_rect_pipeline_info{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            fill_rect_stage, pipeline_layout, VK_NULL_HANDLE, -1};
        VkPipeline fill_rect_pipeline = VK_NULL_HANDLE;
        const VkResult fill_rect_result = vkCreateComputePipelines(
            device, VK_NULL_HANDLE, 1, &fill_rect_pipeline_info, nullptr, &fill_rect_pipeline);
        vkDestroyShaderModule(device, fill_rect_shader, nullptr);
        check(fill_rect_result, "vkCreateComputePipelines(fill rect)");
        if (!kernel_registry.register_kernel(
                GpuKernelId::FillRect,
                reinterpret_cast<GpuKernelRegistry::PipelineHandle>(fill_rect_pipeline))) {
            vkDestroyPipeline(device, fill_rect_pipeline, nullptr);
            throw std::runtime_error("Vulkan kernel registry rejected FillRect pipeline");
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

        if (timestamp_valid_bits != 0) {
            const VkQueryPoolCreateInfo query_pool_info{
                VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0,
                VK_QUERY_TYPE_TIMESTAMP,
                static_cast<std::uint32_t>(2 * FrameBatchState::kSlotCount), 0};
            check(vkCreateQueryPool(device, &query_pool_info, nullptr, &timestamp_pool),
                  "vkCreateQueryPool(timestamp)");
        }
    }

    ~Impl() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        for (auto& [slot, physical] : physical_surfaces) {
            (void)slot;
            destroy_image(physical.image);
        }
        destroy_image(dst);
        destroy_image(src);
        if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
        if (staging_memory != VK_NULL_HANDLE) vkFreeMemory(device, staging_memory, nullptr);
        if (glyph_instance_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, glyph_instance_buffer, nullptr);
        }
        if (glyph_instance_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, glyph_instance_memory, nullptr);
        }
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
                              GpuKernelId::ColorAdjust, GpuKernelId::Matte,
                              GpuKernelId::TextRun, GpuKernelId::FillRect}) {
            const auto handle = kernel_registry.resolve(id);
            if (handle != 0) {
                vkDestroyPipeline(device,
                    reinterpret_cast<VkPipeline>(handle), nullptr);
            }
        }
        if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (timestamp_pool != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestamp_pool, nullptr);
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

    // fill_rect has a single-image binding (destination only, no source).
    void write_fill_rect_descriptors(VkDescriptorSet set, const Image& destination) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr}};
        vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
    }

    void bind_fill_rect_descriptors(const Image& destination) {
        ensure_descriptor_set();
        write_fill_rect_descriptors(descriptor_set, destination);
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

    void write_text_run_descriptors(VkDescriptorSet set, const Image& destination,
                                    const Image& atlas, VkBuffer instance_buffer) {
        const VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, destination.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo atlas_info{VK_NULL_HANDLE, atlas.view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorBufferInfo buffer_info{instance_buffer, 0, VK_WHOLE_SIZE};
        const VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dst_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &atlas_info, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer_info, nullptr}};
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

    // ── synchronization helpers (single emission site) ───────────────────────
    // Layout transitions and memory barriers for the pass pipeline are built
    // ONLY here.  Kernels (record_*) never synchronize; the operation
    // wrappers route through either the BarrierPlan mapper (plan-driven
    // batches via begin_plan_batch) or the conservative fallback (standalone
    // ops and direct op calls without a plan).

    VkImageMemoryBarrier make_image_barrier(const Image& image,
                                            VkImageLayout old_layout,
                                            VkImageLayout new_layout,
                                            VkAccessFlags src_access,
                                            VkAccessFlags dst_access) const {
        return VkImageMemoryBarrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
            src_access, dst_access, old_layout, new_layout,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image.image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    }

    void emit_barriers(VkCommandBuffer command, VkPipelineStageFlags src_stage,
                       VkPipelineStageFlags dst_stage,
                       const std::vector<VkImageMemoryBarrier>& barriers) {
        if (barriers.empty()) return;
        vkCmdPipelineBarrier(command, src_stage, dst_stage, 0,
                             0, nullptr, 0, nullptr,
                             static_cast<std::uint32_t>(barriers.size()),
                             barriers.data());
    }

    // Conservative fallback for plan-less paths (standalone ops and direct
    // op calls inside a plain begin_frame_batch): one full image memory
    // barrier per accessed surface plus the first-write layout transition.
    // This reproduces the legacy per-pass synchronization exactly, but lives
    // in ONE place instead of being duplicated inside every kernel.
    void emit_conservative_pass_sync(VkCommandBuffer command,
                                     std::initializer_list<const Image*> images) {
        std::vector<VkImageMemoryBarrier> barriers;
        barriers.reserve(images.size());
        for (const Image* image : images) {
            barriers.push_back(make_image_barrier(
                *image,
                image->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
        }
        emit_barriers(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, barriers);
    }

    // Route one pass's synchronization: the BarrierPlan mapper when the
    // batch is plan-driven, the conservative fallback otherwise.
    void emit_pass_sync(VkCommandBuffer command,
                        std::initializer_list<const Image*> images) {
        if (frame_batch.sync_plan) {
            emit_plan_pass_barriers(command, *frame_batch.sync_plan,
                                    frame_batch.pass_count);
            for (const Image* image : images) {
                bool unplanned = false;
                for (const auto handle : unplanned_surface_handles) {
                    const auto binding = surface_bindings.find(handle);
                    if (binding == surface_bindings.end()) continue;
                    const auto physical = physical_surfaces.find(binding->second);
                    if (physical != physical_surfaces.end() &&
                        &physical->second.image == image) {
                        unplanned = true;
                        break;
                    }
                }
                if (unplanned) {
                    emit_conservative_pass_sync(command, images);
                    break;
                }
            }
        } else {
            emit_conservative_pass_sync(command, images);
        }
    }

    // Cross-overlay boundary inside a command batch.  When the next overlay
    // begins, every physical image the previous overlay wrote must be visible
    // before any of the next overlay's passes sample it, regardless of how
    // logical handles alias slots across the two overlays.  A single
    // conservative full-barrier over all initialized images is the safe,
    // per-boundary cost (once per overlay, not per pass).
    void emit_command_batch_boundary() {
        std::vector<VkImageMemoryBarrier> barriers;
        for (auto& [slot, physical] : physical_surfaces) {
            (void)slot;
            if (!physical.image.initialized) continue;
            barriers.push_back(make_image_barrier(
                physical.image,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
        }
        emit_barriers(active_command_buffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, barriers);
    }

    // The single BarrierPlan→Vulkan mapper.  Emits, into `command`, the
    // image memory barriers the plan requires before the pass at
    // `pass_index` records its dispatch:
    //   * an in-frame previous access becomes a compute-stage memory barrier
    //     (SHADER_WRITE / SHADER_READ source access from the previous
    //     transition, destination access from the current one) — this covers
    //     the write→read / read→write / write→write chains between passes;
    //   * a first write to a never-initialized surface becomes an
    //     UNDEFINED→GENERAL layout transition (contents discarded);
    //   * a first read needs no barrier: every previous submission (uploads,
    //     earlier batches) is queued before this one on the same queue, so
    //     FIFO ordering already made it visible.
    void emit_plan_pass_barriers(VkCommandBuffer command,
                                 const runtime::BarrierPlan& plan,
                                 std::size_t pass_index) {
        std::vector<VkImageMemoryBarrier> barriers;
        for (const auto& transition : plan.transitions) {
            if (transition.pass_index != pass_index) continue;
            if (transition.surface == runtime::kInvalidRenderSurfaceHandle) continue;
            const auto binding = surface_bindings.find(transition.surface);
            if (binding == surface_bindings.end()) continue;
            const auto slot = binding->second;
            const auto physical_it = physical_surfaces.find(slot);
            if (physical_it == physical_surfaces.end()) continue;
            const auto& image = physical_it->second.image;
            const bool is_write =
                transition.access == runtime::ResourceAccess::Write ||
                transition.access == runtime::ResourceAccess::ReadWrite;
            const auto prev_it = m_slot_last_access.find(slot);
            if (prev_it != m_slot_last_access.end()) {
                const bool prev_write =
                    prev_it->second == runtime::ResourceAccess::Write ||
                    prev_it->second == runtime::ResourceAccess::ReadWrite;
                barriers.push_back(make_image_barrier(
                    image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    prev_write ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT,
                    is_write ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT));
            } else if (is_write && !image.initialized) {
                barriers.push_back(make_image_barrier(
                    image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT));
            }
            m_slot_last_access[slot] = transition.access;
        }
        emit_barriers(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, barriers);
    }

    // ── record-only kernel primitives ────────────────────────────────────────
    // These functions only append Vulkan commands to the given command
    // buffer: pipeline bind, descriptor bind, push constants, dispatch.
    // They NEVER submit and they NEVER synchronize — layout transitions and
    // memory barriers are emitted by the caller through the single sync
    // helpers below (the BarrierPlan mapper, or the conservative fallback
    // for plan-less calls).  They also never mutate surface state (the
    // `initialized` flags are updated by the operation wrappers).

    void record_composite(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          std::int32_t blend_mode, float source_scale,
                          const float tint[4],
                          const std::optional<raster::BBox>& clip = std::nullopt) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::Composite)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t blend_mode;
            float source_scale;
            float tint[4];
            std::int32_t clip_rect[4];
        } params{blend_mode, source_scale, {tint[0], tint[1], tint[2], tint[3]},
                 {0, 0, static_cast<std::int32_t>(destination.width),
                  static_cast<std::int32_t>(destination.height)}};
        if (clip) {
            params.clip_rect[0] = std::max(0, clip->x0);
            params.clip_rect[1] = std::max(0, clip->y0);
            params.clip_rect[2] = std::min(static_cast<std::int32_t>(destination.width), clip->x1);
            params.clip_rect[3] = std::min(static_cast<std::int32_t>(destination.height), clip->y1);
        }
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_transform(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination, const Image& source,
                          int offset_x, int offset_y, float opacity) {
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

    void record_fill_rect(VkCommandBuffer command, VkDescriptorSet descriptors,
                          const Image& destination,
                          std::int32_t x0, std::int32_t y0,
                          std::int32_t x1, std::int32_t y1,
                          const Color& color) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::FillRect)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t rect[4];
            float color[4];
        } params{{x0, y0, x1, y1}, {color.r, color.g, color.b, color.a}};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    void record_text_run(VkCommandBuffer command, VkDescriptorSet descriptors,
                         const Image& destination, std::int32_t glyph_count) {
        // The glyph instances were written by a preceding vkCmdUpdateBuffer
        // (transfer stage); publish them to the compute shader before dispatch.
        const VkBufferMemoryBarrier barrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            glyph_instance_buffer, 0, VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             0, nullptr, 1, &barrier, 0, nullptr);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reinterpret_cast<VkPipeline>(kernel_registry.resolve(GpuKernelId::TextRun)));
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_layout, 0, 1, &descriptors, 0, nullptr);
        struct PushConstants {
            std::int32_t glyph_count;
        } params{glyph_count};
        vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command, (destination.width + 15) / 16,
                      (destination.height + 15) / 16, 1);
    }

    // Read the frame's [start, end] timestamp pair for a ring slot after its
    // fence has been signaled and accumulate the GPU elapsed duration.  The
    // slot's queries are reset by vkCmdResetQueryPool when the buffer is
    // re-recorded, so this must run after the fence wait and before that
    // reset executes on the GPU.
    void read_gpu_timestamps(std::size_t slot) {
        if (timestamp_pool == VK_NULL_HANDLE) return;
        std::uint64_t stamps[2] = {0, 0};
        const VkResult result = vkGetQueryPoolResults(
            device, timestamp_pool, static_cast<std::uint32_t>(2 * slot), 2,
            sizeof(stamps), stamps, sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result != VK_SUCCESS) return;
        if (stamps[1] >= stamps[0]) {
            const double elapsed_ns =
                static_cast<double>(stamps[1] - stamps[0]) * timestamp_period_ns;
            stats.gpu_execute_us += static_cast<std::uint64_t>(elapsed_ns / 1000.0);
        }
    }

    // End the active frame batch's command buffer and submit it exactly once
    // with the current slot's fence.  No wait-for-completion happens here:
    // the caller waits only when that slot is reused (begin_frame_batch())
    // or before a readback (wait_for_pending()).
    void submit_batch() {
        const auto slot = frame_batch.next_slot;
        if (timestamp_pool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(frame_batch.command_buffers[slot],
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                timestamp_pool,
                                static_cast<std::uint32_t>(2 * slot + 1));
        }
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
        const auto submit_start = profiling::now();
        check(vkQueueSubmit(queue, 1, &submit_info, frame_batch.fences[slot]),
              "vkQueueSubmit(frame batch)");
        stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
        ++stats.submissions;
        frame_batch.in_flight[slot] = true;
        frame_batch.pass_count = 0;
        frame_batch.next_slot = (slot + 1) % FrameBatchState::kSlotCount;
    }

    static bool surface_compatible(const runtime::SurfaceDesc& a,
                                   const runtime::SurfaceDesc& b) {
        return a.width == b.width && a.height == b.height && a.format == b.format;
    }

    // True when at least one logical handle currently references `slot`.
    bool slot_in_use(std::size_t slot) const {
        for (const auto& [handle, bound_slot] : surface_bindings) {
            (void)handle;
            if (bound_slot == slot) return true;
        }
        return false;
    }

    std::size_t bound_slot(runtime::RenderSurfaceHandle handle) const {
        const auto it = surface_bindings.find(handle);
        if (it == surface_bindings.end()) {
            throw std::invalid_argument(
                "Vulkan surface handle is not bound to a physical slot");
        }
        return it->second;
    }

    // The single resolve path: handle → slot → backing image.
    Image& resolve_image(runtime::RenderSurfaceHandle handle) {
        const auto slot = bound_slot(handle);
        const auto physical_it = physical_surfaces.find(slot);
        if (physical_it == physical_surfaces.end()) {
            throw std::invalid_argument("Vulkan physical slot has no backing image");
        }
        return physical_it->second.image;
    }

    // True when some handle OTHER than `self` is bound to `slot` and the
    // slot's image already holds content.  Pre-initialized images (uploaded
    // assets) are never aliased: the liveness model covers in-frame
    // producers only, so sharing such a slot with a writer could clobber
    // pixels the frame still needs to sample.
    bool slot_has_initialized_occupant(std::size_t slot,
                                       runtime::RenderSurfaceHandle self) const {
        const auto physical_it = physical_surfaces.find(slot);
        if (physical_it == physical_surfaces.end() ||
            !physical_it->second.image.initialized) {
            return false;
        }
        for (const auto& [handle, bound_slot] : surface_bindings) {
            if (handle != self && bound_slot == slot) return true;
        }
        return false;
    }

    // Bind a handle to a slot, creating (or resizing) the slot's single
    // backing image as needed.  Aliased handles resolve to the same image
    // (one VkImage per slot, never per handle).  Two conservative guards
    // keep aliasing safe when content already exists:
    //   * a handle whose image holds content (uploaded before the batch) is
    //     PINNED to its current slot — pixels never migrate to another slot;
    //   * a handle never aliases a slot whose occupant image is initialized
    //     — such a slot is DIVERTED to a fresh private slot instead.
    Image& bind_handle_to_slot(runtime::RenderSurfaceHandle handle,
                               std::size_t slot,
                               const runtime::SurfaceDesc& desc) {
        const auto previous = surface_bindings.find(handle);
        if (previous != surface_bindings.end() && previous->second != slot) {
            const auto old_slot = previous->second;
            const auto old_it = physical_surfaces.find(old_slot);
            const bool pinned = old_it != physical_surfaces.end() &&
                                old_it->second.image.initialized;
            if (pinned) {
                slot = old_slot;  // content stays where it is
            } else {
                surface_bindings.erase(previous);
                if (!slot_in_use(old_slot) && old_it != physical_surfaces.end()) {
                    destroy_image(old_it->second.image);
                    physical_surfaces.erase(old_it);
                }
            }
        }
        if (slot_has_initialized_occupant(slot, handle)) {
            slot = next_slot++;  // never share pre-initialized content
        }
        auto& physical = physical_surfaces[slot];
        stats.physical_surfaces_peak = std::max(
            stats.physical_surfaces_peak,
            static_cast<std::uint64_t>(physical_surfaces.size()));
        if (physical.image.image == VK_NULL_HANDLE ||
            physical.image.width != desc.width ||
            physical.image.height != desc.height) {
            if (physical.image.image != VK_NULL_HANDLE) destroy_image(physical.image);
            make_image(physical.image, desc.width, desc.height);
            physical.image.initialized = false;
        }
        physical.desc = desc;
        surface_bindings[handle] = slot;
        return physical.image;
    }

    Image& ensure_surface(runtime::RenderSurfaceHandle handle,
                          const runtime::SurfaceDesc& desc) {
        if (handle == runtime::kInvalidRenderSurfaceHandle ||
            desc.format != runtime::PixelFormat::Rgba32Float ||
            desc.width == 0 || desc.height == 0) {
            throw std::invalid_argument("Vulkan surface requires a non-empty Rgba32Float description");
        }
        const auto binding = surface_bindings.find(handle);
        if (binding != surface_bindings.end()) {
            auto& physical = physical_surfaces.at(binding->second);
            if (physical.image.width != desc.width ||
                physical.image.height != desc.height) {
                destroy_image(physical.image);
                make_image(physical.image, desc.width, desc.height);
                physical.desc = desc;
            }
            ensure_descriptor_set();
            return physical.image;
        }
        // Unbound: alias a compatible, currently-unused physical slot
        // (lifetime-disjoint reuse) before allocating a fresh one.
        for (auto& [slot, physical] : physical_surfaces) {
            if (!slot_in_use(slot) && surface_compatible(physical.desc, desc)) {
                ensure_descriptor_set();
                return bind_handle_to_slot(handle, slot, desc);
            }
        }
        ensure_descriptor_set();
        return bind_handle_to_slot(handle, next_slot++, desc);
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

    void release_surface_now(runtime::RenderSurfaceHandle handle) {
        const auto binding = surface_bindings.find(handle);
        if (binding == surface_bindings.end()) return;
        wait_for_pending();
        const auto slot = binding->second;
        surface_bindings.erase(binding);
        if (!slot_in_use(slot)) {
            const auto physical_it = physical_surfaces.find(slot);
            if (physical_it != physical_surfaces.end()) {
                destroy_image(physical_it->second.image);
                physical_surfaces.erase(physical_it);
            }
        }
        unplanned_surface_handles.erase(handle);
        ++stats.surface_releases;
    }

    void flush_deferred_surface_releases() {
        if (deferred_surface_releases.empty()) return;
        auto pending = std::move(deferred_surface_releases);
        deferred_surface_releases.clear();
        for (const auto handle : pending) release_surface_now(handle);
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
        if (surface_bindings.count(handle) == 0) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
        auto& image = resolve_image(handle);
        if (!image.initialized) {
            throw std::invalid_argument("Vulkan download references an uninitialized surface");
        }
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
        const auto readback_start = profiling::now();
        void* mapped = nullptr;
        check(vkMapMemory(device, staging_memory, 0, bytes, 0, &mapped), "vkMapMemory(surface download)");
        std::memcpy(rgba.data(), mapped, static_cast<std::size_t>(bytes));
        vkUnmapMemory(device, staging_memory);
        stats.readback_us += static_cast<std::uint64_t>(profiling::elapsed_us(readback_start));
    }

    void composite(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source, BlendMode mode,
                   const std::optional<raster::BBox>& clip = std::nullopt) {
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width != src_image.width ||
            dst_image.height != src_image.height) {
            throw std::invalid_argument("Vulkan composite references incompatible surfaces");
        }
        const std::int32_t blend_mode = mode == BlendMode::Add ? 1 : 0;
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_composite(cmd, descriptors, dst_image, src_image,
                             blend_mode, 1.0f, kIdentityTint, clip);
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_composite(command_buffer, descriptor_set, dst_image, src_image,
                         blend_mode, 1.0f, kIdentityTint, clip);
        submit();
    }

    void fill_rect(runtime::RenderSurfaceHandle destination,
                   std::int32_t x0, std::int32_t y0,
                   std::int32_t x1, std::int32_t y1,
                   const Color& color) {
        auto& dst_image = resolve_image(destination);
        if (dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan fill_rect references an empty surface");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_fill_rect_descriptors(descriptors, dst_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image});
            record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1, color);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_fill_rect_descriptors(dst_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image});
        record_fill_rect(command_buffer, descriptor_set, dst_image, x0, y0, x1, y1, color);
        dst_image.initialized = true;
        submit();
    }

    void transform(runtime::RenderSurfaceHandle destination,
                   runtime::RenderSurfaceHandle source,
                   int offset_x, int offset_y, float opacity) {
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan transform references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_transform(cmd, descriptors,
                             dst_image, src_image, offset_x, offset_y, opacity);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
        record_transform(command_buffer, descriptor_set, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        submit();
    }

    void transform_affine(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle source,
                          const runtime::SurfaceAffineTransform& transform) {
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan affine transform references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_transform_affine(cmd, descriptors,
                                    dst_image, src_image, transform);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
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
        auto& dst_image = resolve_image(destination);
        auto& src_image = resolve_image(source);
        if (!src_image.initialized ||
            dst_image.width != src_image.width ||
            dst_image.height != src_image.height) {
            throw std::invalid_argument("Vulkan blur references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst_image, src_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &src_image});
            record_blur(cmd, descriptors, dst_image, src_image, radius, horizontal);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst_image, src_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &src_image});
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
            return resolve_image(handle);
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
            // Glow is a single plan pass whose internal blur→blur→composite
            // chain is a write→read dependency chain between dispatches; the
            // full barriers below are glow's own structure (the only
            // multi-dispatch operation), so the pass-level mapper call is
            // skipped for it.
            const auto cmd = active_command_buffer();
            emit_conservative_pass_sync(cmd, {&horizontal, &src});
            record_blur(cmd, horizontal_descriptor, horizontal, src, radius, true);
            emit_conservative_pass_sync(cmd, {&vertical, &horizontal});
            record_blur(cmd, vertical_descriptor, vertical, horizontal, radius, false);
            emit_conservative_pass_sync(cmd, {&dst, &vertical});
            record_composite(cmd, composite_descriptor, dst, vertical,
                             1, intensity, tint_rgba);
            horizontal.initialized = true;
            vertical.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }

        begin_command_buffer();
        const auto horizontal_descriptor = ensure_glow_descriptor_set(0);
        write_descriptors(horizontal_descriptor, horizontal, src);
        const auto vertical_descriptor = ensure_glow_descriptor_set(1);
        write_descriptors(vertical_descriptor, vertical, horizontal);
        const auto composite_descriptor = ensure_glow_descriptor_set(2);
        write_descriptors(composite_descriptor, dst, vertical);
        emit_conservative_pass_sync(command_buffer, {&horizontal, &src});
        record_blur(command_buffer, horizontal_descriptor,
                    horizontal, src, radius, true);
        emit_conservative_pass_sync(command_buffer, {&vertical, &horizontal});
        record_blur(command_buffer, vertical_descriptor,
                    vertical, horizontal, radius, false);
        emit_conservative_pass_sync(command_buffer, {&dst, &vertical});
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
        auto& dst = resolve_image(destination);
        auto& src = resolve_image(source);
        if (!src.initialized ||
            dst.width != src.width ||
            dst.height != src.height) {
            throw std::invalid_argument("Vulkan color adjust references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_descriptors(descriptors, dst, src);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst, &src});
            record_color_adjust(cmd, descriptors,
                                dst, src, brightness, contrast, tint, tint_amount);
            dst.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        bind_descriptors(dst, src);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst, &src});
        record_color_adjust(command_buffer, descriptor_set,
                            dst, src, brightness, contrast, tint, tint_amount);
        dst.initialized = true;
        submit();
    }

    void matte(runtime::RenderSurfaceHandle destination,
               runtime::RenderSurfaceHandle target,
               runtime::RenderSurfaceHandle matte_surface,
               bool luma, bool inverted) {
        auto& dst = resolve_image(destination);
        auto& target_image = resolve_image(target);
        auto& matte_image = resolve_image(matte_surface);
        if (!target_image.initialized || !matte_image.initialized ||
            dst.width != target_image.width ||
            dst.height != target_image.height ||
            dst.width != matte_image.width ||
            dst.height != matte_image.height) {
            throw std::invalid_argument("Vulkan matte references incompatible surfaces");
        }
        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_matte_descriptors(descriptors, dst, target_image, matte_image);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst, &target_image, &matte_image});
            record_matte(cmd, descriptors,
                         dst, target_image, matte_image, luma, inverted);
            dst.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        ensure_descriptor_set();
        write_matte_descriptors(descriptor_set, dst, target_image, matte_image);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst, &target_image, &matte_image});
        record_matte(command_buffer, descriptor_set,
                     dst, target_image, matte_image, luma, inverted);
        dst.initialized = true;
        submit();
    }

    // GPU text-run primitive: composite a batch of glyph quads sampled from a
    // packed atlas texture into the destination in ONE kernel dispatch.
    void text_run_surface(runtime::RenderSurfaceHandle destination,
                          runtime::RenderSurfaceHandle atlas,
                          std::span<const runtime::GlyphInstance> glyphs) {
        if (glyphs.empty()) {
            throw std::invalid_argument("Vulkan text run requires at least one glyph");
        }
        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(glyphs.size() * sizeof(runtime::GlyphInstance));
        if (bytes > 65536) {
            throw std::invalid_argument(
                "Vulkan text run exceeds the 65536-byte vkCmdUpdateBuffer limit");
        }
        auto& dst_image = resolve_image(destination);
        auto& atlas_image = resolve_image(atlas);
        if (!atlas_image.initialized || !dst_image.initialized ||
            dst_image.width == 0 || dst_image.height == 0) {
            throw std::invalid_argument("Vulkan text run references incompatible surfaces");
        }
        ensure_glyph_instance_buffer(bytes);
        const std::int32_t glyph_count = static_cast<std::int32_t>(glyphs.size());

        if (frame_batch.active) {
            const auto descriptors = allocate_pass_descriptor_set();
            write_text_run_descriptors(descriptors, dst_image, atlas_image, glyph_instance_buffer);
            const auto cmd = active_command_buffer();
            emit_pass_sync(cmd, {&dst_image, &atlas_image});
            vkCmdUpdateBuffer(cmd, glyph_instance_buffer, 0, bytes, glyphs.data());
            record_text_run(cmd, descriptors, dst_image, glyph_count);
            dst_image.initialized = true;
            ++frame_batch.pass_count;
            ++stats.passes_executed;
            return;
        }
        ensure_descriptor_set();
        write_text_run_descriptors(descriptor_set, dst_image, atlas_image, glyph_instance_buffer);
        begin_command_buffer();
        emit_conservative_pass_sync(command_buffer, {&dst_image, &atlas_image});
        vkCmdUpdateBuffer(command_buffer, glyph_instance_buffer, 0, bytes, glyphs.data());
        record_text_run(command_buffer, descriptor_set, dst_image, glyph_count);
        dst_image.initialized = true;
        ++stats.passes_executed;
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

    // Grow (or create) the device-local glyph-instance storage buffer.  The
    // buffer is updated in-band with vkCmdUpdateBuffer and read by the
    // text-run compute kernel, so it needs both transfer-dst and storage
    // usage; device-local memory keeps the GPU read path on-card.
    void ensure_glyph_instance_buffer(VkDeviceSize bytes) {
        if (glyph_instance_capacity >= bytes) return;
        if (glyph_instance_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, glyph_instance_buffer, nullptr);
        }
        if (glyph_instance_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, glyph_instance_memory, nullptr);
        }
        const VkBufferCreateInfo info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
        check(vkCreateBuffer(device, &info, nullptr, &glyph_instance_buffer),
              "vkCreateBuffer(glyph instances)");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, glyph_instance_buffer, &requirements);
        const VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, requirements.size,
            memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        check(vkAllocateMemory(device, &allocation, nullptr, &glyph_instance_memory),
              "vkAllocateMemory(glyph instances)");
        check(vkBindBufferMemory(device, glyph_instance_buffer, glyph_instance_memory, 0),
              "vkBindBufferMemory(glyph instances)");
        glyph_instance_capacity = bytes;
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
            const auto wait_start = profiling::now();
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            stats.gpu_wait_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
            check(vkResetFences(device, 1, &fence), "vkResetFences");
            pending_timeline_value = 0;
        }
        // Synchronize any frame-batch slots still in flight.  This is a sync
        // point (release/readback path), distinct from begin_frame_batch()
        // which waits ONLY on the fence of the slot being reused.
        for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
            if (!frame_batch.in_flight[i]) continue;
            const auto wait_start = profiling::now();
            check(vkWaitForFences(device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(frame batch slot)");
            stats.gpu_wait_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(wait_start));
            check(vkResetFences(device, 1, &frame_batch.fences[i]),
                  "vkResetFences(frame batch slot)");
            frame_batch.in_flight[i] = false;
            read_gpu_timestamps(i);
        }
        // Synchronize any in-flight upload slots too.  release_surface() and
        // every standalone path call wait_for_pending() before reusing or
        // destroying a surface, and an asynchronous upload may still be
        // copying into it; draining these slots closes the use-after-free
        // where an in-flight VkImage was destroyed before its copy completed.
        for (auto& slot : upload_slots) {
            wait_upload_slot(slot);
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
        const auto submit_start = profiling::now();
        check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
        stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(profiling::elapsed_us(submit_start));
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
    std::swap(m_draw_node_fallback, other.m_draw_node_fallback);
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
    return graph::RenderCapabilities{
        .text_run = m_draw_node_fallback != nullptr &&
                    m_draw_node_fallback->capabilities().text_run};
}

std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
VulkanBackend::processor_snapshot() const noexcept {
    return m_draw_node_fallback ? m_draw_node_fallback->processor_snapshot() : nullptr;
}

bool VulkanBackend::requires_processor_snapshot() const noexcept {
    return m_draw_node_fallback && m_draw_node_fallback->requires_processor_snapshot();
}

VulkanBackendStats VulkanBackend::stats() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return m_impl ? m_impl->stats : VulkanBackendStats{};
#else
    return {};
#endif
}

void VulkanBackend::export_gpu_telemetry_counters(
    std::vector<std::pair<std::string, std::uint64_t>>& out) const {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return;
    out.emplace_back("gpu_submissions", m_impl->stats.submissions);
    out.emplace_back("passes_executed", m_impl->stats.passes_executed);
    out.emplace_back("gpu_upload_bytes", m_impl->stats.upload_bytes);
    out.emplace_back("gpu_readback_bytes", m_impl->stats.readback_bytes);
    out.emplace_back("physical_surfaces_peak", m_impl->stats.physical_surfaces_peak);
    out.emplace_back("gpu_submit_cpu_us", m_impl->stats.gpu_submit_cpu_us);
    out.emplace_back("gpu_wait_cpu_us", m_impl->stats.gpu_wait_cpu_us);
    out.emplace_back("readback_us", m_impl->stats.readback_us);
    out.emplace_back("cpu_gpu_sync_us", m_impl->stats.gpu_wait_cpu_us + m_impl->stats.readback_us);
    out.emplace_back("gpu_execute_us", m_impl->stats.gpu_execute_us);
    out.emplace_back("gpu_nodes", m_impl->stats.passes_executed);
    out.emplace_back("software_fallback_nodes", m_impl->stats.software_fallback_nodes);
    out.emplace_back("software_fallback_us", m_impl->stats.software_fallback_us);
    out.emplace_back("fallback_draw_node", m_impl->stats.fallback_draw_node);
    out.emplace_back("fallback_draw_image", m_impl->stats.fallback_draw_image);
    out.emplace_back("fallback_draw_other", m_impl->stats.fallback_draw_other);
    out.emplace_back("fallback_text_run", m_impl->stats.fallback_text_run);
    out.emplace_back("fallback_composite", m_impl->stats.fallback_composite);
    out.emplace_back("fallback_composite_dimensions", m_impl->stats.fallback_composite_dimensions);
    out.emplace_back("fallback_composite_mode", m_impl->stats.fallback_composite_mode);
    out.emplace_back("fallback_effect", m_impl->stats.fallback_effect);
    out.emplace_back("fallback_blur", m_impl->stats.fallback_blur);
    out.emplace_back("fallback_dof", m_impl->stats.fallback_dof);
#else
    (void)out;
#endif
}

std::size_t VulkanBackend::physical_surface_count() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return m_impl ? m_impl->physical_surfaces.size() : 0;
#else
    return 0;
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
    // Second and later frames of an active command batch keep recording into
    // the SAME command buffer (opened by the first frame).  Flush a
    // cross-overlay boundary barrier and reset only per-frame bookkeeping;
    // the descriptor allocator and command buffer stay intact so every
    // overlay's recorded descriptor sets remain valid until the single
    // submission at end_command_batch().
    if (m_impl->command_batch_active && m_impl->command_batch_started) {
        m_impl->emit_command_batch_boundary();
        batch.pass_count = 0;
        batch.sync_plan = nullptr;
        m_impl->m_slot_last_access.clear();
        batch.active = true;
        return;
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
        m_impl->read_gpu_timestamps(slot);
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
    if (m_impl->timestamp_pool != VK_NULL_HANDLE) {
        const auto query_base = static_cast<std::uint32_t>(2 * slot);
        vkCmdResetQueryPool(batch.command_buffers[slot], m_impl->timestamp_pool,
                            query_base, 2);
        vkCmdWriteTimestamp(batch.command_buffers[slot],
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_impl->timestamp_pool, query_base);
    }
    batch.active = true;
    batch.pass_count = 0;
    batch.descriptor_sets.clear();
    batch.sync_plan = nullptr;
    m_impl->m_slot_last_access.clear();
    // The first frame of a command batch opened the buffer above; mark the
    // batch as started so subsequent frames take the soft-reset path.
    if (m_impl->command_batch_active) {
        m_impl->command_batch_started = true;
    }
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::begin_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (m_impl->command_batch_active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a command batch is already active");
    }
    if (m_impl->frame_batch.active) {
        throw std::logic_error(
            "VulkanBackend::begin_command_batch: a frame batch is already active");
    }
    // The first overlay's begin_plan_batch → begin_frame_batch opens the
    // single command buffer for the whole batch.
    m_impl->command_batch_active = true;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::begin_plan_batch(const runtime::CommandPlan& plan) {
#ifdef CHRONON3D_ENABLE_VULKAN
    begin_frame_batch();
    m_impl->frame_batch.sync_plan = &plan.barriers;
    // Bind every planned allocation to its physical slot, backing each slot
    // with exactly one VkImage.  Lifetime-disjoint handles that share a
    // planned slot therefore alias the same device image (the registry-side
    // bind_plan_slots() propagates the same mapping for identity records).
    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.surface == runtime::kInvalidRenderSurfaceHandle) continue;
        if (allocation.physical_slot == std::numeric_limits<std::size_t>::max()) continue;
        if (allocation.physical_slot >= plan.resources.slots.size()) continue;
        const auto& planned = plan.resources.slots[allocation.physical_slot];
        const runtime::SurfaceDesc desc{
            planned.width, planned.height, planned.format,
            planned.usage, runtime::LifetimeClass::FrameTransient,
            static_cast<std::size_t>(planned.width) * planned.height *
                sizeof(float) * 4};
        m_impl->bind_handle_to_slot(allocation.surface, allocation.physical_slot, desc);
    }
#else
    (void)plan;
    unsupported("begin_plan_batch");
#endif
}

void VulkanBackend::end_frame_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    auto& batch = m_impl->frame_batch;
    if (!batch.active) return;
    if (m_impl->command_batch_active) {
        // Defer the submission: end_command_batch() performs exactly one
        // vkQueueSubmit for every overlay recorded into this command batch.
        batch.active = false;
        return;
    }
    m_impl->submit_batch();
    m_impl->flush_deferred_surface_releases();
    batch.active = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

void VulkanBackend::end_command_batch() {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl->command_batch_active) return;
    if (m_impl->command_batch_started) {
        // The final frame's end_frame_batch() deferred its submission, so the
        // single command buffer is still open and holds all N overlays.  One
        // vkQueueSubmit flushes the whole batch.
        m_impl->submit_batch();
    }
    m_impl->flush_deferred_surface_releases();
    m_impl->frame_batch.active = false;
    m_impl->command_batch_active = false;
    m_impl->command_batch_started = false;
#else
    (void)0;  // no-op when the Vulkan backend is not compiled
#endif
}

graph::RenderOpResult VulkanBackend::create_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        (void)m_impl->ensure_surface(handle, desc);
        if (m_impl->frame_batch.active || m_impl->command_batch_active) {
            m_impl->unplanned_surface_handles.insert(handle);
        }
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
        if (m_impl->frame_batch.active || m_impl->command_batch_active) {
            if (std::find(m_impl->deferred_surface_releases.begin(),
                          m_impl->deferred_surface_releases.end(), handle) ==
                m_impl->deferred_surface_releases.end()) {
                m_impl->deferred_surface_releases.push_back(handle);
            }
            return graph::RenderOpResult(graph::RenderOpOutcome{});
        }
        m_impl->release_surface_now(handle);
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
    BlendMode mode, CompositeOperator op,
    const std::optional<raster::BBox>& clip) {
    if ((mode != BlendMode::Normal && mode != BlendMode::Add) ||
        op != CompositeOperator::SourceOver) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "VulkanBackend::composite_surfaces: only Normal/Add SourceOver is implemented"});
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->composite(destination, source, mode, clip);
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

graph::RenderOpResult VulkanBackend::fill_rect_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Color& color) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->fill_rect(destination, x0, y0, x1, y1, color);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_rect_surface: Vulkan support is disabled"});
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

graph::RenderOpResult VulkanBackend::draw_text_run_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        m_impl->text_run_surface(destination, atlas, glyphs);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::RenderBackendError{
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()});
    }
#else
    (void)destination; (void)atlas; (void)glyphs;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface: Vulkan support is disabled"});
#endif
}

void VulkanBackend::unsupported(const char* operation) {
    throw std::runtime_error(std::string{"VulkanBackend::"} + operation +
                             ": RenderSurface execution is not wired yet");
}

void VulkanBackend::record_software_fallback(
    const char* reason,
    std::chrono::steady_clock::time_point started) noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return;
    ++m_impl->stats.software_fallback_nodes;
    if (std::strcmp(reason, "draw_node") == 0) ++m_impl->stats.fallback_draw_node;
    else if (std::strcmp(reason, "text_run") == 0) ++m_impl->stats.fallback_text_run;
    else if (std::strcmp(reason, "composite") == 0) ++m_impl->stats.fallback_composite;
    else if (std::strcmp(reason, "effect") == 0) ++m_impl->stats.fallback_effect;
    else if (std::strcmp(reason, "blur") == 0) ++m_impl->stats.fallback_blur;
    else if (std::strcmp(reason, "dof") == 0) ++m_impl->stats.fallback_dof;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    m_impl->stats.software_fallback_us += static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, elapsed));
#else
    (void)reason;
    (void)started;
#endif
}

void VulkanBackend::apply_per_pixel_dof(
    Framebuffer& framebuffer, std::span<const float> depth,
    const DepthOfFieldSettings& dof, const LensModel& lens,
    const std::optional<raster::BBox>& clip) {
    // DOF still uses the legacy CPU framebuffer contract. Keep the selected
    // runtime Vulkan-backed while delegating this unsupported operation to
    // the canonical software backend until a surface-native DOF pass exists.
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->apply_per_pixel_dof(
            framebuffer, depth, dof, lens, clip);
        record_software_fallback("dof", started);
        return;
    }
    unsupported("apply_per_pixel_dof");
}
void VulkanBackend::set_draw_node_fallback(
    std::unique_ptr<graph::RenderBackend> fallback) {
    m_draw_node_fallback = std::move(fallback);
}

void VulkanBackend::draw_node(Framebuffer& framebuffer, const RenderNode& node,
                              const RenderState& state, const Camera& camera,
                              int width, int height) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->draw_node(framebuffer, node, state, camera, width, height);
        if (m_impl) {
            if (node.shape.type() == ShapeType::Image) {
                ++m_impl->stats.fallback_draw_image;
            } else {
                ++m_impl->stats.fallback_draw_other;
            }
        }
        record_software_fallback("draw_node", started);
        return;
    }
    unsupported("draw_node: no legacy-node fallback attached");
}
void VulkanBackend::apply_effect_stack(
    Framebuffer& framebuffer, const EffectStack& stack,
    const effects::EffectExecutionContext& context) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->apply_effect_stack(framebuffer, stack, context);
        record_software_fallback("effect", started);
        return;
    }
    unsupported("apply_effect_stack");
}
void VulkanBackend::composite_layer(Framebuffer& destination, const Framebuffer& source,
                                    BlendMode mode, const std::optional<raster::BBox>& clip,
                                    CompositeOperator op) {
    if (mode != BlendMode::Normal || op != CompositeOperator::SourceOver) {
        if (m_draw_node_fallback) {
            const auto started = std::chrono::steady_clock::now();
            m_draw_node_fallback->composite_layer(destination, source, mode, clip, op);
            if (m_impl) ++m_impl->stats.fallback_composite_mode;
            record_software_fallback("composite", started);
            return;
        }
        throw std::runtime_error("VulkanBackend::composite_layer: only Normal/SourceOver is implemented");
    }
    if (destination.width() != source.width() || destination.height() != source.height()) {
        if (m_draw_node_fallback) {
            const auto started = std::chrono::steady_clock::now();
            m_draw_node_fallback->composite_layer(destination, source, mode, clip, op);
            if (m_impl) ++m_impl->stats.fallback_composite_dimensions;
            record_software_fallback("composite", started);
            return;
        }
        throw std::runtime_error("VulkanBackend::composite_layer: surface dimensions differ");
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    m_impl->composite(destination.surface_handle(), source.surface_handle(), mode, clip);
#else
    unsupported("composite_layer");
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run(
    Framebuffer& framebuffer, const TextRunShape& shape,
    const glm::mat4& model_matrix, float opacity) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        auto result = m_draw_node_fallback->draw_text_run(
            framebuffer, shape, model_matrix, opacity);
        record_software_fallback("text_run", started);
        return result;
    }
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run: no legacy-node fallback attached"});
}
void VulkanBackend::apply_blur(
    Framebuffer& framebuffer, float radius,
    const std::optional<raster::BBox>& clip) {
    if (m_draw_node_fallback) {
        const auto started = std::chrono::steady_clock::now();
        m_draw_node_fallback->apply_blur(framebuffer, radius, clip);
        record_software_fallback("blur", started);
        return;
    }
    unsupported("apply_blur");
}

std::unique_ptr<graph::RenderBackend> make_vulkan_backend() {
    return std::make_unique<VulkanBackend>();
}

} // namespace chronon3d::backends::vulkan
