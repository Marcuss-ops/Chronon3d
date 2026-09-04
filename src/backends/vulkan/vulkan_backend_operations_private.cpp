// vulkan_backend_operations_private.cpp — VulkanBackend::Impl public
// operation wrappers (composite, blur, glow, text/layer batches…).

#include "vulkan_backend_impl.hpp"
#include <atomic>
#include <cmath>

namespace chronon3d::backends::vulkan {

void VulkanBackend::Impl::composite(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    BlendMode mode,
                                    const std::optional<raster::BBox>& clip,
                                    bool replace) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst_image.width != src_image.width ||
        dst_image.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan composite references incompatible surfaces: dst=" +
            std::to_string(dst_image.width) + "x" +
            std::to_string(dst_image.height) + " src=" +
            std::to_string(src_image.width) + "x" +
            std::to_string(src_image.height) +
            (src_image.initialized ? "" : " src-uninitialized"));
    }
    const std::int32_t blend_mode = replace ? 2 : (mode == BlendMode::Add ? 1 : 0);
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_composite(cmd, descriptors, dst_image, src_image,
                         blend_mode, 1.0f, kIdentityTint, clip);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan composite requires an active frame batch");
}

void VulkanBackend::Impl::fill_rect(runtime::RenderSurfaceHandle destination,
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
    throw std::logic_error("Vulkan fill_rect requires an active frame batch");
}

void VulkanBackend::Impl::initialize_transparent_surface(
    runtime::RenderSurfaceHandle destination) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan transparent initialization references an empty surface");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan transparent initialization requires an active frame batch");
    }
    const auto descriptors = allocate_pass_descriptor_set();
    write_fill_rect_descriptors(descriptors, dst_image);
    const auto cmd = active_command_buffer();
    // Initialization is deliberately not part of the compiled pass index.
    // It establishes transparent untouched pixels before the first planned
    // affine/composite operation without shifting plan transitions.
    emit_unplanned_compute_sync(cmd, {&dst_image});
    record_fill_rect(cmd, descriptors, dst_image, 0, 0,
                     static_cast<std::int32_t>(dst_image.width),
                     static_cast<std::int32_t>(dst_image.height),
                     Color::transparent());
    dst_image.initialized = true;
    // The initialization dispatch is intentionally outside the compiled
    // pass stream. Publish its shader writes explicitly before the first
    // planned pass touches this image; otherwise that pass may still use the
    // plan's pre-initialization state and race the transparent clear.
    emit_unplanned_compute_sync(cmd, {&dst_image});
    ++stats.passes_executed;
}

void VulkanBackend::Impl::fill_solid_shape(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Vec4& shape, const Vec4& line,
    const Color& color) {
    auto& dst_image = resolve_image(destination);
    if (dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument("Vulkan shape fill references an empty surface");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(descriptors, dst_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        record_fill_rect(cmd, descriptors, dst_image, x0, y0, x1, y1,
                         color, shape, line);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error(
        "Vulkan fill_solid_shape requires an active frame batch");
}

void VulkanBackend::Impl::fill_path(runtime::RenderSurfaceHandle destination,
                                    std::span<const Vec2> vertices,
                                    const Color& color) {
    if (vertices.size() < 3 || vertices.size() > 8) {
        throw std::invalid_argument("Vulkan path fill supports 3..8 vertices");
    }
    auto& dst_image = resolve_image(destination);
    std::array<Vec2, 8> polygon{};
    std::copy(vertices.begin(), vertices.end(), polygon.begin());
    const Vec4 shape{4.0f, 0.0f, 0.0f, static_cast<float>(vertices.size())};
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_fill_rect_descriptors(descriptors, dst_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image});
        record_fill_rect(cmd, descriptors, dst_image, 0, 0,
                         static_cast<std::int32_t>(dst_image.width),
                         static_cast<std::int32_t>(dst_image.height),
                         color, shape, Vec4{0.0f}, polygon);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan fill_path requires an active frame batch");
}

void VulkanBackend::Impl::transform(runtime::RenderSurfaceHandle destination,
                                    runtime::RenderSurfaceHandle source,
                                    int offset_x, int offset_y, float opacity) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan transform references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_transform(cmd, descriptors, dst_image, src_image,
                         offset_x, offset_y, opacity);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan transform requires an active frame batch");
}

