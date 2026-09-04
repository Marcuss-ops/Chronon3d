#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::graph::determinism {

/// Determinism is a compile/execution contract, not a tolerance alias.
enum class DeterminismClass : std::uint8_t {
    BitExact,
    DeterministicWithinPlatform,
    Approximate,
};

struct BitExactContract {
    DeterminismClass required{DeterminismClass::BitExact};

    /// BitExact means byte-for-byte identical output. No ULP tolerance is
    /// accepted in this mode. DeterministicWithinPlatform may declare a
    /// numeric tolerance, but it must never be promoted to BitExact.
    [[nodiscard]] constexpr bool permits_ulp_tolerance() const noexcept {
        return required != DeterminismClass::BitExact;
    }
};

/// Certificate emitted by a deterministic comparison harness. The harness is
/// responsible for hashing the unfused reference and fused implementation over
/// the same plan/assets/environment/version. Runtime/compiler code only consumes
/// the resulting immutable evidence; it does not invent certification.
struct FusionCertification {
    std::string reference_sha256;
    std::string fused_sha256;
    std::string environment_fingerprint;
    std::string chronon_version;
    std::uint32_t max_ulp_error{0};
    bool same_plan{false};
    bool same_assets{false};
    bool same_environment{false};
    bool same_chronon_version{false};

    [[nodiscard]] bool has_common_inputs() const noexcept {
        return same_plan && same_assets && same_environment && same_chronon_version;
    }

    [[nodiscard]] bool bit_exact() const noexcept {
        return has_common_inputs() &&
               !reference_sha256.empty() &&
               reference_sha256 == fused_sha256;
    }

    [[nodiscard]] bool deterministic_within_platform() const noexcept {
        return has_common_inputs() && max_ulp_error <= 1;
    }

    [[nodiscard]] bool permits(DeterminismClass required) const noexcept {
        switch (required) {
        case DeterminismClass::BitExact:
            return bit_exact();
        case DeterminismClass::DeterministicWithinPlatform:
            return bit_exact() || deterministic_within_platform();
        case DeterminismClass::Approximate:
            return true;
        }
        return false;
    }
};

} // namespace chronon3d::graph::determinism
