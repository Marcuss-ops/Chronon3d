// vulkan_backend_text_ops_private.cpp — VulkanBackend::Impl text operations
// (text_run_surface, tiled text batch upload and dispatch).

#include "vulkan_backend_impl.hpp"
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::text_run_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
    if (glyphs.empty()) {
        throw std::invalid_argument("Vulkan text run requires at least one glyph");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan text run requires an active frame batch");
    }
    if (frame_batch.pass_count >= kGlyphInstancePassesPerSlot) {
        throw std::logic_error("Vulkan text upload pass capacity exhausted");
    }
    constexpr VkDeviceSize kMaxCmdUpdateBytes = 65536;
    const VkDeviceSize instance_bytes =
        static_cast<VkDeviceSize>(glyphs.size() * sizeof(runtime::GlyphInstance));
    if (instance_bytes > kMaxCmdUpdateBytes) {
        throw std::logic_error("Vulkan text instances exceed vkCmdUpdateBuffer limit");
    }
    const std::size_t pass_slot =
        frame_batch.next_slot * kGlyphInstancePassesPerSlot +
        (frame_batch.pass_count % kGlyphInstancePassesPerSlot);
    ensure_glyph_instance_buffer(instance_bytes, pass_slot);
    auto& buffer_state = glyph_instance_buffers[pass_slot];
    auto& dst_image = resolve_image(destination);
    auto& atlas_image = resolve_image(atlas);
    const auto descriptors = allocate_pass_descriptor_set();
    write_text_run_descriptors(descriptors, dst_image,
                               atlas_image, buffer_state.buffer);

    std::int32_t x0 = static_cast<std::int32_t>(dst_image.width);
    std::int32_t y0 = static_cast<std::int32_t>(dst_image.height);
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    for (const auto& g : glyphs) {
        const auto sx = (g.scale_x != 0.0f) ? g.scale_x : 1.0f;
        const auto sy = (g.scale_y != 0.0f) ? g.scale_y : 1.0f;
        x0 = std::min(x0, g.dst_x);
        y0 = std::min(y0, g.dst_y);
        x1 = std::max(x1, g.dst_x +
                      static_cast<std::int32_t>(std::ceil(g.width * sx)));
        y1 = std::max(y1, g.dst_y +
                      static_cast<std::int32_t>(std::ceil(g.height * sy)));
    }
    const auto cmd = active_command_buffer();
    emit_pass_sync(cmd, {&dst_image, &atlas_image});
    vkCmdUpdateBuffer(cmd, buffer_state.buffer, 0, instance_bytes, glyphs.data());
    record_text_run(cmd, descriptors, dst_image,
                    static_cast<std::int32_t>(glyphs.size()),
                    buffer_state.buffer, true, current_frame,
                    highlight_color, highlight_enabled, x0, y0, x1, y1);
    ++frame_batch.pass_count;
    dst_image.initialized = true;
    ++stats.passes_executed;
}

namespace {

// The tiled shaders consume the canonical 128-byte runtime record. Keeping
// a second, shorter mirror here used to make dynamic animator fields invisible
// to Vulkan and shift every following array element from the shader's point of
// view. The public record is now the sole host/shader layout authority for
// selector, tracking, rotation, fill-color and effect bindings.
using GpuTextRunDynamic = runtime::TextRunDynamic;
static_assert(sizeof(GpuTextRunDynamic) == 128,
              "GpuTextRunDynamic must match runtime::TextRunDynamic/std430");

} // namespace

