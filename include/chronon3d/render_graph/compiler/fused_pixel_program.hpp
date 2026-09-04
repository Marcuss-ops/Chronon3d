#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <chronon3d/render_graph/compiler/bit_exact_contract.hpp>
#include <chronon3d/simd/pixel_kernels.hpp>

namespace chronon3d { struct RenderCounters; }
namespace chronon3d::graph { class RenderGraph; struct RenderGraphContext; }

namespace chronon3d::graph::fusion {

struct PixelOperation {
    enum class Kind : std::uint8_t { ColorMatrix, Opacity, Blend };
    Kind kind{Kind::ColorMatrix};
    std::array<float, 12> params{};
    std::uint8_t blend_mode{0};

    constexpr PixelOperation() = default;
    explicit PixelOperation(Kind k) : kind(k) {}
    static PixelOperation color_matrix(const std::array<float, 12>& m) noexcept {
        PixelOperation op(Kind::ColorMatrix); op.params = m; return op;
    }
    static PixelOperation opacity(float v) noexcept {
        PixelOperation op(Kind::Opacity); op.params[0] = v; return op;
    }
    static PixelOperation blend(std::uint8_t mode) noexcept {
        PixelOperation op(Kind::Blend); op.blend_mode = mode; return op;
    }
};

using PixelKernel = chronon3d::simd::BlendKernel::ApplyFn;

/// Structural fusion guards. Precision compatibility is deliberately distinct
/// from BitExact certification: float32/1-ULP compatibility alone can never
/// certify exact output bits.
struct FusedColorOpacityBlendGuard {
    bool math_order_preserved{false};
    bool blend_mode_compatible{false};
    bool dirty_rect_compatible{false};
    bool precision_compatible{false};

    [[nodiscard]] constexpr bool structural_pass() const noexcept {
        return math_order_preserved && blend_mode_compatible &&
               dirty_rect_compatible && precision_compatible;
    }

    [[nodiscard]] std::array<char, 5> tag() const noexcept {
        return {{
            math_order_preserved ? 'M' : 'm',
            blend_mode_compatible ? 'B' : 'b',
            dirty_rect_compatible ? 'D' : 'd',
            precision_compatible ? 'P' : 'p',
            '\0'
        }};
    }
};

struct FusedPixelProgram {
    std::vector<PixelOperation> operations;
    PixelKernel resolved_kernel{nullptr};
    FusedColorOpacityBlendGuard guards{};

    /// Determinism authority for this program. BitExact is the default and is
    /// fail-closed until a comparison harness attaches an exact certificate.
    determinism::BitExactContract determinism_contract{};
    determinism::FusionCertification certification{};

    std::size_t bytes_per_pixel{16};
    std::size_t pixel_count{0};

    [[nodiscard]] bool certified_for_execution() const noexcept {
        return guards.structural_pass() &&
               certification.permits(determinism_contract.required);
    }

    [[nodiscard]] bool execute(float* dst_rgba,
                               const float* src_rgba,
                               std::size_t pixels) const;

    [[nodiscard]] std::size_t bytes_unfused() const noexcept {
        return 3 * 2 * pixel_count * bytes_per_pixel;
    }
    [[nodiscard]] std::size_t bytes_fused() const noexcept {
        return 3 * pixel_count * bytes_per_pixel;
    }
    [[nodiscard]] std::size_t bytes_saved() const noexcept {
        return certified_for_execution() ? bytes_unfused() - bytes_fused() : 0;
    }
};

struct FusionStats {
    std::size_t passes_before_fusion{0};
    std::size_t passes_after_fusion{0};
    std::size_t bytes_saved_by_fusion{0};
};

void emit_fusion_counters(
    chronon3d::RenderCounters* counters,
    std::size_t passes_before_fusion,
    std::size_t passes_after_fusion,
    std::size_t bytes_saved_by_fusion) noexcept;

/// Detects candidates and emits descriptors. Descriptors are not executable in
/// BitExact mode until `certification.bit_exact()` is true. This separates
/// candidate lowering from certification and keeps the runtime fail-closed.
[[nodiscard]] FusionStats fuse_color_opacity_blend(
    const graph::RenderGraph& graph,
    const graph::RenderGraphContext& ctx,
    const chronon3d::simd::PixelKernelSet& kernels,
    std::vector<FusedPixelProgram>& out_programs);

} // namespace chronon3d::graph::fusion
