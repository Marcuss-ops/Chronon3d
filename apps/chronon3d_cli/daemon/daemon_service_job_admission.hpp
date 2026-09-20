#pragma once

#include "daemon_render_concurrency.hpp"

#include <chronon3d/runtime/device_scheduler.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace chronon3d::cli {

/// Resolve the execution-domain verdict for one normalized job without owning
/// transport state, mutexes or scheduler reservations.
[[nodiscard]] RenderPolicyDecision decide_render_job_admission(
    bool parallel_disjoint,
    std::uint64_t estimated_vram_bytes);

/// RAII owner for one admitted RENDER_JOB.
///
/// Construction acquires the policy-selected shared/exclusive execution lock,
/// waits for scheduler capacity, and stores the reservation. Destruction
/// releases both the reservation and lock, including exception paths. The
/// context deliberately does not own DaemonService state; all synchronization
/// primitives remain owned by the service and are passed by reference.
class RenderJobAdmissionContext final {
public:
    RenderJobAdmissionContext(
        bool parallel_disjoint,
        std::uint64_t estimated_vram_bytes,
        std::shared_mutex& render_job_mutex,
        std::mutex& admission_mutex,
        std::condition_variable& admission_cv,
        std::atomic<bool>& running,
        runtime::DeviceScheduler& device_scheduler,
        const runtime::DeviceSelectionRequirements& requirements);

    RenderJobAdmissionContext(const RenderJobAdmissionContext&) = delete;
    RenderJobAdmissionContext& operator=(const RenderJobAdmissionContext&) = delete;
    RenderJobAdmissionContext(RenderJobAdmissionContext&&) = delete;
    RenderJobAdmissionContext& operator=(RenderJobAdmissionContext&&) = delete;
    ~RenderJobAdmissionContext();

    [[nodiscard]] bool admitted() const noexcept { return m_admitted; }
    [[nodiscard]] const char* failure_reason() const noexcept {
        return m_failure_reason;
    }
    [[nodiscard]] const RenderPolicyDecision& decision() const noexcept {
        return m_decision;
    }
    [[nodiscard]] runtime::DeviceReservation take_reservation() noexcept;

private:
    std::unique_lock<std::shared_mutex> m_exclusive_lock;
    std::shared_lock<std::shared_mutex> m_shared_lock;
    std::unique_lock<std::mutex> m_admission_lock;
    runtime::DeviceReservation m_reservation;
    std::condition_variable* m_admission_cv{nullptr};
    std::atomic<bool>* m_running{nullptr};
    bool m_admitted{false};
    const char* m_failure_reason{"device resources saturated or admission timed out"};
    RenderPolicyDecision m_decision{};
};

} // namespace chronon3d::cli
