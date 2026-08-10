// ── cpu_isa — canonical CPU ISA detection implementation ───────────────
//
// Implements the four public symbols declared in
// `include/chronon3d/simd/cpu_isa.hpp` (the single source of truth for
// the 5-ISA classification + capability snapshot):
//   - `cpu_isa_name`            (stable lowercase wire contract)
//   - `parse_cpu_isa`           (case-insensitive inverse, env-parseable)
//   - `CpuCapabilities::supports` (ISA containment query)
//   - `detect_cpu_capabilities` (host cpuid/hwcap probe, env-overridable)
//
// This closes the scaffold forward-point of TICKET-SIMD-REGISTRY-CANONICAL
// (and ADR-025 §ALT-A): the ABI was declared + consumed by
// `tests/simd/*` but the implementation had not landed. Detection is
// one-time, immutable per program, and free of global mutable state —
// the result lives in the returned value (no singleton/cache).
//
// Per AGENTS.md §regole "no espansione API non necessaria": NO new
// public symbol is added — this file only defines what the public
// header already declares.

#include <chronon3d/simd/cpu_isa.hpp>

#include <cstdlib>
#include <cstddef>
#include <string_view>

#if (defined(__x86_64__) || defined(__i386__)) && \
    (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>   // __builtin_cpu_init() declaration (GCC/Clang)
#endif

namespace chronon3d {
namespace simd {

namespace {

// Canonical names, in CpuIsa enumerator order (see cpu_isa.hpp contract:
// "do not re-order enumerators without updating this fn").
constexpr std::string_view kIsaNames[] = {
    "scalar", "sse42", "avx2", "avx512", "neon",
};
constexpr std::size_t kIsaCount = 5;

// Hoisted to a file-local helper (not re-created per character).
constexpr char ascii_to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_to_lower(a[i]) != ascii_to_lower(b[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

const char* cpu_isa_name(CpuIsa isa) noexcept {
    const auto index = static_cast<std::size_t>(isa);
    return index < kIsaCount ? kIsaNames[index].data() : "scalar";
}

bool parse_cpu_isa(const char* text, CpuIsa& out) noexcept {
    if (text == nullptr) {
        return false;
    }
    const std::string_view view(text);
    for (std::size_t i = 0; i < kIsaCount; ++i) {
        if (iequals(view, kIsaNames[i])) {
            out = static_cast<CpuIsa>(i);
            return true;
        }
    }
    return false;
}

bool CpuCapabilities::supports(CpuIsa isa) const noexcept {
    switch (isa) {
        case CpuIsa::Scalar: return true;   // reference always available
        case CpuIsa::SSE42:  return has_sse42;
        case CpuIsa::AVX2:   return has_avx2;
        case CpuIsa::AVX512: return has_avx512;
        case CpuIsa::NEON:   return has_neon;
    }
    return false;
}

CpuCapabilities detect_cpu_capabilities() noexcept {
    CpuCapabilities caps{};

    // Deterministic override for tests / post-rot repro (ADR-020 +
    // TICKET-SIMD-REGISTRY-CANONICAL): when `CHRONON3D_FORCE_CPU_ISA` is
    // set to a valid name, the returned snapshot is exactly the forced
    // ISA plus every weaker ISA it implies — no host probe runs. An
    // invalid value falls through to host detection (never crashes).
    if (const char* forced = std::getenv("CHRONON3D_FORCE_CPU_ISA")) {
        CpuIsa forced_isa{};
        if (parse_cpu_isa(forced, forced_isa)) {
            switch (forced_isa) {
                case CpuIsa::Scalar:
                    return caps;   // all flags false → scalar route
                case CpuIsa::SSE42:
                    caps.has_sse42 = true;
                    caps.highest_isa = CpuIsa::SSE42;
                    return caps;
                case CpuIsa::AVX2:
                    caps.has_sse42 = true;
                    caps.has_avx2 = true;
                    caps.highest_isa = CpuIsa::AVX2;
                    return caps;
                case CpuIsa::AVX512:
                    caps.has_sse42 = true;
                    caps.has_avx2 = true;
                    caps.has_avx512 = true;
                    caps.highest_isa = CpuIsa::AVX512;
                    return caps;
                case CpuIsa::NEON:
                    caps.has_neon = true;
                    caps.highest_isa = CpuIsa::NEON;
                    return caps;
            }
        }
    }

    // Host probe. `__builtin_cpu_supports` is a compiler builtin
    // (runtime CPUID query, no inline assembly); `__builtin_cpu_init()`
    // primes the CPU model in GCC. Platform-abstracted: non-x86/aarch64
    // hosts conservatively report the scalar route.
#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    caps.has_sse42  = __builtin_cpu_supports("sse4.2");
    caps.has_avx2   = __builtin_cpu_supports("avx2");
    caps.has_avx512 = __builtin_cpu_supports("avx512f");
#else
    // MSVC / other toolchains: conservative default (scalar route).
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    caps.has_neon = true;   // all current aarch64 implementations expose NEON
#endif

    // Highest supported ISA, weakest → strongest (per cpu_isa.hpp
    // "highest_isa is always ≥ Scalar").
    if (caps.has_avx512) {
        caps.highest_isa = CpuIsa::AVX512;
    } else if (caps.has_avx2) {
        caps.highest_isa = CpuIsa::AVX2;
    } else if (caps.has_sse42) {
        caps.highest_isa = CpuIsa::SSE42;
    } else if (caps.has_neon) {
        caps.highest_isa = CpuIsa::NEON;
    } else {
        caps.highest_isa = CpuIsa::Scalar;
    }
    return caps;
}

} // namespace simd
} // namespace chronon3d
