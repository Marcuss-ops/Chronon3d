// tests/simd/test_cpu_isa.cpp
// ════════════════════════════════════════════════════════════════════════════
// Certifies the canonical CPU ISA detection API
// (`include/chronon3d/simd/cpu_isa.hpp`, implemented in
// `src/backends/software/simd/cpu_isa.cpp`):
//   - `cpu_isa_name` / `parse_cpu_isa` stable wire contract
//   - `CpuCapabilities::supports` containment semantics
//   - `detect_cpu_capabilities` honors `CHRONON3D_FORCE_CPU_ISA`
//   - host detection coherence (highest_isa consistent with flags)
//
// Pure API contract test (no rendering backend). TIER=UNIT, registered
// unconditionally via `simd/cpu_isa_tests.cmake` — the API surface it
// certifies is available on every build.
// ════════════════════════════════════════════════════════════════════════════

#include <chronon3d/simd/cpu_isa.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

namespace cs = chronon3d::simd;

namespace {

// Restore the pre-test state of CHRONON3D_FORCE_CPU_ISA on scope exit so
// doctest cases stay isolated from the ambient environment.
class ScopedForceIsaEnv {
public:
    explicit ScopedForceIsaEnv(const char* value) {
        saved_ = std::getenv("CHRONON3D_FORCE_CPU_ISA");
        if (saved_ != nullptr) {
            saved_copy_ = saved_;
            saved_ = saved_copy_.c_str();
        }
        set(value);
    }

    ~ScopedForceIsaEnv() {
        if (saved_ != nullptr) {
            set(saved_);
        } else {
            unsetenv("CHRONON3D_FORCE_CPU_ISA");
        }
    }

    static void set(const char* value) {
        if (value != nullptr) {
            setenv("CHRONON3D_FORCE_CPU_ISA", value, 1);
        } else {
            unsetenv("CHRONON3D_FORCE_CPU_ISA");
        }
    }

private:
    const char* saved_{nullptr};
    std::string saved_copy_;
};

} // namespace

TEST_CASE("cpu_isa_name: canonical lowercase wire contract") {
    CHECK_EQ(std::string(cs::cpu_isa_name(cs::CpuIsa::Scalar)), "scalar");
    CHECK_EQ(std::string(cs::cpu_isa_name(cs::CpuIsa::SSE42)), "sse42");
    CHECK_EQ(std::string(cs::cpu_isa_name(cs::CpuIsa::AVX2)), "avx2");
    CHECK_EQ(std::string(cs::cpu_isa_name(cs::CpuIsa::AVX512)), "avx512");
    CHECK_EQ(std::string(cs::cpu_isa_name(cs::CpuIsa::NEON)), "neon");
}

TEST_CASE("cpu_isa_name: out-of-range enumerator is clamped to scalar") {
    // Defensive contract: the function is noexcept and never null.
    const auto out_of_range = static_cast<cs::CpuIsa>(999);
    CHECK_EQ(std::string(cs::cpu_isa_name(out_of_range)), "scalar");
}

TEST_CASE("parse_cpu_isa: case-insensitive roundtrip") {
    for (const cs::CpuIsa isa :
         {cs::CpuIsa::Scalar, cs::CpuIsa::SSE42, cs::CpuIsa::AVX2,
          cs::CpuIsa::AVX512, cs::CpuIsa::NEON}) {
        const char* name = cs::cpu_isa_name(isa);
        cs::CpuIsa parsed{};
        CHECK(cs::parse_cpu_isa(name, parsed));
        CHECK_EQ(parsed, isa);
    }
}

TEST_CASE("parse_cpu_isa: mixed-case input is accepted") {
    cs::CpuIsa parsed{};
    REQUIRE(cs::parse_cpu_isa("AVX2", parsed));
    CHECK_EQ(parsed, cs::CpuIsa::AVX2);
    REQUIRE(cs::parse_cpu_isa("aVx512", parsed));
    CHECK_EQ(parsed, cs::CpuIsa::AVX512);
    REQUIRE(cs::parse_cpu_isa("NeoN", parsed));
    CHECK_EQ(parsed, cs::CpuIsa::NEON);
}

TEST_CASE("parse_cpu_isa: invalid input is rejected") {
    cs::CpuIsa parsed = cs::CpuIsa::AVX2;
    CHECK_FALSE(cs::parse_cpu_isa("avx3", parsed));
    CHECK_FALSE(cs::parse_cpu_isa("", parsed));
    CHECK_FALSE(cs::parse_cpu_isa(" sse42", parsed));  // leading space
    CHECK_FALSE(cs::parse_cpu_isa(nullptr, parsed));
    // Failed parse must not mutate the output.
    CHECK_EQ(parsed, cs::CpuIsa::AVX2);
}

TEST_CASE("CpuCapabilities::supports: scalar is always available") {
    const cs::CpuCapabilities none{};   // value-init → all flags false
    CHECK(none.supports(cs::CpuIsa::Scalar));
    CHECK_FALSE(none.supports(cs::CpuIsa::SSE42));
    CHECK_FALSE(none.supports(cs::CpuIsa::AVX2));
    CHECK_FALSE(none.supports(cs::CpuIsa::AVX512));
    CHECK_FALSE(none.supports(cs::CpuIsa::NEON));
}

