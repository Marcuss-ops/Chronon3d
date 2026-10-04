// vulkan_backend_surface_api.cpp — VulkanBackend public surface and operation
// adapters (create/upload/download/release, composite/blur/… wrappers), the
// legacy Framebuffer composite adapter, and the make_vulkan_backend factory.
// Each adapter is a thin batched wrapper over the Impl ops in
// vulkan_backend_operations_private.cpp, so touching this TU never recompiles
// the kernel recording, descriptor or surface-store code.
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

namespace chronon3d::backends::vulkan {

template <typename Fn>
graph::RenderOpResult VulkanBackend::run_batched_surface_op(
    std::span<const runtime::RenderSurfaceHandle> touched,
    std::span<const runtime::RenderSurfaceHandle> read_surfaces,
    Fn&& fn) {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Two recording shapes share this funnel:
    //   * legacy per-op path — the op owns the batch, so both contention
    //     domains are held for its duration and it resolves through the store;
    //   * decoupled path — a batch already owns the recording window, so the
    //     surface domain is released and the op's surfaces are leased instead.
    // `touched` is the op's own resolved set, `read_surfaces` the surfaces it
    // only reads through the compiled transition stream.
    try {
        // Batch ownership decides which locking shape this call needs, so it
        // is read under the submission lock instead of assumed.
        bool owns_batch = false;
        {
            Impl::DomainLock submission_lock(
                *m_impl, Impl::LockDomain::Submission);
            owns_batch = !m_impl->frame_batch.active;
        }
        if (owns_batch) {
            // Legacy per-op submit path: it drives a full batch transition
            // (begin → record → submit → deferred surface flush), which needs
            // both domains. Re-checked under the locks, because another batch
            // driver may have started one between the two sections.
            // Audit N4 measurement: the two domains this site
            // still needs, timed (see Impl::DomainLock).
            Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
            if (!m_impl->frame_batch.active) {
                begin_frame_batch_unlocked();
                // Both domains are held here, so the op resolves through the
                // store exactly like before: no lease is needed, and none is
                // taken (a pin that is never read would only add a way to
                // unbalance the plan-wide pin counts).
                fn();
                end_frame_batch_unlocked();
                return graph::RenderOpResult(graph::RenderOpOutcome{});
            }
            // Lost the race: a batch is active now, so fall through to the
            // decoupled recording path below.
        }

        // Decoupled recording path (compiled plan batch or command batch).
        // The surface-store lock is taken only for the lease lookup and is
        // released before the submission ring is touched; the generation
        // token is revalidated across that hand-off, before the first command
        // is recorded.
        //
        // Pin and release are paired per successful pin only: a failed pin has
        // already rolled itself back, and releasing a list that was never
        // pinned would steal a count from an enclosing plan batch and unpin a
        // slot that is still being recorded into.
        bool touched_pinned = false;
        bool reads_pinned = false;
        try {
            m_impl->pin_recording_leases(touched);
            touched_pinned = true;
            if (!read_surfaces.empty()) {
                m_impl->pin_recording_leases(read_surfaces);
                reads_pinned = true;
            }
            // From here to the end of fn() the surface domain is released for
            // this thread, so resolve_image() must go through the leases.
            VulkanBackend::Impl::RecordingResolveScope recording;
            // Submission domain only, and timed: the store lock was released
            // before this point (audit N4), so this is the recording phase's
            // real contention cost.
            Impl::DomainLock submission_lock(
                *m_impl, Impl::LockDomain::Submission);
            if (!m_impl->revalidate_recording_leases()) {
                throw std::logic_error(
                    "Vulkan recording lease was invalidated across the "
                    "surface→submission hand-off (audit N4 token)");
            }
            fn();
        } catch (...) {
            if (reads_pinned) m_impl->release_recording_leases(read_surfaces);
            if (touched_pinned) m_impl->release_recording_leases(touched);
            throw;
        }
        if (reads_pinned) m_impl->release_recording_leases(read_surfaces);
        m_impl->release_recording_leases(touched);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)touched; (void)read_surfaces; (void)fn;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::create_surface(
    runtime::RenderSurfaceHandle handle, const runtime::SurfaceDesc& desc) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        std::lock_guard lock(m_impl->surfaces.mutex);
        (void)m_impl->ensure_surface(handle, desc);
        // Generic graph surfaces are transient even when created while a
        // batch is recording. Only CUDA/NVENC external surfaces are unplanned;
        // marking every in-batch surface unplanned prevents reclamation.
        ++m_impl->stats.surface_creations;
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::InvalidInput, error.what()));
    }
#else
    (void)handle; (void)desc;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::create_surface: Vulkan support is disabled"));
#endif
}


