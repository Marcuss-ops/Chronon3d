#pragma once

// ---------------------------------------------------------------------------
// core/scheduler/execution_scheduler.hpp
//
// Single owner of the tbb::task_arena for the whole engine.  Replaces the
// `tbb::task_arena m_arena` member of `GraphExecutor` (audit §9.2 — PR-B).
//
// Every rendering entry point (GraphExecutor::execute, tile path,
// PrecompNode::execute, future checksum harnesses) either receives an
// `ExecutionScheduler&` parameter or reads it from
// `ctx.services.scheduler`.
//
// Concurrency contract:
//   * Sequential   → arena(1)  → caps nested tbb::parallel_for
//   * TbbAutomatic → arena(default)  → TBB infers local slot count
//   * TbbFixed(N)  → arena(N) → reproducible for benchmarks
//
// Thread pinning (legacy opt-in via CHRONON3D_PIN_MAIN_THREAD=1) is now
// owned by ExecutionScheduler::init.  pthread/sched/windows includes move
// from executor.cpp to execution_scheduler.cpp.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <tbb/task_arena.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <type_traits>
#include <utility>

#include <chronon3d/core/scheduler/scheduler_mode.hpp>
#include <chronon3d/core/scheduler/for_each_tile.hpp>
#include <chronon3d/raster/bbox.hpp>

namespace chronon3d {

class Config;

/// Parsed settings: env → Config::SchedulerConfig → ExecutionSchedulerConfig.
struct ExecutionSchedulerConfig {
    SchedulerMode mode{SchedulerMode::TbbAutomatic};
    /// For TbbFixed: number of arena slots.  0 means "use hardware_concurrency()".
    /// Negative values are spdlog::warn-rejected in the env parser and defaulted
    /// to 0 (hardware_concurrency fallback at scheduler construction).
    int worker_count{0};
    /// If true, pin the calling thread to core 0 at scheduler construction.
    bool pin_calling_thread{false};
};

// Factory overloads
class ExecutionScheduler;
ExecutionScheduler make_execution_scheduler(ExecutionSchedulerConfig cfg);
class ExecutionScheduler;
ExecutionScheduler make_execution_scheduler(const Config& cfg);

/// Authoritative thread-pool for the engine.  Owns exactly one
/// `tbb::task_arena`.  Construction is the only place where thread
/// pinning may occur.  All public methods are constexpr-able except
/// `concurrency()` (depends on TBB internals).
class ExecutionScheduler {
public:
    /// Direct construction (used by tests + factories).  Avoid in production
    /// code paths; prefer `make_execution_scheduler(Config&)`.
    explicit ExecutionScheduler(
        SchedulerMode mode = SchedulerMode::TbbAutomatic,
        int worker_count = 0,
        bool pin_calling_thread = false
    );

    /// Run the callable inside the scheduler arena.
    /// Signature mirrors tbb::task_arena::execute().
    /// NOTE: NOT const — `tbb::task_arena::execute()` mutates arena state.
    template <typename Fn>
    decltype(auto) run(Fn&& fn) {
        return m_arena.execute(std::forward<Fn>(fn));
    }

    /// Parallel index loop, routed through the scheduler arena.
    /// Sequential mode runs serially (still inside arena(1) so nested
    /// tbb::parallel_for calls cannot escape into siblings).
    /// NOTE: NOT const — `tbb::task_arena::execute()` mutates arena state.
    template <typename Fn>
    void for_each_index(std::size_t count, Fn&& fn);

    /// Parallel tile-loop dispatched through the scheduler arena
    /// (F4.3 — TICKET-KERNEL-TILING-V1).  Routes through `m_arena`
    /// so Sequential mode caps nested parallel_for calls; the actual
    /// tiling math lives in the canonical free function
    /// `chronon3d::scheduler::for_each_tile` (see
    /// `include/chronon3d/core/scheduler/for_each_tile.hpp`).
    ///
    /// Tile-coverage invariant: union of `body(Tile)` invocations
    /// covers `bbox` exactly (no overlap, no gaps), edge tiles may be
    /// smaller than `ts.width`/`ts.height` at right/bottom.
    template <typename Fn>
    void for_each_tile(const raster::BBox& bbox, scheduler::TileSize ts, Fn&& fn);

    /// Maximum concurrency of the underlying task arena (== arena size).
    [[nodiscard]] int concurrency() const noexcept {
        return m_arena.max_concurrency();
    }

    /// Effective worker slots for TbbFixed (== concurrency()).  The header
    /// has no redundant `m_worker_count` field any more: the underlying
    /// `tbb::task_arena.max_concurrency()` is the single source of truth
    /// (review round-3 cleanup).
    [[nodiscard]] int worker_count() const noexcept {
        return m_arena.max_concurrency();
    }

    [[nodiscard]] SchedulerMode mode() const noexcept { return m_mode; }

private:
    SchedulerMode m_mode{SchedulerMode::TbbAutomatic};
    tbb::task_arena m_arena;
};

// ── Free helpers (env parsing + diagnostics) ───────────────────────────
std::string_view scheduler_mode_name(SchedulerMode mode) noexcept;
bool parse_scheduler_mode(std::string_view text, SchedulerMode& out) noexcept;

// ── Template out-of-line definitions ───────────────────────────────────
template <typename Fn>
inline void ExecutionScheduler::for_each_index(std::size_t count, Fn&& fn) {
    // PR-B review (round 3): Sequential branch must ALSO be wrapped in
    // `m_arena.execute(...)`, even though the body is a plain serial
    // loop.  Otherwise nested `tbb::parallel_for` calls inside the per-
    // node kernels (blur, composite, blend2d bridge, grid background)
    // escape the bounding arena(1) and re-enter the global default
    // arena — which is exactly what the Sequential mode was created to
    // prevent for deterministic tests (audit §9.10 / spec §10).
    m_arena.execute([&]() {
        if (m_mode == SchedulerMode::Sequential) {
            for (std::size_t i = 0; i < count; ++i) {
                fn(i);
            }
            return;
        }
        tbb::parallel_for(
            tbb::blocked_range<std::size_t>(0, count, 1),
            [&](const tbb::blocked_range<std::size_t>& range) {
                for (std::size_t i = range.begin(); i != range.end(); ++i) {
                    fn(i);
                }
            }
        );
    });
}

// ── Tile-tiling dispatch (F4.3 — TICKET-KERNEL-TILING-V1) ─────────────
//
// Routed via m_arena.execute so Sequential mode caps nested parallel_for
// (mirroring the existing for_each_index pattern).  The free function
// `chronon3d::scheduler::for_each_tile` (in for_each_tile.hpp) owns the
// tile-coverage math + parallel_for_tracked telemetry recording.
template <typename Fn>
inline void ExecutionScheduler::for_each_tile(
    const raster::BBox& bbox, scheduler::TileSize ts, Fn&& fn
) {
    m_arena.execute([&]() {
        chronon3d::scheduler::for_each_tile(bbox, ts, std::forward<Fn>(fn));
    });
}

} // namespace chronon3d
