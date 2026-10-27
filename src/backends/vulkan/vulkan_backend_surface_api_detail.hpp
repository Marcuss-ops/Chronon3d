#pragma once

// vulkan_backend_surface_api_detail.hpp — out-of-class definition of the
// VulkanBackend::run_batched_surface_op member template (issue #51). Kept in a
// private detail header so the surface-API adapter units can be split below
// 500 lines while the funnel stays single-sourced.

#ifdef CHRONON3D_ENABLE_VULKAN
#include "vulkan_backend_impl.hpp"
#endif

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

} // namespace chronon3d::backends::vulkan