void VulkanBackend::Impl::transform_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform_value) {
    auto& dst_image = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan affine transform references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst_image, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst_image, &src_image});
        record_transform_affine(cmd, descriptors,
                                dst_image, src_image, transform_value);
        dst_image.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error(
        "Vulkan affine transform requires an active frame batch");
}

void VulkanBackend::Impl::blur(runtime::RenderSurfaceHandle destination,
                               runtime::RenderSurfaceHandle source,
                               float radius, bool horizontal) {
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
    throw std::logic_error("Vulkan blur requires an active frame batch");
}

void VulkanBackend::Impl::glow(runtime::RenderSurfaceHandle destination,
                               runtime::RenderSurfaceHandle source,
                               runtime::RenderSurfaceHandle scratch_horizontal,
                               runtime::RenderSurfaceHandle scratch_vertical,
                               float radius, float intensity,
                               const Color& tint) {
    if (!(radius >= 0.0f) || radius > 32.0f) {
        throw std::invalid_argument("Vulkan glow radius must be within [0, 32]");
    }
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    auto& horizontal = resolve_image(scratch_horizontal);
    auto& vertical = resolve_image(scratch_vertical);
    if (!src_image.initialized || !dst.initialized ||
        src_image.width != dst.width || src_image.height != dst.height ||
        horizontal.width != src_image.width || horizontal.height != src_image.height ||
        vertical.width != src_image.width || vertical.height != src_image.height) {
        throw std::invalid_argument("Vulkan glow surfaces have incompatible dimensions");
    }
    const float tint_rgba[4] = {tint.r, tint.g, tint.b, tint.a};

    if (frame_batch.active) {
        const auto horizontal_descriptor = allocate_pass_descriptor_set();
        write_descriptors(horizontal_descriptor, horizontal, src_image);
        const auto vertical_descriptor = allocate_pass_descriptor_set();
        write_descriptors(vertical_descriptor, vertical, horizontal);
        const auto composite_descriptor = allocate_pass_descriptor_set();
        write_descriptors(composite_descriptor, dst, vertical);
        const auto cmd = active_command_buffer();

        // One command-plan pass => consume its canonical transition stream
        // exactly once. Internal dispatch dependencies use the same
        // ResourceTransition -> Sync2 translator.
        emit_pass_sync(cmd, {&dst, &src_image, &horizontal, &vertical});
        record_blur(cmd, horizontal_descriptor, horizontal, src_image,
                    radius, true);
        horizontal.initialized = true;

        runtime::ResourceTransition horizontal_ready;
        horizontal_ready.range = runtime::image_range(runtime::ResourceAspect::Color);
        horizontal_ready.before = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderWrite,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        horizontal_ready.after = runtime::ResourceState{
            .stages = runtime::PipelineStage::ComputeShader,
            .access = runtime::AccessMask::ShaderRead,
            .layout = runtime::ResourceLayout::General,
            .queue = runtime::QueueClass::Compute,
        };
        emit_resource_transition(cmd, horizontal.image, horizontal_ready);

        record_blur(cmd, vertical_descriptor, vertical, horizontal,
                    radius, false);
        vertical.initialized = true;

        runtime::ResourceTransition vertical_ready = horizontal_ready;
        emit_resource_transition(cmd, vertical.image, vertical_ready);

        record_composite(cmd, composite_descriptor, dst, vertical,
                         1, intensity, tint_rgba);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }

    throw std::logic_error("Vulkan glow requires an active frame batch");
}

