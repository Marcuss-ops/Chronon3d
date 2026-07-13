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
// Defaults are chosen per machine class so that a 16-thread desktop
// gets the canonical 12/2/2 split requested by the render pipeline.

#include <cstddef>
#include <string_view>

namespace chronon3d {

/// Process-wide CPU budget.
struct CpuBudget {
    int render_threads{0};
    int decode_threads{0};
    int encode_threads{0};

    [[nodiscard]] int total() const noexcept {
        return render_threads + decode_threads + encode_threads;
    }

    [[nodiscard]] bool empty() const noexcept {
        return render_threads == 0 && decode_threads == 0 && encode_threads == 0;
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
/// of logical cores.  Guarantees at least one thread per pool whenever
/// the total is >= 3, and never returns more than total_threads.
[[nodiscard]] CpuBudget cpu_budget_for_class(CpuMachineClass c, int total_threads) noexcept;

/// Build a budget from environment variables.
///
/// Reads:
///   CHRONON3D_CPU_RENDER_THREADS
///   CHRONON3D_CPU_DECODE_THREADS
///   CHRONON3D_CPU_ENCODE_THREADS
///   CHRONON3D_CPU_MACHINE_CLASS  (desktop|laptop|server|embedded)
///
/// Explicit per-pool values override the preset.  If no env var is set,
/// the desktop preset is used.
[[nodiscard]] CpuBudget cpu_budget_from_environment(int total_threads);

} // namespace chronon3d