graph::RenderOpResult VulkanBackend::release_surface(
    runtime::RenderSurfaceHandle handle) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        if (handle == runtime::kInvalidRenderSurfaceHandle) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::InvalidInput,
                "VulkanBackend::release_surface: invalid handle"));
        }
        // A command buffer may still reference any surface created during the
        // active batch.  "Unplanned" only means that the barrier planner does
        // not know the handle; it must never shorten the Vulkan lifetime.
        if (surface_lifecycle_diag_enabled()) {
            spdlog::info("[surface-lifecycle] release handle={} defer={}", handle,
                         (m_impl->frame_batch.active || m_impl->command_batch_active));
        }
        if (m_impl->frame_batch.active || m_impl->command_batch_active) {
            m_impl->defer_surface_release(handle);
            return graph::RenderOpResult(graph::RenderOpOutcome{});
        }
        m_impl->release_surface_now(handle);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::release_surface: Vulkan support is disabled"));
#endif
}

bool VulkanBackend::is_native_surface_valid(
    runtime::RenderSurfaceHandle handle) const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl || handle == runtime::kInvalidRenderSurfaceHandle) return false;
    std::lock_guard lock(m_impl->surfaces.mutex);
    return m_impl->surface_valid(handle);
#else
    (void)handle;
    return false;
#endif
}

bool VulkanBackend::wait_for_pending_submissions() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (!m_impl) return false;
    try {
        // Retiring completed frame transient surfaces requires the surface store lock.
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->wait_for_pending();
        return true;
    } catch (const std::exception& error) {
        spdlog::error("[vulkan] native submission drain failed: {}", error.what());
        return false;
    }
#else
    return false;
#endif
}

std::size_t VulkanBackend::native_surface_ring_capacity() const noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    return Impl::VulkanSubmissionRing::kSlotCount + 1;
#else
    return 1;
#endif
}

void VulkanBackend::release_frame_transient_surfaces() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (m_impl) {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->release_frame_transient_surfaces();
    }
#endif
}

void VulkanBackend::retire_frame_transient_surfaces() noexcept {
#ifdef CHRONON3D_ENABLE_VULKAN
    if (m_impl) {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        // Keep deferred frame-batch releases alive through the terminal
        // native-output synchronization. The render pipeline calls this only
        // after its readback/encode consumer has finished with the output.
        m_impl->flush_deferred_surface_releases();
        m_impl->retire_completed_frame_transient_surfaces();
    }
#endif
}

graph::RenderOpResult VulkanBackend::download_surface(
    runtime::RenderSurfaceHandle handle, std::span<float> rgba) {
#ifdef CHRONON3D_ENABLE_VULKAN
    try {
        // Audit N4 measurement: the two domains this site
        // still needs, timed (see Impl::DomainLock).
        Impl::DomainLock domain_lock(*m_impl, Impl::LockDomain::Dual);
        m_impl->download(handle, rgba);
        return graph::RenderOpResult(graph::RenderOpOutcome{});
    } catch (const std::exception& error) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::ExecutionFailure, error.what()));
    }
#else
    (void)handle; (void)rgba;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::download_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::composite_surfaces(
    runtime::RenderSurfaceHandle destination, runtime::RenderSurfaceHandle source,
    BlendMode mode, CompositeOperator op,
    const std::optional<raster::BBox>& clip) {
    if ((mode != BlendMode::Normal && mode != BlendMode::Add &&
         mode != BlendMode::Multiply && mode != BlendMode::Screen &&
         mode != BlendMode::Overlay) ||
        op != CompositeOperator::SourceOver) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            graph::RenderBackendErrorCode::UnsupportedCapability,
            "VulkanBackend::composite_surfaces: blend mode/operator is unsupported"));
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination, source, mode, clip);
    });
#else
    (void)destination; (void)source;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::composite_surfaces: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::copy_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination, source, BlendMode::Normal, clip, true);
    });
#else
    (void)destination; (void)source; (void)clip;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::copy_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_rect_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const Color& color) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_rect(destination, x0, y0, x1, y1, color);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_rect_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::initialize_transparent_surface(
    runtime::RenderSurfaceHandle destination) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->initialize_transparent_surface(destination);
    });
#else
    (void)destination;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::initialize_transparent_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_solid_shape_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    const Vec4& shape, const Vec4& line, const Color& color,
    const std::array<Vec2, 8>& vertices) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_solid_shape(destination, x0, y0, x1, y1,
                                 shape, line, color, vertices);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1;
    (void)shape; (void)line; (void)color; (void)vertices;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_solid_shape_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_dashed_path_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
    float width, const Color& color, std::span<const Vec2> points,
    std::span<const std::uint32_t> run_lengths, bool has_fill,
    std::uint32_t fill_vertex_count, const std::array<Vec2, 8>& fill_vertices) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_dashed_path(destination, x0, y0, x1, y1, width, color,
                                 points, run_lengths, has_fill, fill_vertex_count,
                                 fill_vertices);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)width;
    (void)color; (void)points; (void)run_lengths; (void)has_fill;
    (void)fill_vertex_count; (void)fill_vertices;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_dashed_path_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_path_surface(
    runtime::RenderSurfaceHandle destination,
    std::span<const Vec2> vertices, const Color& color) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_path(destination, vertices, color);
    });