void VulkanBackend::Impl::color_adjust(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float brightness, float contrast,
    const Color& tint, float tint_amount) {
    auto& dst = resolve_image(destination);
    auto& src_image = resolve_image(source);
    if (!src_image.initialized ||
        dst.width != src_image.width || dst.height != src_image.height) {
        throw std::invalid_argument(
            "Vulkan color adjust references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_descriptors(descriptors, dst, src_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &src_image});
        record_color_adjust(cmd, descriptors, dst, src_image,
                            brightness, contrast, tint, tint_amount);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan color_adjust requires an active frame batch");
}

void VulkanBackend::Impl::matte(runtime::RenderSurfaceHandle destination,
                                runtime::RenderSurfaceHandle target,
                                runtime::RenderSurfaceHandle matte_surface,
                                bool luma, bool inverted) {
    auto& dst = resolve_image(destination);
    auto& target_image = resolve_image(target);
    auto& matte_image = resolve_image(matte_surface);
    if (!target_image.initialized || !matte_image.initialized ||
        dst.width != target_image.width || dst.height != target_image.height ||
        dst.width != matte_image.width || dst.height != matte_image.height) {
        throw std::invalid_argument("Vulkan matte references incompatible surfaces");
    }
    if (frame_batch.active) {
        const auto descriptors = allocate_pass_descriptor_set();
        write_matte_descriptors(descriptors, dst, target_image, matte_image);
        const auto cmd = active_command_buffer();
        emit_pass_sync(cmd, {&dst, &target_image, &matte_image});
        record_matte(cmd, descriptors, dst, target_image, matte_image,
                     luma, inverted);
        dst.initialized = true;
        ++frame_batch.pass_count;
        ++stats.passes_executed;
        return;
    }
    throw std::logic_error("Vulkan matte requires an active frame batch");
}

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

struct alignas(16) GpuGlyphStatic {
    std::uint32_t run_index{0};
    std::uint32_t atlas_page_and_flags{0};
    std::uint32_t atlas_pos{0};
    std::uint32_t atlas_size{0};
    float plane_left{0.0f};
    float plane_top{0.0f};
    float plane_right{0.0f};
    float plane_bottom{0.0f};
    std::uint32_t draw_order{0};
    std::uint32_t pad0{0};
    std::uint32_t pad1{0};
    std::uint32_t pad2{0};
};
static_assert(sizeof(GpuGlyphStatic) == 48,
              "GpuGlyphStatic must be 48 bytes matching std430");

struct alignas(16) GpuTextRunDynamic {
    float tx{0.0f};
    float ty{0.0f};
    float sx{1.0f};
    float sy{1.0f};
    float opacity{1.0f};
    std::uint32_t color{0xFFFFFFFF};
    float pad0{0.0f};
    float pad1{0.0f};
};
static_assert(sizeof(GpuTextRunDynamic) == 32,
              "GpuTextRunDynamic must be 32 bytes");

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
    if (!dst_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan draw_text_batch references an invalid destination surface");
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

    std::array<GpuGlyphStatic, 1024> local_glyphs;
    std::vector<GpuGlyphStatic> heap_glyphs;
    GpuGlyphStatic* gpu_glyphs = local_glyphs.data();
    if (glyphs.size() > local_glyphs.size()) {
        heap_glyphs.resize(glyphs.size());
        gpu_glyphs = heap_glyphs.data();
    }

    std::array<GpuTextRunDynamic, 128> local_runs;
    std::vector<GpuTextRunDynamic> heap_runs;
    GpuTextRunDynamic* gpu_runs = local_runs.data();
    if (runs.size() > local_runs.size()) {
        heap_runs.resize(runs.size());
        gpu_runs = heap_runs.data();
    }

    for (std::size_t r = 0; r < runs.size(); ++r) {
        const auto& run = runs[r];
        auto& gr = gpu_runs[r];
        gr.tx = run.tx;
        gr.ty = run.ty;
        gr.sx = (run.sx != 0.0f) ? run.sx : 1.0f;
        gr.sy = (run.sy != 0.0f) ? run.sy : 1.0f;
        gr.opacity = run.opacity;
        gr.color = run.color;
        gr.pad0 = 0.0f;
        gr.pad1 = 0.0f;
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
        gg.pad0 = gg.pad1 = gg.pad2 = 0;

        const auto& run = gpu_runs[gg.run_index];
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
        glyphs.size() * sizeof(GpuGlyphStatic));
    const VkDeviceSize run_bytes = static_cast<VkDeviceSize>(
        runs.size() * sizeof(GpuTextRunDynamic));
    constexpr VkDeviceSize kMaxCmdUpdateBytes = 65536;
    if (glyph_bytes > kMaxCmdUpdateBytes || run_bytes > kMaxCmdUpdateBytes) {
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
    const VkBuffer glyph_buffer = glyph_instance_buffers[pass_slot].buffer;
    const VkBuffer run_buffer = text_run_dynamic_buffers[pass_slot].buffer;

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

    const bool atlas_is_mtsdf =
        atlas_image.text_atlas_encoding == runtime::TextAtlasEncoding::MTSDF;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        gpu_glyphs[i].atlas_page_and_flags =
            static_cast<std::uint32_t>(glyphs[i].atlas_page) |
            (static_cast<std::uint32_t>(atlas_is_mtsdf ? 1u : 0u) << 16);
    }

    if (frame_batch.active) {
        constexpr std::int32_t kTileSize = 16;
        constexpr std::int32_t kMaxGlyphsPerTile = 64;
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
                                           glyph_buffer, run_buffer,
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
        vkCmdFillBuffer(cmd, count_buffer, 0, tile_count_bytes, 0);

        std::vector<VkBufferMemoryBarrier2KHR> upload_barriers;
        upload_barriers.reserve(3);
        const auto add_upload_barrier = [&](VkBuffer buffer,
                                            VkAccessFlags2KHR dst_access) {
            upload_barriers.push_back(VkBufferMemoryBarrier2KHR{
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
                VK_WHOLE_SIZE});
        };
        if (glyph_updated) {
            add_upload_barrier(glyph_buffer, VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        if (run_updated) {
            add_upload_barrier(run_buffer, VK_ACCESS_2_SHADER_READ_BIT_KHR);
        }
        add_upload_barrier(count_buffer,
                           VK_ACCESS_2_SHADER_READ_BIT_KHR |
                           VK_ACCESS_2_SHADER_WRITE_BIT_KHR);
        emit_buffer_barriers2(cmd, upload_barriers);

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
};
static_assert(sizeof(GpuLayerInstance) == 96,
              "GpuLayerInstance must be 96 bytes");

} // namespace

void VulkanBackend::Impl::execute_layer_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::LayerInstance> instances,
    std::span<const runtime::RenderSurfaceHandle> resources,
    std::span<const float> transforms,
    std::span<const float> paints) {
    (void)paints;
    if (instances.empty()) return;
    ++stats.layer_batch_calls;
    stats.layer_instances_processed += instances.size();

    auto& dst_image = resolve_image(destination);
    if (!dst_image.initialized || dst_image.width == 0 || dst_image.height == 0) {
        throw std::invalid_argument(
            "Vulkan layer batch references an invalid destination surface");
    }
    if (!frame_batch.active) {
        throw std::logic_error("Vulkan layer batch requires an active frame batch");
    }

    const auto cmd = active_command_buffer();
    bool pass_sync_emitted = false;
    std::size_t group_index = 0;
    std::size_t start_idx = 0;
    while (start_idx < instances.size()) {
        const auto res_idx = instances[start_idx].resource_index;
        if (res_idx >= resources.size()) {
            throw std::invalid_argument(
                "Vulkan layer batch resource_index out of range");
        }
        const auto source_handle = resources[res_idx];
        if (source_handle == runtime::kInvalidRenderSurfaceHandle) {
            throw std::invalid_argument(
                "Vulkan layer batch references an invalid image resource");
        }

        std::size_t end_idx = start_idx + 1;
        while (end_idx < instances.size() &&
               instances[end_idx].resource_index == res_idx) {
            ++end_idx;
        }

        auto& src_image = resolve_image(source_handle);
        if (!src_image.initialized || src_image.width == 0 || src_image.height == 0) {
            throw std::invalid_argument(
                "Vulkan layer batch source image is not initialized");
        }

        const std::size_t count = end_idx - start_idx;
        std::array<GpuLayerInstance, 512> local_instances;
        std::vector<GpuLayerInstance> heap_instances;
        GpuLayerInstance* gpu_instances = local_instances.data();
        if (count > local_instances.size()) {
            heap_instances.resize(count);
            gpu_instances = heap_instances.data();
        }

        std::int32_t dispatch_x0 = static_cast<std::int32_t>(dst_image.width);
        std::int32_t dispatch_y0 = static_cast<std::int32_t>(dst_image.height);
        std::int32_t dispatch_x1 = 0;
        std::int32_t dispatch_y1 = 0;

        for (std::size_t i = 0; i < count; ++i) {
            const auto& inst = instances[start_idx + i];
            auto& gpu_inst = gpu_instances[i];
            gpu_inst = GpuLayerInstance{};
            float dx0 = inst.dst_x0;
            float dy0 = inst.dst_y0;
            float dx1 = inst.dst_x1;
            float dy1 = inst.dst_y1;
            if (dx1 <= 1.0f && dy1 <= 1.0f &&
                (dx1 > dx0 || dy1 > dy0)) {
                dx0 *= static_cast<float>(dst_image.width);
                dy0 *= static_cast<float>(dst_image.height);
                dx1 *= static_cast<float>(dst_image.width);
                dy1 *= static_cast<float>(dst_image.height);
            }
            gpu_inst.dst_rect[0] = dx0;
            gpu_inst.dst_rect[1] = dy0;
            gpu_inst.dst_rect[2] = dx1;
            gpu_inst.dst_rect[3] = dy1;
            gpu_inst.src_rect[0] = inst.src_x0;
            gpu_inst.src_rect[1] = inst.src_y0;
            gpu_inst.src_rect[2] =
                (inst.src_x1 == 0.0f && inst.src_y1 == 0.0f) ? 1.0f : inst.src_x1;
            gpu_inst.src_rect[3] =
                (inst.src_x1 == 0.0f && inst.src_y1 == 0.0f) ? 1.0f : inst.src_y1;
            gpu_inst.limits[0] = static_cast<float>(src_image.width);
            gpu_inst.limits[1] = static_cast<float>(src_image.height);
            gpu_inst.limits[2] = inst.opacity;
            gpu_inst.limits[3] = 1.0f;

            if (inst.transform_index > 0 &&
                (inst.transform_index + 1) * 16 <= transforms.size()) {
                gpu_inst.has_affine = 1;
                const float* m = transforms.data() + inst.transform_index * 16;
                gpu_inst.affine_row0[0] = m[0];
                gpu_inst.affine_row0[1] = m[4];
                gpu_inst.affine_row0[2] = m[12];
                gpu_inst.affine_row1[0] = m[1];
                gpu_inst.affine_row1[1] = m[5];
                gpu_inst.affine_row1[2] = m[13];
                dispatch_x0 = dispatch_y0 = 0;
                dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
                dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
            } else {
                gpu_inst.has_affine = 0;
                dispatch_x0 = std::min(
                    dispatch_x0, static_cast<std::int32_t>(std::floor(dx0)));
                dispatch_y0 = std::min(
                    dispatch_y0, static_cast<std::int32_t>(std::floor(dy0)));
                dispatch_x1 = std::max(
                    dispatch_x1, static_cast<std::int32_t>(std::ceil(dx1)));
                dispatch_y1 = std::max(
                    dispatch_y1, static_cast<std::int32_t>(std::ceil(dy1)));
            }
            gpu_inst.blend_mode = (inst.blend == BlendMode::Add) ? 1u : 0u;
            gpu_inst.resource_idx = inst.resource_index;
        }

        if (dispatch_x0 >= dispatch_x1 || dispatch_y0 >= dispatch_y1) {
            dispatch_x0 = dispatch_y0 = 0;
            dispatch_x1 = static_cast<std::int32_t>(dst_image.width);
            dispatch_y1 = static_cast<std::int32_t>(dst_image.height);
        }

        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(count * sizeof(GpuLayerInstance));
        const std::size_t instance_slot =
            frame_batch.next_slot * kGlyphInstancePassesPerSlot +
            ((frame_batch.pass_count + group_index) % kGlyphInstancePassesPerSlot);
        ensure_layer_instance_buffer(bytes, instance_slot);
        const VkBuffer instance_buffer = layer_instance_buffers[instance_slot].buffer;

        std::uint64_t instance_hash = 1469598103934665603ull;
        const auto* byte_ptr =
            reinterpret_cast<const std::uint8_t*>(gpu_instances);
        for (std::size_t b = 0; b < bytes; ++b) {
            instance_hash ^= byte_ptr[b];
            instance_hash *= 1099511628211ull;
        }
        const bool instance_updated =
            layer_instance_sizes[instance_slot] != bytes ||
            layer_instance_hashes[instance_slot] != instance_hash;

        const auto descriptors = allocate_pass_descriptor_set();
        write_layer_batch_descriptors(descriptors, dst_image, src_image,
                                      instance_buffer);
        if (!pass_sync_emitted) {
            emit_pass_sync(cmd, {&dst_image, &src_image});
            pass_sync_emitted = true;
        }
        if (instance_updated) {
            vkCmdUpdateBuffer(cmd, instance_buffer, 0, bytes, gpu_instances);
            layer_instance_hashes[instance_slot] = instance_hash;
            layer_instance_sizes[instance_slot] = bytes;
        }
        record_layer_batch(cmd, descriptors, dst_image,
                           static_cast<std::int32_t>(count),
                           instance_buffer, instance_updated,
                           dispatch_x0, dispatch_y0, dispatch_x1, dispatch_y1);
        dst_image.initialized = true;
        start_idx = end_idx;
        ++group_index;
    }

    ++frame_batch.pass_count;
    ++stats.passes_executed;
}

void VulkanBackend::Impl::ensure_images(std::uint32_t width,
                                        std::uint32_t height) {
    if (dst.width == width && dst.height == height &&
        src.image != VK_NULL_HANDLE) return;
    destroy_image(dst);
    destroy_image(src);
    check(vkResetDescriptorPool(device, descriptor_pool, 0),
          "vkResetDescriptorPool");
    make_image(dst, width, height);
    make_image(src, width, height);
    descriptor_set = VK_NULL_HANDLE;
    glow_descriptor_sets = {};
    bind_descriptors(dst, src);
}

void VulkanBackend::Impl::ensure_staging(VkDeviceSize bytes) {
    if (staging.size >= bytes) return;
    if (staging.buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(staging);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    staging = memory_manager.create_buffer(info, VulkanMemoryClass::HostUpload);
    if (debug_context) {
        debug_context->set_buffer_name(staging.buffer,
                                       "Chronon3D.Buffer.Staging");
    }
    ++stats.staging_allocations;
}

void VulkanBackend::Impl::ensure_glyph_instance_buffer(VkDeviceSize bytes,
                                                        std::size_t index) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    if (glyph_instance_buffers[index].size >= bytes) return;
    if (glyph_instance_buffers[index].buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(glyph_instance_buffers[index]);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    glyph_instance_buffers[index] =
        memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(glyph_instance_buffers[index].buffer,
                                       "Chronon3D.Buffer.GlyphInstance");
    }
    glyph_instance_hashes[index] = 0;
    glyph_instance_sizes[index] = 0;
}

void VulkanBackend::Impl::ensure_layer_instance_buffer(VkDeviceSize bytes,
                                                        std::size_t index) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    if (layer_instance_buffers[index].size >= bytes) return;
    if (layer_instance_buffers[index].buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(layer_instance_buffers[index]);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    layer_instance_buffers[index] =
        memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(layer_instance_buffers[index].buffer,
                                       "Chronon3D.Buffer.LayerInstance");
    }
    layer_instance_hashes[index] = 0;
    layer_instance_sizes[index] = 0;
}

void VulkanBackend::Impl::ensure_text_run_dynamic_buffer(VkDeviceSize bytes,
                                                          std::size_t index) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    if (text_run_dynamic_buffers[index].size >= bytes) return;
    if (text_run_dynamic_buffers[index].buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(text_run_dynamic_buffers[index]);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    text_run_dynamic_buffers[index] =
        memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(text_run_dynamic_buffers[index].buffer,
                                       "Chronon3D.Buffer.TextRunDynamic");
    }
    text_run_dynamic_hashes[index] = 0;
    text_run_dynamic_sizes[index] = 0;
}

void VulkanBackend::Impl::ensure_text_tile_buffer(VkDeviceSize bytes,
                                                   std::size_t index,
                                                   bool indices) {
    if (index >= kGlyphInstanceRingSize) index = 0;
    auto& buffer_alloc = indices ? text_tile_index_buffers[index]
                                 : text_tile_count_buffers[index];
    if (buffer_alloc.size >= bytes) return;
    if (buffer_alloc.buffer != VK_NULL_HANDLE) {
        memory_manager.destroy_buffer(buffer_alloc);
    }
    const VkBufferCreateInfo info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr};
    buffer_alloc = memory_manager.create_buffer(info, VulkanMemoryClass::DeviceLocal);
    if (debug_context) {
        debug_context->set_buffer_name(
            buffer_alloc.buffer,
            indices ? "Chronon3D.Buffer.TextTileIndex" :
                      "Chronon3D.Buffer.TextTileCount");
    }
}

