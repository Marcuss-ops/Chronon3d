#pragma once

// device_program.hpp — DeviceProgram boundary (ProgramCache)
//
// Single authority motto: "1 semantic program, 2 lowerings."
//
// Mutant entries must be expanded between edits.
//
//   PixelProgram ──► DeviceProgramKey(target, specialization) ──► artifact
//
// Slang/PTX/SASS are compiler technologies that lower the SAME PixelProgram.
// They do not create `SlangCache` / `PtxCache` / `SassCache` /
// `ShaderBinaryRegistry`, and they do not create a renderer/backend/cache
// family. Every compiled GPU payload belongs to ProgramCache (see
// cache/cache_taxonomy.hpp) and is persisted via cache::CompiledArtifactCache.
// The key is:
//
//   ProgramCache
//       └─ DeviceProgramKey
//             ├─ PixelProgram fingerprint (semantic identity)
//             ├─ target              (SPIR-V / PTX / CpuSimd)
//             ├─ determinism class   (BitExact / DeterministicWithinPlatform / Approximate)
//             ├─ specialization      (future: constants folded at compile time)
//             ├─ compiler version
//             ├─ backend ABI version
//             └─ device capability   (future: sm_86, rdna3, …)
//
// v1 lowering set:
//   - CpuSimd  — the certified PixelProgram v2 stack interpreter/payload path.
//                Backend execution remains behind the compiled artifact API.
//   - SPIR-V   — reserved (ACTIVE only after PixelProgram v1 + SLANG ENTRY GATE).
//   - PTX      — reserved (ACTIVE only after SPIR-V lowering is certified).
//
// The two GPU targets are stubbed as fail-closed until the entry gate below
// is satisfied — they must not be callable as a production lowering in v1.
//
//   SLANG ENTRY GATE (from the analysis memo):
//     same-SHA main green
//     decoder ASAN clean
//     WBH certification baseline exists
//     DeterminismClass implemented (BitExact / DeterministicWithinPlatform / Approximate)
//     single synchronization authority
//     PixelProgram v2 frozen  ← current IR version
//     one reference PixelProgram corpus  ← tests/render_graph/compiler/test_pixel_program.cpp
//     ProgramFingerprint includes IR version / compiler version / target / specialization / determinism class

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <chronon3d/cache/compiled_artifact_cache.hpp>
#include <chronon3d/render_graph/compiler/bit_exact_contract.hpp>
#include <chronon3d/render_graph/compiler/pixel_program.hpp>

namespace chronon3d::graph::pixel {

enum class DeviceTarget : std::uint8_t {
    CpuSimd = 0,
    SpirV = 1,
    Ptx = 2,
};

// Canonical artifact-backend identities. Keep these outside the compiler
// implementation so compiler-boundary checks do not treat serialization
// vocabulary as a backend dependency.
inline constexpr std::string_view kCpuArtifactBackend = "cpu";
inline constexpr std::string_view kGpuArtifactBackendA = "vulkan";
inline constexpr std::string_view kGpuArtifactBackendB = "cuda";

[[nodiscard]] std::string_view to_string(DeviceTarget target) noexcept;
[[nodiscard]] std::optional<DeviceTarget> device_target_from_string(
    std::string_view value) noexcept;

struct DeviceProgramKey {
    std::uint64_t pixel_program_fingerprint{0};
    DeviceTarget target{DeviceTarget::CpuSimd};
    determinism::DeterminismClass determinism_class{
        determinism::DeterminismClass::BitExact};
    std::string specialization;
    std::string compiler_version;
    std::uint32_t backend_abi_version{kPixelProgramAbiVersion};
    std::string device_capability;

    // Stable, filesystem-safe ProgramCache identity. Deterministic across
    // processes and machines. Never an object address or filename.
    [[nodiscard]] std::string stable_name() const;

    // Bridge to the persistent ProgramCache artifact store. The returned key
    // round-trips through cache::CompiledArtifactCache::save_program_artifact
    // / load_program_artifact without inventing a second artifact authority.
    [[nodiscard]] cache::ProgramArtifactKey to_program_artifact_key() const;

    [[nodiscard]] bool operator==(const DeviceProgramKey& other) const noexcept;
    [[nodiscard]] bool operator!=(const DeviceProgramKey& other) const noexcept {
        return !(*this == other);
    }
};

[[nodiscard]] DeviceProgramKey make_device_program_key(
    const PixelProgram& program,
    DeviceTarget target,
    std::string specialization = {},
    std::string compiler_version = {},
    std::string device_capability = {}) noexcept;

// ── Lowering result ─────────────────────────────────────────────────────────

enum class LoweringStatus : std::uint8_t {
    Ok = 0,
    InvalidProgram = 1,
    UncertifiedBitExact = 2,
    UnsupportedTarget = 3,
};

struct LoweringResult;

class DeviceProgram {
public:
    [[nodiscard]] const DeviceProgramKey& key() const noexcept { return key_; }
    [[nodiscard]] std::span<const std::uint8_t> payload() const noexcept { return payload_; }
    [[nodiscard]] const determinism::FusionCertification& certification() const noexcept {
        return certification_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return key_.pixel_program_fingerprint != 0 && !payload_.empty() &&
               (key_.determinism_class == determinism::DeterminismClass::Approximate ||
                certification_.bound_to(key_.pixel_program_fingerprint)) &&
               certification_.permits(key_.determinism_class);
    }

private:
    DeviceProgramKey key_;
    std::vector<std::uint8_t> payload_;
    determinism::FusionCertification certification_;

    friend LoweringResult lower_to_cpu(const PixelProgram&) noexcept;
    friend bool execute_lowered_cpu(const DeviceProgram&, float*, const float*,
                                    std::size_t) noexcept;
};

struct LoweringResult {
    LoweringStatus status{LoweringStatus::Ok};
    std::string diagnostic;
    std::vector<std::uint8_t> payload;
    DeviceProgram device_program;

    [[nodiscard]] bool ok() const noexcept {
        return status == LoweringStatus::Ok;
    }
};

// v1 CPU lowering — the only executable lowering in this patch. For
// BitExact programs it is fail-closed unless certification.bit_exact().
// Produces a small descriptor payload whose bytes are executable via
// execute_lowered_cpu() and which is cacheable through CompiledArtifactCache.
[[nodiscard]] LoweringResult lower_to_cpu(const PixelProgram& program) noexcept;

// Execute a CPU-lowered DeviceProgram. The complete comparison evidence is
// retained beside the payload; the payload alone cannot assert BitExact.
[[nodiscard]] bool execute_lowered_cpu(
    const DeviceProgram& program,
    float* dst_rgba,
    const float* src_rgba,
    std::size_t pixels) noexcept;

// Raw payload execution is limited to Approximate programs because it lacks
// the certificate attached to DeviceProgram.
[[nodiscard]] bool execute_lowered_cpu(
    std::span<const std::uint8_t> payload,
    float* dst_rgba,
    const float* src_rgba,
    std::size_t pixels) noexcept;

// SPIR-V / PTX lowerings — reserved for the future Slang pipeline. They are
// declared here so the cache key + artifact plumbing can be unit-tested, but
// they return UnsupportedTarget until the SLANG ENTRY GATE is satisfied.
[[nodiscard]] LoweringResult lower_to_spirv(const PixelProgram& program) noexcept;
[[nodiscard]] LoweringResult lower_to_ptx(const PixelProgram& program) noexcept;

} // namespace chronon3d::graph::pixel
