#include "daemon_service_job_admission.hpp"

#include <chrono>

namespace chronon3d::cli {

RenderPolicyDecision decide_render_job_admission(
    bool parallel_disjoint,
    std::uint64_t estimated_vram_bytes) {
    return decide_render_admission(RenderPolicyInput{
        .job_class = parallel_disjoint
            ? RenderJobClass::ChunkedDisjointSamePlan
            : kDefaultRenderJobClass,
        .job_peak_vram_bytes = estimated_vram_bytes,
        .device_total_vram_bytes = kReferenceDeviceVramBytes,
        .device_free_vram_bytes = kReferenceDeviceVramBytes,
    });
}

RenderJobAdmissionContext::RenderJobAdmissionContext(
    bool parallel_disjoint,
    std::uint64_t estimated_vram_bytes,
    std::shared_mutex& render_job_mutex,
    std::mutex& admission_mutex,
    std::condition_variable& admission_cv,
    std::atomic<bool>& running,
    runtime::DeviceScheduler& device_scheduler,
    const runtime::DeviceSelectionRequirements& requirements)
    : m_admission_cv(&admission_cv), m_running(&running) {
    m_decision = decide_render_job_admission(
        parallel_disjoint, estimated_vram_bytes);

    if (m_decision.admission == RenderAdmission::ParallelDisjoint) {
        m_shared_lock = std::shared_lock<std::shared_mutex>(render_job_mutex);
    } else {
        m_exclusive_lock = std::unique_lock<std::shared_mutex>(render_job_mutex);
    }

    m_admission_lock = std::unique_lock<std::mutex>(admission_mutex);
    const auto admission_started = std::chrono::steady_clock::now();
    const bool admitted = m_admission_cv->wait_for(
        m_admission_lock, std::chrono::seconds(300), [&]() {
            if (!m_running->load(std::memory_order_acquire)) return true;
            auto reservation = device_scheduler.reserve(requirements);
            if (reservation) {
                m_reservation = std::move(*reservation);
                return true;
            }
            return false;
        });

    m_wait_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - admission_started).count();
    if (!m_running->load(std::memory_order_acquire)) {
        m_failure_reason = "daemon is shutting down";
        m_admission_lock.unlock();
        return;
    }
    if (!admitted || !m_reservation.valid()) {
        m_admission_lock.unlock();
        return;
    }
    m_admitted = true;
    // The scheduler reservation is now owned by this context; do not hold the
    // admission mutex while the renderer runs. The mutex only protects the
    // reserve attempt and the condition-variable predicate.
    m_admission_lock.unlock();
}

RenderJobAdmissionContext::~RenderJobAdmissionContext() {
    // Release the reservation before waking waiters. DeviceReservation's
    // destructor returns credits; waking after that makes the predicate see
    // the new scheduler state and avoids a needless failed wakeup.
    m_reservation.release();
    if (m_admission_cv != nullptr) m_admission_cv->notify_all();
}

runtime::DeviceReservation RenderJobAdmissionContext::take_reservation() noexcept {
    return std::move(m_reservation);
}

} // namespace chronon3d::cli