void VulkanBackend::Impl::draw_text_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::GlyphStatic> glyphs,
    std::span<const runtime::TextRunDynamic> runs,
    std::span<const runtime::RenderSurfaceHandle> atlas_pages) {
    if (glyphs.empty() || runs.empty() || atlas_pages.empty()) return;
    ++stats.text_batch_calls;
    stats.glyphs_processed += glyphs.size();

    auto& dst_image = resolve_image(destination);
    // A freshly bound terminal NVENC surface is intentionally uninitialized,
    // just like the destination accepted by execute_layer_batch().  Text
    // rasterization blends over destination pixels, so establish transparent
    // contents before the first text pass instead of rejecting the valid
    // first-write lifetime state (or reading undefined pixels).
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan draw_text_batch references an invalid destination surface");
    }
    if (!dst_image.initialized) {
        const auto clear_descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(clear_descriptors, dst_image);
        const auto clear_cmd = active_command_buffer();
        emit_unplanned_compute_sync(clear_cmd, {&dst_image});
        record_fill_rect(clear_cmd, clear_descriptors, dst_image, 0, 0,
                         static_cast<std::int32_t>(dst_image.width),
                         static_cast<std::int32_t>(dst_image.height),
                         Color::transparent());
        dst_image.initialized = true;
        emit_unplanned_compute_sync(clear_cmd, {&dst_image});
        ++stats.passes_executed;
    }
    auto& atlas_image = resolve_image(atlas_pages[0]);
    if (!atlas_image.initialized || atlas_image.width == 0 || atlas_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan draw_text_batch references an invalid atlas surface");
    }

    static std::atomic<bool> format_diagnostic_emitted{false};
    bool expected = false;
    if (format_diagnostic_emitted.compare_exchange_strong(expected, true)) {
        spdlog::info(
            "chronon3d::vulkan text batch formats: dst_image.format={} "
            "atlas_image.format={} atlas_encoding={} "
            "(shader contract: dst rgba32f, atlas rgba8)",
            static_cast<int>(dst_image.format),
            static_cast<int>(atlas_image.format),
            static_cast<int>(atlas_image.text_atlas_encoding));
    }

    std::array<VulkanGpuGlyphStatic, 1024> local_glyphs;
    VulkanGpuGlyphStatic* gpu_glyphs = local_glyphs.data();
    if (glyphs.size() > local_glyphs.size()) {
        if (text_glyph_scratch.size() < glyphs.size()) {
            text_glyph_scratch.resize(glyphs.size());
        }
        gpu_glyphs = text_glyph_scratch.data();
    }

    std::array<GpuTextRunDynamic, 128> local_runs;
    GpuTextRunDynamic* gpu_runs = local_runs.data();
    if (runs.size() > local_runs.size()) {
        if (text_run_scratch.size() < runs.size()) {
            text_run_scratch.resize(runs.size());
        }
        gpu_runs = text_run_scratch.data();
    }

    for (std::size_t r = 0; r < runs.size(); ++r) {
        const auto& run = runs[r];
        auto& gr = gpu_runs[r];
        gr = run;
        gr.sx = (run.sx != 0.0f) ? run.sx : 1.0f;
        gr.sy = (run.sy != 0.0f) ? run.sy : 1.0f;
    }

    std::array<VulkanGpuGlyphDynamic, 1024> local_glyph_dynamic;
    VulkanGpuGlyphDynamic* gpu_glyph_dynamic = local_glyph_dynamic.data();
    if (glyphs.size() > local_glyph_dynamic.size()) {
        if (text_glyph_dynamic_scratch.size() < glyphs.size()) {
            text_glyph_dynamic_scratch.resize(glyphs.size());
        }
        gpu_glyph_dynamic = text_glyph_dynamic_scratch.data();
    }
    std::vector<std::uint32_t> max_glyph_index(runs.size(), 0);
    for (const auto& glyph : glyphs) {
        if (glyph.run_index < max_glyph_index.size()) {
            max_glyph_index[glyph.run_index] =
                std::max(max_glyph_index[glyph.run_index], glyph.pad0);
        }
    }
    std::vector<std::vector<float>> tracking_prefixes(runs.size());
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const std::size_t count = static_cast<std::size_t>(max_glyph_index[r]) + 1;
        auto& prefix = tracking_prefixes[r];
        prefix.resize(count + 1, 0.0f);
        for (std::size_t i = 0; i < count; ++i) {
            prefix[i + 1] = prefix[i] + gpu_runs[r].animator_tracking *
                runtime::gpu_text_selector_weight(gpu_runs[r], static_cast<std::uint32_t>(i));
        }
    }

    std::int32_t dispatch_x0 = static_cast<std::int32_t>(dst_image.width);
    std::int32_t dispatch_y0 = static_cast<std::int32_t>(dst_image.height);
    std::int32_t dispatch_x1 = 0;
    std::int32_t dispatch_y1 = 0;

    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const auto& gs = glyphs[i];
        auto& gg = gpu_glyphs[i];
        gg.run_index = (gs.run_index < runs.size()) ? gs.run_index : 0;
        gg.atlas_page_and_flags = static_cast<std::uint32_t>(gs.atlas_page) |
            (static_cast<std::uint32_t>(gs.flags) << 16);
        gg.atlas_pos = static_cast<std::uint32_t>(gs.atlas_x) |
            (static_cast<std::uint32_t>(gs.atlas_y) << 16);
        gg.atlas_size = static_cast<std::uint32_t>(gs.atlas_w) |
            (static_cast<std::uint32_t>(gs.atlas_h) << 16);
        gg.plane_left = gs.plane_left;
        gg.plane_top = gs.plane_top;
        gg.plane_right = (gs.plane_right > gs.plane_left)
            ? gs.plane_right : (gs.plane_left + gs.atlas_w);
        gg.plane_bottom = (gs.plane_bottom > gs.plane_top)
            ? gs.plane_bottom : (gs.plane_top + gs.atlas_h);
        gg.draw_order = gs.draw_order != 0
            ? gs.draw_order : static_cast<std::uint32_t>(i);
        gg.pad0 = gs.pad0;
        gg.origin_x = gs.origin_x;
        gg.origin_y = gs.origin_y;

        const auto& run = gpu_runs[gg.run_index];
        auto& dynamic = gpu_glyph_dynamic[i];
        const std::uint32_t selector_unit =
            (run.flags >> runtime::kTextRunSelectorUnitShift) &
            runtime::kTextRunSelectorUnitMask;
        const std::uint32_t selector_index = selector_unit ==
                static_cast<std::uint32_t>(TextSelectorUnit::Word)
            ? runtime::unpack_mtsdf_glyph_unit_index(gs.flags)
            : (selector_unit == static_cast<std::uint32_t>(TextSelectorUnit::Line)
                ? runtime::unpack_mtsdf_glyph_unit_index(gs.flags)
                : gg.pad0);
        dynamic.selector_weight = runtime::gpu_text_selector_weight(run, selector_index);
        const auto& prefix = tracking_prefixes[gg.run_index];
        dynamic.tracking_prefix = prefix[std::min<std::size_t>(gg.pad0,
                                                                prefix.size() - 1)];
        const float gx0 = run.tx + gg.plane_left * run.sx;
        const float gy0 = run.ty + gg.plane_top * run.sy;
        const float gx1 = run.tx + gg.plane_right * run.sx;
        const float gy1 = run.ty + gg.plane_bottom * run.sy;
        dispatch_x0 = std::min(dispatch_x0,
                               static_cast<std::int32_t>(std::floor(gx0)));
        dispatch_y0 = std::min(dispatch_y0,
                               static_cast<std::int32_t>(std::floor(gy0)));
        dispatch_x1 = std::max(dispatch_x1,
                               static_cast<std::int32_t>(std::ceil(gx1)));
        dispatch_y1 = std::max(dispatch_y1,
                               static_cast<std::int32_t>(std::ceil(gy1)));
    }

    if (dispatch_x0 >= dispatch_x1 || dispatch_y0 >= dispatch_y1) {
        dispatch_x0 = dispatch_y0 = 0;
        dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
        dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
    }

    const VkDeviceSize glyph_bytes = static_cast<VkDeviceSize>(
        glyphs.size() * sizeof(VulkanGpuGlyphStatic));
    const VkDeviceSize run_bytes = static_cast<VkDeviceSize>(
        runs.size() * sizeof(GpuTextRunDynamic));
    const VkDeviceSize glyph_dynamic_bytes = static_cast<VkDeviceSize>(
        glyphs.size() * sizeof(VulkanGpuGlyphDynamic));
    constexpr VkDeviceSize kMaxCmdUpdateBytes = 65536;
    if (glyph_bytes > kMaxCmdUpdateBytes || run_bytes > kMaxCmdUpdateBytes ||
        glyph_dynamic_bytes > kMaxCmdUpdateBytes) {
        throw std::logic_error(
            "Vulkan text metadata exceeds vkCmdUpdateBuffer limit");
    }
    if (frame_batch.active &&
        frame_batch.pass_count >= kGlyphInstancePassesPerSlot) {
        throw std::logic_error(
            "Vulkan text upload pass capacity exhausted");
    }

    const std::size_t pass_slot = frame_batch.active
        ? frame_batch.next_slot * kGlyphInstancePassesPerSlot +
              (frame_batch.pass_count % kGlyphInstancePassesPerSlot)
        : 0;
    ensure_glyph_instance_buffer(glyph_bytes, pass_slot);
    ensure_text_run_dynamic_buffer(run_bytes, pass_slot);
    ensure_text_glyph_dynamic_buffer(glyph_dynamic_bytes, pass_slot);
    const VkBuffer glyph_buffer = glyph_instance_buffers[pass_slot].buffer;
    const VkBuffer run_buffer = text_run_dynamic_buffers[pass_slot].buffer;
    const VkBuffer glyph_dynamic_buffer = text_glyph_dynamic_buffers[pass_slot].buffer;

    std::uint64_t glyph_hash = 1469598103934665603ull;
    const auto* glyph_ptr = reinterpret_cast<const std::uint8_t*>(gpu_glyphs);
    for (std::size_t b = 0; b < glyph_bytes; ++b) {
        glyph_hash ^= glyph_ptr[b];
        glyph_hash *= 1099511628211ull;
    }
    const bool glyph_updated =
        glyph_instance_sizes[pass_slot] != glyph_bytes ||
        glyph_instance_hashes[pass_slot] != glyph_hash;

    std::uint64_t run_hash = 1469598103934665603ull;
    const auto* run_ptr = reinterpret_cast<const std::uint8_t*>(gpu_runs);
    for (std::size_t b = 0; b < run_bytes; ++b) {
        run_hash ^= run_ptr[b];
        run_hash *= 1099511628211ull;
    }
    const bool run_updated =
        text_run_dynamic_sizes[pass_slot] != run_bytes ||
        text_run_dynamic_hashes[pass_slot] != run_hash;

    std::uint64_t glyph_dynamic_hash = 1469598103934665603ull;
    const auto* glyph_dynamic_ptr =
        reinterpret_cast<const std::uint8_t*>(gpu_glyph_dynamic);
    for (std::size_t b = 0; b < glyph_dynamic_bytes; ++b) {
        glyph_dynamic_hash ^= glyph_dynamic_ptr[b];
        glyph_dynamic_hash *= 1099511628211ull;
    }
    const bool glyph_dynamic_updated =
        text_glyph_dynamic_sizes[pass_slot] != glyph_dynamic_bytes ||
        text_glyph_dynamic_hashes[pass_slot] != glyph_dynamic_hash;

    const bool atlas_is_mtsdf =
        atlas_image.text_atlas_encoding == runtime::TextAtlasEncoding::MTSDF;
    (void)atlas_is_mtsdf;

    if (frame_batch.active) {
        constexpr std::int32_t kTileSize = 16;
        constexpr std::int32_t kMaxGlyphsPerTile = 256;
        const std::int32_t tiles_x =
            (static_cast<std::int32_t>(dst_image.width) + kTileSize - 1) /
            kTileSize;
        const std::int32_t tiles_y =
            (static_cast<std::int32_t>(dst_image.height) + kTileSize - 1) /
            kTileSize;
        const VkDeviceSize tile_count_bytes =
            static_cast<VkDeviceSize>(tiles_x) *
            static_cast<VkDeviceSize>(tiles_y) * sizeof(std::uint32_t);
        const VkDeviceSize tile_index_bytes =
            tile_count_bytes * kMaxGlyphsPerTile;
        ensure_text_tile_buffer(tile_count_bytes, pass_slot, false);
        ensure_text_tile_buffer(tile_index_bytes, pass_slot, true);
        const VkBuffer count_buffer = text_tile_count_buffers[pass_slot].buffer;
        const VkBuffer index_buffer = text_tile_index_buffers[pass_slot].buffer;
        const auto bin_descriptors = allocate_text_tile_bin_descriptor_set();
        const auto raster_descriptors = allocate_text_tile_raster_descriptor_set();
        write_text_tile_bin_descriptors(bin_descriptors, glyph_buffer, run_buffer,
                                        count_buffer, index_buffer);
        write_text_tile_raster_descriptors(raster_descriptors, dst_image, atlas_image,
                                           glyph_buffer, glyph_dynamic_buffer, run_buffer,
                                           count_buffer, index_buffer);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &atlas_image});
        if (glyph_updated) {
            vkCmdUpdateBuffer(cmd, glyph_buffer, 0, glyph_bytes, gpu_glyphs);
            glyph_instance_hashes[pass_slot] = glyph_hash;
            glyph_instance_sizes[pass_slot] = glyph_bytes;
        }
        if (run_updated) {
            vkCmdUpdateBuffer(cmd, run_buffer, 0, run_bytes, gpu_runs);
            text_run_dynamic_hashes[pass_slot] = run_hash;
            text_run_dynamic_sizes[pass_slot] = run_bytes;
        }
        if (glyph_dynamic_updated) {
            vkCmdUpdateBuffer(cmd, glyph_dynamic_buffer, 0, glyph_dynamic_bytes,
                              gpu_glyph_dynamic);
            text_glyph_dynamic_hashes[pass_slot] = glyph_dynamic_hash;
            text_glyph_dynamic_sizes[pass_slot] = glyph_dynamic_bytes;
        }
        vkCmdFillBuffer(cmd, count_buffer, 0, tile_count_bytes, 0);
        std::array<VkBufferMemoryBarrier2KHR, 4> upload_barriers{};
        std::size_t upload_barrier_count = 0;
        const auto add_upload_barrier = [&](VkBuffer buffer,
                                            VkAccessFlags2KHR dst_access) {
            upload_barriers[upload_barrier_count++] = VkBufferMemoryBarrier2KHR{
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR,
                nullptr,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                dst_access,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                buffer,
                0,
                VK_WHOLE_SIZE};
        };
        if (glyph_updated) {
            add_upload_barrier(glyph_buffer, VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        if (run_updated) {
            add_upload_barrier(run_buffer, VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        if (glyph_dynamic_updated) {
            add_upload_barrier(glyph_dynamic_buffer, VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        add_upload_barrier(count_buffer,
                           VK_ACCESS_2_SHADER_READ_BIT_KHR |
                           VK_ACCESS_2_SHADER_WRITE_BIT_KHR);
        emit_buffer_barriers2(cmd,
                              std::span<const VkBufferMemoryBarrier2KHR>{
                                  upload_barriers.data(), upload_barrier_count});

        record_text_tile_bin(cmd, bin_descriptors,
                             static_cast<std::int32_t>(glyphs.size()),
                             tiles_x, tiles_y, kMaxGlyphsPerTile,
                             static_cast<std::int32_t>(dst_image.width),
                             static_cast<std::int32_t>(dst_image.height));

        const VkBufferMemoryBarrier2KHR bin_barriers[] = {
            {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
             VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
             VK_ACCESS_2_SHADER_READ_BIT_KHR,
             VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
             count_buffer, 0, VK_WHOLE_SIZE},
            {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR, nullptr,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
             VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
             VK_ACCESS_2_SHADER_READ_BIT_KHR,
             VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
             index_buffer, 0, VK_WHOLE_SIZE}};
        emit_buffer_barriers2(cmd, bin_barriers);

        record_text_tile_raster(cmd, raster_descriptors,
                                tiles_x, tiles_y, kMaxGlyphsPerTile,
                                dst_image, atlas_image);
        ++frame_batch.pass_count;
        dst_image.initialized = true;
        ++stats.passes_executed;
        return;
    }

    throw std::logic_error(
        "Vulkan tiled text batch requires an active frame batch");
}

} // namespace chronon3d::backends::vulkan