void VulkanBackend::Impl::begin_command_buffer() {
    wait_for_pending();
    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
    check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
}

void VulkanBackend::Impl::wait_for_pending() {
    if (pending_timeline_value != 0) {
        const auto wait_start = profiling::now();
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
              "vkWaitForFences");
        const auto wait_us = static_cast<std::uint64_t>(
            profiling::elapsed_us(wait_start));
        stats.gpu_wait_cpu_us += wait_us;
        ++stats.standalone_wait_count;
        stats.standalone_wait_us += wait_us;
        check(vkResetFences(device, 1, &fence), "vkResetFences");
        pending_timeline_value = 0;
    }
    for (std::size_t i = 0; i < FrameBatchState::kSlotCount; ++i) {
        if (!frame_batch.in_flight[i]) continue;
        const auto wait_start = profiling::now();
        const VkResult wait_result = vkWaitForFences(
            device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX);
        if (wait_result == VK_ERROR_DEVICE_LOST) {
            spdlog::error(
                "[vulkan] DEVICE LOST REPORT phase=wait_for_pending slot={} "
                "in_flight_slots=[{},{},{}] pending_timeline={}",
                i, frame_batch.in_flight[0], frame_batch.in_flight[1],
                frame_batch.in_flight[2], pending_timeline_value);
        }
        check(wait_result, "vkWaitForFences(frame batch slot)");
        const auto wait_us = static_cast<std::uint64_t>(
            profiling::elapsed_us(wait_start));
        stats.gpu_wait_cpu_us += wait_us;
        ++stats.frame_batch_drain_wait_count;
        stats.frame_batch_drain_wait_us += wait_us;
        check(vkResetFences(device, 1, &frame_batch.fences[i]),
              "vkResetFences(frame batch slot)");
        frame_batch.in_flight[i] = false;
        read_gpu_timestamps(i);
    }
    for (auto& slot : uploads.slots) wait_upload_slot(slot);
}

