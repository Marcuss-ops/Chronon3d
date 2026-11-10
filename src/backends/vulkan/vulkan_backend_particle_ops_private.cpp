// vulkan_backend_particle_ops_private.cpp — stateless GPU particle emitter.
//
// The emitter compute pass writes the exact GpuLayerInstance records consumed
// by layer_batch.comp.  This keeps the CPU sampler as the oracle: the CPU path
// samples particles and feeds the same billboard rasterizer, while this path
// derives the records on-device from (seed, index, absolute time).

#include "vulkan_backend_impl.hpp"
#include "vulkan_particle_emitter.hpp"

#include <chronon3d/scene/model/shape/particles.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

namespace {

struct alignas(16) GpuLayerInstance {
    float dst_rect[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float src_rect[4]{0.0f, 0.0f, 1.0f, 1.0f};
    float affine_row0[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float affine_row1[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float limits[4]{0.0f, 0.0f, 1.0f, 1.0f};
    std::uint32_t has_affine{0};
    std::uint32_t blend_mode{0};
    std::uint32_t resource_idx{0};
    std::uint32_t pad{0};
    float particle_color[4]{1.0f, 1.0f, 1.0f, 1.0f};
};
static_assert(sizeof(GpuLayerInstance) == 112,
              "GpuLayerInstance must match layer_batch.comp std430 ABI");

void emit_particle_buffer_barrier(
    VkCommandBuffer command, VkBuffer buffer,
    VkPipelineStageFlags source_stage, VkAccessFlags source_access,
    VkPipelineStageFlags destination_stage, VkAccessFlags destination_access) {
    const VkBufferMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr,
        source_access, destination_access,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        buffer, 0, VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, source_stage, destination_stage, 0,
                         0, nullptr, 1, &barrier, 0, nullptr);
}

std::uint32_t layer_blend_mode(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal: return 0u;
    case BlendMode::Add: return 1u;
    case BlendMode::Multiply: return 3u;
    case BlendMode::Screen: return 4u;
    case BlendMode::Overlay: return 5u;
    default:
        throw std::invalid_argument(
            "GPU particle emitter does not support this blend mode");
    }
}

GpuParticleEmitterUniform make_particle_uniform(
    const ParticleEmitterParams& emitter, float seconds) {
    GpuParticleEmitterUniform uniform{};
    uniform.header[0] = emitter.count;
    uniform.header[1] = emitter.seed;
    uniform.header[2] = static_cast<std::uint32_t>(emitter.shape);
    uniform.header[3] = (emitter.bokeh ? 1u : 0u) |
                        (emitter.environment ? 2u : 0u);

    uniform.emitter[0] = emitter.emitter_size.x;
    uniform.emitter[1] = emitter.emitter_size.y;
    uniform.emitter[2] = emitter.lifetime_min;
    uniform.emitter[3] = emitter.lifetime_max;

    uniform.velocity_size[0] = emitter.velocity_min.x;
    uniform.velocity_size[1] = emitter.velocity_min.y;
    uniform.velocity_size[2] = emitter.size_min;
    uniform.velocity_size[3] = emitter.size_max;

    uniform.velocity_gravity[0] = emitter.velocity_max.x;
    uniform.velocity_gravity[1] = emitter.velocity_max.y;
    uniform.velocity_gravity[2] = emitter.gravity;
    uniform.velocity_gravity[3] = emitter.turbulence;

    uniform.flow_twinkle[0] = emitter.flow.strength;
    uniform.flow_twinkle[1] = emitter.flow.scale;
    uniform.flow_twinkle[2] = emitter.flow.speed;
    uniform.flow_twinkle[3] = emitter.twinkle.amount;

    uniform.motion[0] = emitter.twinkle.speed;
    uniform.motion[1] = emitter.depth_min;
    uniform.motion[2] = emitter.depth_max;
    uniform.motion[3] = seconds;

    uniform.sprite[0] = emitter.sprite_scale_min;
    uniform.sprite[1] = emitter.sprite_scale_max;
    uniform.sprite[2] = emitter.rotation_min;
    uniform.sprite[3] = emitter.rotation_max;
    uniform.angular[0] = emitter.angular_velocity_min;
    uniform.angular[1] = emitter.angular_velocity_max;
    uniform.angular[2] = static_cast<float>(layer_blend_mode(emitter.blend_mode));

    if (emitter.color_ramp.color_stops.empty()) {
        for (std::size_t i = 0; i < kGpuParticleLutEntries; ++i) {
            uniform.color_lut[i][0] = 1.0f;
            uniform.color_lut[i][1] = 1.0f;
            uniform.color_lut[i][2] = 1.0f;
            uniform.color_lut[i][3] = 1.0f;
        }
    } else {
        for (std::size_t i = 0; i < kGpuParticleLutEntries; ++i) {
            const float t = static_cast<float>(i) /
                static_cast<float>(kGpuParticleLutEntries - 1U);
            const Color color = graphics::sample_gradient(emitter.color_ramp, t);
            uniform.color_lut[i][0] = color.r;
            uniform.color_lut[i][1] = color.g;
            uniform.color_lut[i][2] = color.b;
            uniform.color_lut[i][3] = color.a;
        }
    }
    return uniform;
}

void write_particle_emitter_descriptors(
    VkDevice device, VkDescriptorSet set, VkBuffer instance_buffer,
    VkDeviceSize instance_offset, VkDeviceSize instance_bytes,
    VkDeviceSize uniform_bytes) {
    const VkDescriptorBufferInfo instance_info{
        instance_buffer, instance_offset, instance_bytes};
    const VkDescriptorBufferInfo uniform_info{
        instance_buffer, 0, uniform_bytes};
    const VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &instance_info, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniform_info, nullptr}};
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

} // namespace

