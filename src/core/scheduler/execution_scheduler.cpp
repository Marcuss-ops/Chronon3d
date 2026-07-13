// ---------------------------------------------------------------------------
// execution_scheduler.cpp
//
// Single owner of the process-wide tbb::task_arena.
// Implements SchedulerMode dispatch + optional thread pinning (legacy opt-in).
// ---------------------------------------------------------------------------

#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scheduler/scheduler_mode.hpp>
#include <chronon3d/core/config.hpp>

#include <algorithm>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace chronon3d {

namespace {

// ── pin_thread_to_core — opt-in helper.  Pin the calling thread to a
// single CPU core for deterministic single-threaded benchmarking.
// Inlined from the legacy executor.cpp implementation; lives here because
// pin semantics are a SCHEDULER concern, not an EXECUTOR concern.
void pin_thread_to_core(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

} // namespace

namespace {

// Single-construct helper for the underlying tbb::task_arena.  Avoids the
// default-construct + assign pattern (which would let the default arena
// observe scheduler-side work it was never meant to).
tbb::task_arena make_arena(SchedulerMode mode, int worker_count) {
    switch (mode) {
        case SchedulerMode::Sequential:
            // arena(1) caps nested tbb::parallel_for inside, which is the
            // property the user wants for Sequential (deterministic + reproducible).
            return tbb::task_arena{1};
        case SchedulerMode::TbbAutomatic:
            // Default-constructed arena = automatic slot count.
            return tbb::task_arena{};
        case SchedulerMode::TbbFixed: {
            int slots = worker_count;
            if (slots <= 0) {
                slots = static_cast<int>(
                    std::max(1u, std::thread::hardware_concurrency()));
            }
            return tbb::task_arena{slots};
        }
    }
    return tbb::task_arena{};
}

} // namespace

// ── Lifecycle ──────────────────────────────────────────────────────────

ExecutionScheduler::ExecutionScheduler(
    SchedulerMode mode, int worker_count, bool pin_calling_thread)
    : m_mode(mode)
    , m_arena(make_arena(mode, worker_count))
{
    if (pin_calling_thread) {
        pin_thread_to_core(0);
    }
}

ExecutionScheduler make_execution_scheduler(ExecutionSchedulerConfig cfg) {
    return ExecutionScheduler{cfg.mode, cfg.worker_count, cfg.pin_calling_thread};
}

ExecutionScheduler make_execution_scheduler(const Config& cfg) {
    return make_execution_scheduler(ExecutionSchedulerConfig{
        .mode               = cfg.scheduler().mode(),
        .worker_count       = cfg.cpu_budget().render_threads,
        .pin_calling_thread = cfg.scheduler().pin_calling_thread(),
    });
}

} // namespace chronon3d
