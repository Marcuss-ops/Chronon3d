#pragma once

// ── cpu_budget — unified thread budget for render/decode/encode ───────
//
// Replaces ad-hoc thread limits (tbb::global_control, WritePool,
// encoder "threads=auto", etc.) with a single process-wide budget.
//
// The budget is split into three pools:
//   * render — TBB task arena / ExecutionScheduler
//   * decode — future video decoder threads
//   * encode — encoder internal threads (x264) + writer thread
//
// F4.2 (TICKET-CPU-BUDGET-UNIFIED-V1) extends the prior 3-field shape
// with:
//   * `total_threads` (stored) — the canonical process-wide budget
//     anchor; per-pool shares are derived FROM this via the factory.
//   * `*_max_inflight` — bounded per-pool frame queue depth (decoded
//     simultaneously). Decode: 2-4; render: 2; encode: 2-4 frame. The
//     static-mode default uses the lower bound; class-conditional logic
//     in `cpu_budget_for_class` raises it for high-throughput classes
//     (Server/Desktop).
//   * `BudgetMode { Static, Dynamic }` — static allocation (default;
//     current behavior) vs dynamic-with-backlog (forward-pointed impl
//     per TICKET-CPU-BUDGET-UNIFIED-V1 §Forward-points).
//
// Defaults are chosen per machine class so that a 16-thread desktop
// gets the canonical 12/2/2 thread split + 2/2/4 max-inflight ceilings
// per user-spec verbatim example.

#include <cstddef>
#include <string_view>

namespace chronon3d {

/// CPU budget allocation mode.
///
/// `Static` — per-pool thread + max-inflight fields are fixed at
/// factory construction. No runtime rebalancing. Matches the original
/// CpuBudget behavior.
///
/// `Dynamic` — forward-pointed to TICKET-CPU-BUDGET-UNIFIED-V1 §Step-2;
/// idle pool threads can be temporarily loaned to a busy pool when its
/// backlog exceeds a threshold. Full implementation deferred.
enum class BudgetMode {
    Static,
    Dynamic,
};

/// Name of a budget mode (for diagnostics / env parsing).
[[nodiscard]] std::string_view cpu_budget_mode_name(BudgetMode m) noexcept;

/// Parse a budget mode name. Returns false for unknown values.
[[nodiscard]] bool parse_cpu_budget_mode(std::string_view text, BudgetMode& out) noexcept;

/// Process-wide CPU budget.
///
/// F4.2 shape (per user-spec verbatim):
///   1. `total_threads` — stored field (canonical process-wide anchor)
///   2. `render_threads` — share bound to the render pool
///   3. `decode_threads` — share bound to the decode pool
///   4. `encode_threads` — share bound to the encode pool
/// + bounded queue depths (`*_max_inflight`, 3 fields, default 2×rank)
/// + `mode` (default `Static`).
///
/// Back-compat: `total()` method preserved (returns `total_threads`).
struct CpuBudget {
    int total_threads{0};
    int render_threads{0};
    int decode_threads{0};
    int encode_threads{0};

    // Bounded frame-queue depths (per-pool max frames in flight at once).
    // Default 2 each — class-conditional logic in factory raises them
    // for high-throughput classes.
    int render_max_inflight{2};
    int decode_max_inflight{2};
    int encode_max_inflight{2};

    BudgetMode mode{BudgetMode::Static};

    [[nodiscard]] int total() const noexcept {
        return total_threads;
    }

    [[nodiscard]] bool empty() const noexcept {
        return total_threads == 0;
    }
};

/// Preset machine classes used to derive a default budget.
enum class CpuMachineClass {
    Desktop,
    Laptop,
    Server,
    Embedded,
};

/// Name of a machine class (for diagnostics / env parsing).
[[nodiscard]] std::string_view cpu_machine_class_name(CpuMachineClass c) noexcept;

/// Parse a machine-class name. Returns false for unknown values.
[[nodiscard]] bool parse_cpu_machine_class(std::string_view text, CpuMachineClass& out) noexcept;

/// Compute a default budget for a machine class given the total number
/// of logical cores.
///
/// Per-pool thread shares honor AGENTS.md §regole "no condivisione
/// thread excess". Guarantees:
///   - at least one thread per pool whenever the total is >= 3,
///   - never returns more than `total_threads`,
///   - `total_threads` field reflects the canonical input anchor.
///
/// `mode` defaults to `BudgetMode::Static`.
/// `*_max_inflight` fields use class-conditional defaults:
///   - Desktop: render=2, decode=2, encode=4
///   - Laptop:  render=2, decode=2, encode=2
///   - Server:  render=2, decode=4, encode=4
///   - Embedded: render=2, decode=1, encode=1
[[nodiscard]] CpuBudget cpu_budget_for_class(
    CpuMachineClass c, int total_threads) noexcept;

/// Build a budget from environment variables.
///
/// Reads:
///   `CHRONON3D_CPU_RENDER_THREADS`
///   `CHRONON3D_CPU_DECODE_THREADS`
///   `CHRONON3D_CPU_ENCODE_THREADS`
///   `CHRONON3D_CPU_MACHINE_CLASS` (desktop|laptop|server|embedded)
///   `CHRONON3D_CPU_BUDGET_MODE` (static|dynamic, default static)
///
/// Explicit per-pool values override the preset.  If no env var is set,
/// the desktop preset is used.  `total_threads` field is set to the
/// input `total_threads` argument (caller-supplied OS-core count).
[[nodiscard]] CpuBudget cpu_budget_from_environment(int total_threads);

} // namespace chronon3d
