#pragma once

// pixel_program.hpp — canonical PixelProgram IR v1
//
// This is the semantic authority described in the NVIDIA PTX + SPIR-V + Slang
// analysis. It does NOT introduce Slang/PTX/SASS, a new cache family, or a new
// registry. Lifecycle:
//
//   FrameDelta
//     ↓
//   canonical PixelProgram IR  (THIS FILE — single semantic program)
//     ↓
//   backend lowering  (DeviceProgram, ProgramCache)
//
//                      PixelProgram
//                           │
//               ┌────────────┴────────────┐
//               ▼                         ▼
//        Slang → SPIR-V            Slang → PTX   (future, same IR)
//               │                         │
//               ▼                         ▼
//            Vulkan                     CUDA
//
// Current v1 pilot domain is exactly the triple that FusedPixelProgram
// already validates:
//
//   ColorMatrix → Opacity → Blend
//
// The IR is versioned (ir_version / abi_version), fingerprinted with the
// canonical HashBuilder, and bound to DeterminismClass. BitExact execution
// remains fail-closed until a FusionCertification with equal reference/fused
// SHA-256 is attached — the same contract as FusedPixelProgram.
//
// Cache doctrine: PixelProgram and its DeviceProgram lowerings are ProgramCache
// content (see cache/cache_taxonomy.hpp). There is no SlangCache / PtxCache /
// ShaderBinaryRegistry. The persistent key is DeviceProgramKey::stable_name()
// which maps to cache::ProgramArtifactKey for CompiledArtifactCache.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <chronon3d/render_graph/compiler/bit_exact_contract.hpp>

namespace chronon3d::graph::pixel {

inline constexpr std::uint32_t kPixelProgramIrVersion = 1;
inline constexpr std::uint32_t kPixelProgramAbiVersion = 1;

// ── Pixel operation kinds (v1 pilot) ────────────────────────────────────────

enum class PixelProgramOpKind : std::uint8_t {
    ColorMatrix = 0,
    Opacity = 1,
    Blend = 2,
};

struct PixelProgramOp {
    PixelProgramOpKind kind{PixelProgramOpKind::ColorMatrix};
    std::array<float, 12> params{};
    std::uint8_t blend_mode{0};

    constexpr PixelProgramOp() = default;
    explicit PixelProgramOp(PixelProgramOpKind k) : kind(k) {}

    static PixelProgramOp color_matrix(const std::array<float, 12>& m) noexcept {
        PixelProgramOp op(PixelProgramOpKind::ColorMatrix);
        op.params = m;
        return op;
    }
    static PixelProgramOp opacity(float v) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Opacity);
        op.params[0] = v;
        return op;
    }
    static PixelProgramOp blend(std::uint8_t mode) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Blend);
        op.blend_mode = mode;
        return op;
    }
};

// ── Canonical IR ────────────────────────────────────────────────────────────

struct PixelProgram {
    std::uint32_t ir_version{kPixelProgramIrVersion};
    std::uint32_t abi_version{kPixelProgramAbiVersion};
    std::vector<PixelProgramOp> ops;

    // Determinism authority. BitExact is the default and is fail-closed until
    // the comparison harness attaches an exact certificate.
    determinism::BitExactContract determinism_contract{};
    determinism::FusionCertification certification{};

    // Execution-bound extent (not part of semantic fingerprint except for
    // parity metrics). Kept for bytes_saved parity with FusedPixelProgram.
    std::size_t bytes_per_pixel{16};
    std::size_t pixel_count{0};

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool certified_for_execution() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

    [[nodiscard]] std::size_t bytes_unfused() const noexcept {
        return 3u * 2u * pixel_count * bytes_per_pixel;
    }
    [[nodiscard]] std::size_t bytes_fused() const noexcept {
        return 3u * pixel_count * bytes_per_pixel;
    }
    [[nodiscard]] std::size_t bytes_saved() const noexcept {
        return certified_for_execution() ? bytes_unfused() - bytes_fused() : 0;
    }

    // CPU execution for the pilot triple. Fail-closed if not certified.
    [[nodiscard]] bool execute(float* dst_rgba,
                               const float* src_rgba,
                               std::size_t pixels) const;
};

// ── Pilot builders ─────────────────────────────────────────────────────────

// Identity pilot program: ColorMatrix(identity) → Opacity(1.0) → Blend(0).
// Matches the FusedPixelProgram pilot triple; useful for parity tests.
[[nodiscard]] PixelProgram make_identity_pilot_program(std::size_t pixel_count) noexcept;

// General pilot builder with explicit params. Returns an invalid program if
// params are out of contract (e.g. opacity outside [0,1]).
[[nodiscard]] PixelProgram make_pilot_program(
    const std::array<float, 12>& color_matrix,
    float opacity,
    std::uint8_t blend_mode,
    std::size_t pixel_count) noexcept;

// Predicate: true iff program is the canonical pilot triple
//   ColorMatrix → Opacity → Blend  (in that order, exactly 3 ops).
[[nodiscard]] bool is_canonical_pilot_triple(const PixelProgram& program) noexcept;

} // namespace chronon3d::graph::pixel