void VulkanBackend::Impl::execute_gpu_particle_batch(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle transparent_source,
    const ParticleEmitterParams& emitter, double seconds) {
    if (emitter.count == 0) return;
    if (emitter.count > kGpuParticleMaxCount) {
        throw std::invalid_argument(
            "GPU particle emitter exceeds the 4,096-particle budget");
    }
    if (emitter.shape == EmitterShape::Path) {
        throw std::invalid_argument(
            "GPU particle emitter does not yet support path emitters");
    }
    if (!emitter.sprite_asset.empty()) {
        throw std::invalid_argument(
            "GPU particle emitter does not yet support sprite assets");
    }
    if (!std::isfinite(seconds) || seconds < 0.0) {
        throw std::invalid_argument(
            "GPU particle emitter time must be finite and non-negative");
    }
    if (const auto error = validate_particle_emitter(emitter); !error.empty()) {
        throw std::invalid_argument(error);
    }

    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(transparent_source);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "GPU particle emitter references an invalid destination surface");
    }
    if (!src_image.initialized || src_image.width == 0 || src_image.height == 0) {
        throw std::invalid_argument(
            "GPU particle emitter references an invalid transparent source");
    }
    if (!frame_batch.active) {
        throw std::logic_error(
            "GPU particle emitter requires an active frame batch");
    }

    const std::size_t count = emitter.count;
    const VkDeviceSize instance_bytes =
        static_cast<VkDeviceSize>(count * sizeof(GpuLayerInstance));
    const VkDeviceSize total_bytes =
        static_cast<VkDeviceSize>(kGpuParticleInstanceOffset) + instance_bytes;
    const std::size_t slot = frame_batch.next_slot;
    const std::size_t instance_slot =
        slot * kGlyphInstancePassesPerSlot +
        (frame_batch.pass_count % kGlyphInstancePassesPerSlot);
    ensure_layer_instance_buffer(total_bytes, instance_slot);
    const VkBuffer instance_buffer =
        layer_instance_buffers[instance_slot].buffer;

    const auto command = active_command_buffer();
    const GpuParticleEmitterUniform uniform =
        make_particle_uniform(emitter, static_cast<float>(seconds));
    vkCmdUpdateBuffer(command, instance_buffer, 0, sizeof(uniform), &uniform);
    emit_particle_buffer_barrier(
        command, instance_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    VkDescriptorSet emitter_descriptors =
        descriptor_arena.particle_emitter[slot].allocate();
    write_particle_emitter_descriptors(
        device, emitter_descriptors, instance_buffer,
        static_cast<VkDeviceSize>(kGpuParticleInstanceOffset), instance_bytes,
        sizeof(uniform));

    vkCmdBindPipeline(
        command, VK_PIPELINE_BIND_POINT_COMPUTE,
        reinterpret_cast<VkPipeline>(
            kernels.registry.resolve(GpuKernelId::ParticleEmitter)));
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_COMPUTE,
        kernels.particle_emitter_layout, 0, 1, &emitter_descriptors, 0, nullptr);
    vkCmdDispatch(command, (emitter.count + 63U) / 64U, 1, 1);
    ++stats.vk_cmd_dispatch_count;

    emit_particle_buffer_barrier(
        command, instance_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    emit_pass_sync(command, {&dst_image, &src_image},
                   runtime::GpuPassKind::LayerBatch);

    ++stats.layer_batch_calls;
    stats.layer_instances_processed += count;

    // LayerBatch loops over at most 512 instances per dispatch; keep the
    // generated records in one buffer and bind a windowed descriptor per chunk.
    constexpr std::size_t kDrawChunk = 512;
    for (std::size_t first = 0; first < count; first += kDrawChunk) {
        const std::size_t chunk = std::min(kDrawChunk, count - first);
        const VkDeviceSize chunk_offset =
            static_cast<VkDeviceSize>(kGpuParticleInstanceOffset) +
            static_cast<VkDeviceSize>(first * sizeof(GpuLayerInstance));
        const VkDeviceSize chunk_bytes =
            static_cast<VkDeviceSize>(chunk * sizeof(GpuLayerInstance));
        const auto descriptors = allocate_pass_descriptor_set();
        write_layer_batch_descriptors(
            descriptors, dst_image, src_image, instance_buffer,
            chunk_offset, chunk_bytes);
        record_layer_batch(
            command, descriptors, dst_image,
            static_cast<std::int32_t>(chunk), instance_buffer, false,
            0, 0, static_cast<std::int32_t>(dst_image.width),
            static_cast<std::int32_t>(dst_image.height));
    }

    dst_image.initialized = true;
    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

} // namespace chronon3d::backends::vulkan
