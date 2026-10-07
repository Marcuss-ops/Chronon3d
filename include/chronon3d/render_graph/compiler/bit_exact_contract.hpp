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
    [[nodiscard]] static bool is_sha256_hex(std::string_view value) noexcept {
        if (value.size() != 64) return false;
        for (const char ch : value) {
            if (!((ch >= '0' && ch <= '9') ||
                  (ch >= 'a' && ch <= 'f') ||
                  (ch >= 'A' && ch <= 'F'))) return false;
        }
        return true;
    }

    std::string reference_sha256;
    std::string fused_sha256;
    std::string environment_fingerprint;
    std::string chronon_version;
    // Harness provenance: identifies the exact initial/source pixels, plan,
    // assets, environment, frame extent/index and execution slot compared.
    std::string comparison_input_fingerprint;
    std::uint64_t frame_index{0};
    std::uint32_t slot_index{0};
    std::uint64_t program_fingerprint{0};
    std::uint32_t max_ulp_error{0};
    bool same_plan{false};
    bool same_assets{false};
    bool same_environment{false};
    bool same_chronon_version{false};

    [[nodiscard]] bool has_common_inputs() const noexcept {
        return same_plan && same_assets && same_environment && same_chronon_version &&
               !environment_fingerprint.empty() && !chronon_version.empty() &&
               is_sha256_hex(comparison_input_fingerprint);
    }

    [[nodiscard]] bool bound_to(std::uint64_t fingerprint) const noexcept {
        return fingerprint != 0 && program_fingerprint == fingerprint;
    }

    [[nodiscard]] bool bit_exact() const noexcept {
        return has_common_inputs() && is_sha256_hex(reference_sha256) &&
               is_sha256_hex(fused_sha256) && reference_sha256 == fused_sha256 &&
               max_ulp_error == 0;
    }

    [[nodiscard]] bool deterministic_within_platform() const noexcept {
        return has_common_inputs() && max_ulp_error <= 1 &&
               is_sha256_hex(reference_sha256) && is_sha256_hex(fused_sha256);
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