std::uint64_t VulkanBackend::Impl::submit(bool wait_for_completion) {
    check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
    const auto signal_value = ++next_timeline_value;
    std::vector<VkSemaphore> wait_semaphores;
    std::vector<VkPipelineStageFlags> wait_stages;
    std::vector<VkSemaphore> signal_semaphores{timeline_semaphore};
    std::vector<std::uint64_t> signal_values{signal_value};
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (const auto physical_slot : surfaces.cuda_ready_surfaces) {
        const auto it = surfaces.physical_surfaces.find(physical_slot);
        if (it == surfaces.physical_surfaces.end()) continue;
        wait_semaphores.push_back(it->second.image.cuda_to_vulkan);
        wait_stages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
        signal_values.push_back(0);
    }
    for (const auto physical_slot : surfaces.cuda_export_ready_surfaces) {
        if (surfaces.cuda_ready_surfaces.contains(physical_slot)) continue;
        const auto it = surfaces.physical_surfaces.find(physical_slot);
        if (it == surfaces.physical_surfaces.end()) continue;
        signal_semaphores.push_back(it->second.image.vulkan_to_cuda);
        signal_values.push_back(0);
    }
#endif
    const VkTimelineSemaphoreSubmitInfo timeline_submit{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
        0, nullptr, static_cast<std::uint32_t>(signal_values.size()),
        signal_values.data()};
    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO, &timeline_submit,
        static_cast<std::uint32_t>(wait_semaphores.size()),
        wait_semaphores.data(), wait_stages.data(), 1, &command_buffer,
        static_cast<std::uint32_t>(signal_semaphores.size()),
        signal_semaphores.data()};
    const auto submit_start = profiling::now();
    check(vkQueueSubmit(queue, 1, &submit_info, fence), "vkQueueSubmit");
    stats.gpu_submit_cpu_us += static_cast<std::uint64_t>(
        profiling::elapsed_us(submit_start));
    ++stats.submissions;
    pending_timeline_value = signal_value;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    surfaces.cuda_ready_surfaces.clear();
    surfaces.cuda_export_ready_surfaces.clear();