TEST_CASE("CpuCapabilities::supports: ISA containment is monotone") {
    const cs::CpuCapabilities avx512_caps{
        cs::CpuIsa::AVX512, true, true, true, false};
    CHECK(avx512_caps.supports(cs::CpuIsa::AVX512));
    CHECK(avx512_caps.supports(cs::CpuIsa::AVX2));
    CHECK(avx512_caps.supports(cs::CpuIsa::SSE42));
    CHECK(avx512_caps.supports(cs::CpuIsa::Scalar));
    CHECK_FALSE(avx512_caps.supports(cs::CpuIsa::NEON));

    const cs::CpuCapabilities neon_caps{cs::CpuIsa::NEON, false, false, false, true};
    CHECK(neon_caps.supports(cs::CpuIsa::NEON));
    CHECK(neon_caps.supports(cs::CpuIsa::Scalar));
    CHECK_FALSE(neon_caps.supports(cs::CpuIsa::SSE42));
}

TEST_CASE("detect_cpu_capabilities: honors CHRONON3D_FORCE_CPU_ISA") {
    // Deterministic override (ADR-020 / TICKET-SIMD-REGISTRY-CANONICAL):
    // the returned snapshot is exactly the forced ISA + weaker ISAs it
    // implies — no host probe runs, so the result is host-independent.
    ScopedForceIsaEnv env_guard(nullptr);

    ScopedForceIsaEnv::set("Scalar");
    {
        const auto caps = cs::detect_cpu_capabilities();
        CHECK_EQ(caps.highest_isa, cs::CpuIsa::Scalar);
        CHECK_FALSE(caps.has_sse42);
        CHECK_FALSE(caps.has_avx2);
        CHECK_FALSE(caps.has_avx512);
        CHECK_FALSE(caps.has_neon);
    }

    ScopedForceIsaEnv::set("AVX2");
    {
        const auto caps = cs::detect_cpu_capabilities();
        CHECK_EQ(caps.highest_isa, cs::CpuIsa::AVX2);
        CHECK(caps.has_sse42);
        CHECK(caps.has_avx2);
        CHECK_FALSE(caps.has_avx512);
        CHECK_FALSE(caps.has_neon);
        CHECK(caps.supports(cs::CpuIsa::AVX2));
    }

    ScopedForceIsaEnv::set("AVX512");
    {
        const auto caps = cs::detect_cpu_capabilities();
        CHECK_EQ(caps.highest_isa, cs::CpuIsa::AVX512);
        CHECK(caps.has_sse42);
        CHECK(caps.has_avx2);
        CHECK(caps.has_avx512);
        CHECK(caps.supports(cs::CpuIsa::AVX512));
    }

    ScopedForceIsaEnv::set("NEON");
    {
        const auto caps = cs::detect_cpu_capabilities();
        CHECK_EQ(caps.highest_isa, cs::CpuIsa::NEON);
        CHECK(caps.has_neon);
        CHECK(caps.supports(cs::CpuIsa::NEON));
    }

    // An invalid override value must fall through to host detection
    // (never crash, never return garbage).
    ScopedForceIsaEnv::set("definitely-not-an-isa");
    {
        const auto caps = cs::detect_cpu_capabilities();
        CHECK(caps.supports(cs::CpuIsa::Scalar));
        CHECK(caps.supports(caps.highest_isa));
    }
}

TEST_CASE("detect_cpu_capabilities: host snapshot is internally coherent") {
    // Unset the env override → real host probe. The assertions are
    // machine-agnostic: whatever the host supports, the snapshot must
    // be internally consistent.
    ScopedForceIsaEnv env_guard(nullptr);

    const auto caps = cs::detect_cpu_capabilities();

    // highest_isa must be supported by the flags it claims.
    CHECK(caps.supports(caps.highest_isa));

    // Monotone containment: AVX512 ⇒ AVX2 ⇒ SSE42.
    if (caps.has_avx512) {
        CHECK(caps.has_avx2);
        CHECK(caps.has_sse42);
        CHECK_EQ(caps.highest_isa, cs::CpuIsa::AVX512);
    } else if (caps.has_avx2) {
        CHECK(caps.has_sse42);
        CHECK(caps.highest_isa >= cs::CpuIsa::AVX2);
    } else if (caps.has_sse42) {
        CHECK(caps.highest_isa == cs::CpuIsa::SSE42);
    }

    // Repeated detection is stable (no hidden state).
    const auto caps2 = cs::detect_cpu_capabilities();
    CHECK_EQ(caps.highest_isa, caps2.highest_isa);
    CHECK_EQ(caps.has_sse42, caps2.has_sse42);
    CHECK_EQ(caps.has_avx2, caps2.has_avx2);
    CHECK_EQ(caps.has_avx512, caps2.has_avx512);
    CHECK_EQ(caps.has_neon, caps2.has_neon);
}
