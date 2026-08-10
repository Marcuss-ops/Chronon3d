// ── command_benchmark_machine — `chronon benchmark-machine` ──────────────
//
// Certifies the host machine for Chronon3D: prints the canonical
// "Chronon CPU" banner (CPU model, logical CPUs, NUMA nodes, SIMD
// supported/selected, TBB worker budget) so the engine's parallelism
// and SIMD decisions are VERIFIED at runtime — never assumed from the
// build flags.
//
// The SIMD columns come from `simd::detect_cpu_capabilities()`, the
// canonical per-host detection (cpuid/hwcap, honoring
// `CHRONON3D_FORCE_CPU_ISA` for deterministic repro). The TBB worker
// count is the actual `max_allowed_parallelism` cap installed by
// `main.cpp` (CpuBudget.render_threads).

#include "../../commands.hpp"
#include "../../cli_context.hpp"

#include <chronon3d/simd/cpu_isa.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace chronon3d {
namespace cli {

namespace {

// Read the first CPU model name line from /proc/cpuinfo (x86 "model
// name", aarch64 "Model name"). Returns "unknown" when unavailable.
std::string read_cpu_model_name() {
    std::ifstream in("/proc/cpuinfo");
    if (!in.is_open()) {
        return "unknown";
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string_view view(line);
        const std::size_t colon = view.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view key = view.substr(0, colon);
        const std::size_t key_end = key.find_last_not_of(" \t");
        if (key_end == std::string_view::npos) {
            continue;
        }
        const std::string_view key_trimmed = key.substr(0, key_end + 1);
        if (key_trimmed != "model name" && key_trimmed != "Model name") {
            continue;
        }
        std::string value(view.substr(colon + 1));
        const std::size_t first = value.find_first_not_of(" \t");
        return first == std::string::npos ? "unknown" : value.substr(first);
    }
    return "unknown";
}

// Count logical NUMA nodes from /sys/devices/system/node/possible
// (e.g. "0-3,8-11" → 8, "0" → 1). Falls back to 1 on any read failure.
int count_numa_nodes() {
    std::ifstream in("/sys/devices/system/node/possible");
    std::string text;
    if (!(in >> text) || text.empty()) {
        return 1;
    }
    int nodes = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t comma = text.find(',', pos);
        const std::string part = text.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (part.empty()) {
            if (comma == std::string::npos) break;
            pos = comma + 1;
            continue;
        }
        // Reject malformed entries that aren't pure digit or digit-dash-digit.
        if (part.find_first_not_of("0123456789-") != std::string::npos) {
            if (comma == std::string::npos) break;
            pos = comma + 1;
            continue;
        }
        const std::size_t dash = part.find('-');
        if (dash == std::string::npos) {
            if (!part.empty()) {
                ++nodes;
            }
        } else {
            const int lo = std::atoi(part.substr(0, dash).c_str());
            const int hi = std::atoi(part.substr(dash + 1).c_str());
            if (hi >= lo) {
                nodes += (hi - lo + 1);
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return nodes > 0 ? nodes : 1;
}

// "SIMD supported" column from the canonical capability snapshot
// (uppercase canonical ISA names, space-separated).
std::string simd_supported_list(const simd::CpuCapabilities& caps) {
    std::string out;
    if (caps.has_sse42) {
        out += " SSE42";
    }
    if (caps.has_avx2) {
        out += " AVX2";
    }
    if (caps.has_avx512) {
        out += " AVX512";
    }
    if (caps.has_neon) {
        out += " NEON";
    }
    return out.empty() ? "scalar" : out.substr(1);
}

} // namespace

int command_benchmark_machine(const CliContext& ctx) {
    // SIMD selection is VERIFIED via runtime detection
    // (detect_cpu_capabilities) — never assumed from build flags.
    //
    // NOTE: "SIMD selected" reports the `highest_isa` from the canonical
    // capability detection (the input to the kernel resolver). On AVX512-
    // capable hosts the resolver still routes to kScalarSet (kAvx512Set is
    // a documented forward-point, TICKET-SIMD-REGISTRY-CANONICAL), so the
    // banner may overstate the actual kernel dispatch until that lands.
    // On this AVX2 host the resolver's kAvx2Set is wired and consistent.
    const simd::CpuCapabilities caps = simd::detect_cpu_capabilities();

    std::cout << "Chronon CPU\n";
    std::cout << "--------------------------------\n";
    std::cout << "CPU             " << read_cpu_model_name() << '\n';
    std::cout << "Logical CPUs    " << std::thread::hardware_concurrency() << '\n';
    std::cout << "SIMD supported  " << simd_supported_list(caps) << '\n';
    std::cout << "SIMD selected   " << simd::cpu_isa_name(caps.highest_isa) << '\n';
    std::cout << "TBB workers     " << ctx.cpu_budget.render_threads << '\n';
    std::cout << "NUMA nodes      " << count_numa_nodes() << '\n';
    if (const char* forced = std::getenv("CHRONON3D_FORCE_CPU_ISA")) {
        std::cout << "SIMD override   CHRONON3D_FORCE_CPU_ISA=" << forced << '\n';
    }
    std::cout << "--------------------------------\n";
    return 0;
}

} // namespace cli
} // namespace chronon3d
