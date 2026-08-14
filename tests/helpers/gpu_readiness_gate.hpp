#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// gpu_readiness_gate.hpp — certifies the 7-point "first real Vulkan pipeline"
// readiness checklist from the benchmark plan:
//
//   1. output visually correct          (non-blank, matches the CPU reference)
//   2. CPU/GPU parity acceptable        (within PixelTolerance)
//   3. 1 submit per frame               (one vkQueueSubmit for the whole plan)
//   4. no upload between passes         (uploads only for inputs, never in-pass)
//   5. no intermediate readback         (readback only for the final output)
//   6. physical < logical surfaces      (lifetime-disjoint surfaces alias)
//   7. GPU faster than CPU              (wall-clock, scalar reference vs GPU)
//
// certify_gpu_readiness() runs one CommandPlan on the Vulkan backend and on
// the scalar CPU reference from cpu_gpu_parity.hpp, then reports a verdict
// per point plus the measured timings, speedup, and pixel comparison.  The
// caller creates the surfaces + uploads the input pixels first (same contract
// as run_cpu_gpu_parity).
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/render_graph/checkbackend.hpp>
#include <chronon3d/render_graph/executor/command_plan_executor.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include "cpu_gpu_parity.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d::test {

struct GpuReadinessGate {
    bool output_correct{false};        // 1. output visually correct (non-blank)
    bool parity_ok{false};             // 2. CPU/GPU parity acceptable
    bool one_submit_per_frame{false};  // 3. 1 vkQueueSubmit per frame
    bool no_inter_pass_upload{false};  // 4. no upload between passes
    bool no_intermediate_readback{false};  // 5. no readback between passes
    bool physical_lt_logical{false};   // 6. physical < logical surfaces
    bool gpu_faster{false};            // 7. GPU faster than CPU

    double cpu_ms{0.0};
    double gpu_ms{0.0};
    double speedup{0.0};  // cpu_ms / gpu_ms; > 1 means the GPU was faster
    std::size_t logical_surfaces{0};
    std::size_t physical_surfaces{0};
    graph::PixelCompareResult pixels{};

    [[nodiscard]] int passed() const noexcept {
        int count = 0;
        count += output_correct ? 1 : 0;
        count += parity_ok ? 1 : 0;
        count += one_submit_per_frame ? 1 : 0;
        count += no_inter_pass_upload ? 1 : 0;
        count += no_intermediate_readback ? 1 : 0;
        count += physical_lt_logical ? 1 : 0;
        count += gpu_faster ? 1 : 0;
        return count;
    }
    static constexpr int kTotalPoints = 7;
};

inline GpuReadinessGate certify_gpu_readiness(
    chronon3d::backends::vulkan::VulkanBackend& backend,
    runtime::RenderSurfaceRegistry& registry,
    const runtime::CommandPlan& plan,
    const std::unordered_map<runtime::RenderSurfaceHandle, runtime::SurfaceDesc>& surfaces,
    const std::unordered_map<runtime::RenderSurfaceHandle, std::vector<float>>& input_pixels,
    runtime::RenderSurfaceHandle output_handle,
    const graph::PixelTolerance& tolerance = {}) {
    GpuReadinessGate gate;
    gate.logical_surfaces =
        plan.resources.allocations.empty() ? surfaces.size()
                                           : plan.resources.allocations.size();

    const auto before = backend.stats();

    // 1–2. CPU reference (timed) → oracle pixels.
    const auto t0 = std::chrono::steady_clock::now();
    const auto reference = execute_command_plan_cpu(plan, surfaces, input_pixels, output_handle);
    gate.cpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (reference.empty()) {
        return gate;
    }

    // GPU: execute the plan WITHOUT the final readback yet.
    const auto g0 = std::chrono::steady_clock::now();
    const bool executed = runtime::execute_command_plan(backend, registry, plan);
    gate.gpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0).count();
    if (!executed) {
        return gate;
    }

    const auto after_execute = backend.stats();
    gate.one_submit_per_frame = after_execute.submissions == before.submissions + 1;
    gate.no_inter_pass_upload = after_execute.upload_calls == before.upload_calls;
    gate.no_intermediate_readback = after_execute.readback_calls == before.readback_calls;

    // Final readback — the only readback the pipeline is allowed to perform.
    const auto out_it = surfaces.find(output_handle);
    if (out_it == surfaces.end()) {
        return gate;
    }
    std::vector<float> gpu_pixels(
        static_cast<std::size_t>(out_it->second.width) * out_it->second.height * 4, 0.0f);
    const auto g1 = std::chrono::steady_clock::now();
    const bool downloaded = backend.download_surface(output_handle, gpu_pixels).ok();
    gate.gpu_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g1).count();
    if (!downloaded) {
        return gate;
    }

    // 6. Aliasing proof: lifetime-disjoint surfaces share physical slots.
    //    The resource planner's allocation is the pipeline's contract: fewer
    //    physical slots than logical surfaces.  The backend may add a few
    //    safety-diverted slots for pre-uploaded (pinned) inputs, so the
    //    canonical "physical < logical" measure is the plan's slot count.
    std::unordered_set<std::size_t> plan_slots;
    for (const auto& allocation : plan.resources.allocations) {
        if (allocation.physical_slot != std::numeric_limits<std::size_t>::max()) {
            plan_slots.insert(allocation.physical_slot);
        }
    }
    gate.physical_surfaces = plan_slots.size();
    gate.physical_lt_logical = gate.physical_surfaces < gate.logical_surfaces;

    // 1–2. Output is non-blank and matches the CPU oracle within tolerance.
    gate.output_correct = std::any_of(gpu_pixels.begin(), gpu_pixels.end(),
                                      [](float v) { return v != 0.0f; });
    gate.pixels = graph::compare_pixels(reference, gpu_pixels, tolerance);
    gate.parity_ok = gate.pixels.matched;

    // 7. Speedup.
    if (gate.gpu_ms > 0.0) {
        gate.speedup = gate.cpu_ms / gate.gpu_ms;
    }
    gate.gpu_faster = gate.gpu_ms < gate.cpu_ms;

    return gate;
}

} // namespace chronon3d::test