#endif
    if (wait_for_completion) wait_for_pending();
    return signal_value;
}

void VulkanBackend::Impl::composite(Framebuffer& destination,
                                    const Framebuffer& source) {
    const auto width = static_cast<std::uint32_t>(destination.width());
    const auto height = static_cast<std::uint32_t>(destination.height());
    const VkDeviceSize image_bytes = static_cast<VkDeviceSize>(
        runtime::tight_surface_bytes(runtime::PixelFormat::Rgba32Float,
                                     width, height));
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
    std::memcpy(staging.mapped, packed.data(),
                static_cast<std::size_t>(image_bytes * 2));

    const VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    check(vkResetCommandBuffer(command_buffer, 0), "vkResetCommandBuffer");
    check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");
    transition(command_buffer, src.image,
               src.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    transition(command_buffer, dst.image,
               dst.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkBufferImageCopy source_copy{
        0, width, height,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0, 0, 0}, {width, height, 1}};
    const VkBufferImageCopy destination_copy{
        image_bytes, width, height,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0, 0, 0}, {width, height, 1}};
    vkCmdCopyBufferToImage(command_buffer, staging.buffer, src.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &source_copy);
    vkCmdCopyBufferToImage(command_buffer, staging.buffer, dst.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &destination_copy);
    transition(command_buffer, src.image,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    transition(command_buffer, dst.image,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(
        command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        reinterpret_cast<VkPipeline>(kernels.registry.resolve(GpuKernelId::Composite)));
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            kernels.general_layout, 0, 1,
                            &descriptor_set, 0, nullptr);
    vkCmdDispatch(command_buffer, (width + 15) / 16, (height + 15) / 16, 1);
    transition(command_buffer, dst.image, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const VkBufferImageCopy output_copy{
        image_bytes * 2, width, height,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {0, 0, 0}, {width, height, 1}};
    vkCmdCopyImageToBuffer(command_buffer, dst.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer, 1, &output_copy);
    transition(command_buffer, dst.image,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
    src.initialized = true;
    dst.initialized = true;
    submit();

    const float* output = static_cast<const float*>(staging.mapped) +
        (image_bytes * 2 / sizeof(float));
    for (int y = 0; y < destination.height(); ++y) {
        for (int x = 0; x < destination.width(); ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * width + x) * 4;
            destination.set_pixel(
                x, y,
                Color{output[index], output[index + 1],
                      output[index + 2], output[index + 3]});
        }
    }
}

} // namespace chronon3d::backends::vulkan