#else
    (void)destination; (void)vertices; (void)color;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_path_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::fill_grid_surface(
    runtime::RenderSurfaceHandle destination,
    std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1,
    const NativeGridFillParams& params) {
#ifdef CHRONON3D_ENABLE_VULKAN
    // Host side mirrors render_grid_background_kernel exactly: colors move
    // to linear light and effective opacity scales every alpha (the kernel's
    // degenerate opacity==0 guard is reproduced before the scaling).
    const float effective_opacity =
        (params.opacity > 0.0f) ? params.opacity : 1.0f;
    const Color bg_linear = params.bg_color.to_linear();
    const Color grid_linear = params.grid_color.to_linear();
    const float bg_alpha = bg_linear.a * effective_opacity;
    const float minor_alpha = grid_linear.a * effective_opacity;
    const Color line_color{grid_linear.r, grid_linear.g, grid_linear.b,
                           minor_alpha};
    const Vec4 shape{5.0f, std::max(params.spacing, 1.0f),
                     static_cast<float>(params.major_every),
                     params.style == 1U ? 1.0f : 0.0f};
    const Vec4 line{params.offset.x, params.offset.y,
                    std::max(params.minor_thickness, 0.0f),
                    std::max(params.major_thickness, 0.0f)};
    std::array<Vec2, 8> vertices{};
    vertices[0] = Vec2{bg_linear.r, bg_linear.g};
    vertices[1] = Vec2{bg_linear.b, bg_alpha};
    vertices[2] = Vec2{params.size.x, params.size.y};
    const float edge_fade = std::isfinite(params.edge_fade)
        ? std::max(params.edge_fade, 0.0f) : 0.0f;
    vertices[3] = Vec2{params.centered ? 1.0f : 0.0f, edge_fade};
    vertices[4] = Vec2{std::isfinite(params.major_glow)
        ? std::max(params.major_glow, 0.0f) : 0.0f, 0.0f};
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->fill_grid(destination, x0, y0, x1, y1,
                          line_color, shape, line, vertices);
    });
#else
    (void)destination; (void)x0; (void)y0; (void)x1; (void)y1; (void)params;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::fill_grid_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    int offset_x, int offset_y, float opacity) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->transform(destination, source, offset_x, offset_y, opacity);
    });
#else
    (void)destination; (void)source; (void)offset_x; (void)offset_y; (void)opacity;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::transform_surface_affine(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const runtime::SurfaceAffineTransform& transform) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->transform_affine(destination, source, transform);
    });
#else
    (void)destination; (void)source; (void)transform;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::transform_surface_affine: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::blur_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source, float radius, bool horizontal) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->blur(destination, source, radius, horizontal);
    });
#else
    (void)destination; (void)source; (void)radius; (void)horizontal;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::blur_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::glow_surfaces(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    float radius, float intensity, const Color& tint) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 4> touched{
        destination, source, scratch_horizontal, scratch_vertical};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->glow(destination, source, scratch_horizontal, scratch_vertical,
                     radius, intensity, tint);
    });
#else
    (void)destination; (void)source; (void)scratch_horizontal;
    (void)scratch_vertical; (void)radius; (void)intensity; (void)tint;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::glow_surfaces: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::bloom_surfaces(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle scratch_horizontal,
    runtime::RenderSurfaceHandle scratch_vertical,
    float threshold, float radius, float intensity) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 4> touched{
        destination, source, scratch_horizontal, scratch_vertical};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->bloom(destination, source, scratch_horizontal, scratch_vertical,
                      threshold, radius, intensity);
    });
#else
    (void)destination; (void)source; (void)scratch_horizontal;
    (void)scratch_vertical; (void)threshold; (void)radius; (void)intensity;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::bloom_surfaces: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::color_adjust_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    float brightness, float contrast, const Color& tint, float tint_amount) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->color_adjust(destination, source, brightness, contrast, tint, tint_amount);
    });
#else
    (void)destination; (void)source; (void)brightness; (void)contrast;
    (void)tint; (void)tint_amount;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::color_adjust_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::color_transform_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const graph::NativeColorTransform& transform) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->color_transform(destination, source, transform);
    });
#else
    (void)destination; (void)source; (void)transform;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::color_transform_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::lighting_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle source,
    const Color& multiplier) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, source};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->lighting(destination, source, multiplier);
    });
#else
    (void)destination; (void)source; (void)multiplier;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::lighting_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::matte_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle target,
    runtime::RenderSurfaceHandle matte,
    bool luma, bool inverted) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 3> touched{destination, target, matte};
    return run_batched_surface_op(touched, {}, [&] {
        m_impl->matte(destination, target, matte, luma, inverted);
    });
#else
    (void)destination; (void)target; (void)matte; (void)luma; (void)inverted;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::matte_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run_surface(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, atlas};
    const auto res = run_batched_surface_op(touched, {}, [&] {
        m_impl->text_run_surface(destination, atlas, glyphs);
    });
    if (!res.ok()) return res;
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
#else
    (void)destination; (void)atlas; (void)glyphs;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run_surface_timed(
    runtime::RenderSurfaceHandle destination,
    runtime::RenderSurfaceHandle atlas,
    std::span<const runtime::GlyphInstance> glyphs,
    float current_frame,
    const Color& highlight_color,
    bool highlight_enabled) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{destination, atlas};
    const auto res = run_batched_surface_op(touched, {}, [&] {
        m_impl->text_run_surface(destination, atlas, glyphs, current_frame,
                                 highlight_color, highlight_enabled);
    });
    if (!res.ok()) return res;
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
#else
    (void)destination; (void)atlas; (void)glyphs; (void)current_frame;
    (void)highlight_color; (void)highlight_enabled;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run_surface_timed: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::GlyphStatic> glyphs,
    std::span<const runtime::TextRunDynamic> runs,
    std::span<const runtime::RenderSurfaceHandle> atlas_pages) {
#ifdef CHRONON3D_ENABLE_VULKAN
    // The atlas pages are resolved through resolve_image() by the op, so they
    // are part of the leased set (read_surfaces = the op's read-only set).
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    const auto res = run_batched_surface_op(touched, atlas_pages, [&] {
        m_impl->draw_text_batch(destination, glyphs, runs, atlas_pages);
    });
    if (!res.ok()) return res;
    return graph::RenderOpResult(graph::RenderOpOutcome{glyphs.size()});
#else
    (void)destination; (void)glyphs; (void)runs; (void)atlas_pages;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_batch: Vulkan support is disabled"));
#endif
}

graph::RenderOpResult VulkanBackend::execute_layer_batch(
    runtime::RenderSurfaceHandle destination,
    std::span<const runtime::LayerInstance> instances,
    std::span<const runtime::RenderSurfaceHandle> resources,
    std::span<const float> transforms,
    std::span<const float> paints) {
    for (const auto& instance : instances) {
        if (instance.blend != BlendMode::Normal && instance.blend != BlendMode::Add &&
            instance.blend != BlendMode::Multiply && instance.blend != BlendMode::Screen &&
            instance.blend != BlendMode::Overlay) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                graph::RenderBackendErrorCode::UnsupportedCapability,
                "VulkanBackend::execute_layer_batch: blend mode is unsupported"));
        }
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    // Every layer source surface is resolved through resolve_image() by the
    // op, so the compiled resource table is part of the leased read set.
    const std::array<runtime::RenderSurfaceHandle, 1> touched{destination};
    return run_batched_surface_op(touched, resources, [&] {
        m_impl->execute_layer_batch(destination, instances, resources,
                                     transforms, paints);
    });
#else
    (void)destination; (void)instances; (void)resources;
    (void)transforms; (void)paints;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::execute_layer_batch: Vulkan support is disabled"));
#endif
}

// DEMOLISHED (P1.4): VulkanBackend::preallocate_plan_surfaces removed —
// native surface materialization is the lazy VulkanSurfaceAuthority path.

void VulkanBackend::composite_legacy_surface(
    Framebuffer& destination, const Framebuffer& source, BlendMode mode,
    const std::optional<raster::BBox>& clip) {
#ifdef CHRONON3D_ENABLE_VULKAN
    const std::array<runtime::RenderSurfaceHandle, 2> touched{
        destination.surface_handle(), source.surface_handle()};
    const auto result = run_batched_surface_op(touched, {}, [&] {
        m_impl->composite(destination.surface_handle(), source.surface_handle(), mode, clip);
    });
    if (!result.ok()) {
        throw std::runtime_error(result.error().message);
    }
#else
    (void)destination;
    (void)source;
    (void)mode;
    (void)clip;
    unsupported("composite_layer");
#endif
}

std::unique_ptr<graph::RenderBackend> make_vulkan_backend(
    std::uint32_t device_index) {
    return std::make_unique<VulkanBackend>(device_index);
}

} // namespace chronon3d::backends::vulkan
