#pragma once

// Canonical, versioned pixel IR. v2 extends the v1 pilot with pointwise and
// local-window color operations. Multi-window/spatial-temporal effects remain
// outside this fusion contract. BitExact remains fail-closed until the
// comparison harness attaches equal SHA-256 output evidence.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <chronon3d/render_graph/compiler/bit_exact_contract.hpp>

namespace chronon3d::graph::pixel {

inline constexpr std::uint32_t kPixelProgramIrVersion = 2;
inline constexpr std::uint32_t kPixelProgramAbiVersion = 2;

enum class PixelProgramOpKind : std::uint8_t {
    ColorMatrix = 0, Opacity = 1, Blend = 2,
    Vignette = 3, Noise = 4, Levels = 5, Threshold = 6,
    Brightness = 7, Contrast = 8, Saturation = 9, Exposure = 10,
    Tint = 11, Invert = 12,
};

struct PixelProgramOp {
    PixelProgramOpKind kind{PixelProgramOpKind::ColorMatrix};
    // params ABI: ColorMatrix[0..11]; Opacity[0]; Vignette[radius,softness,
    // amount,r,g,b]; Noise[amount,seed_bits,frame_bits,size,shadow,mid,high,rgb];
    // Levels[red,green,blue,master], five values each; Threshold[threshold,softness];
    // scalar color ops[0]; Tint[r,g,b,amount]. Remaining slots are reserved zero.
    std::array<float, 24> params{};
    std::uint8_t blend_mode{0};
    constexpr PixelProgramOp() = default;
    explicit PixelProgramOp(PixelProgramOpKind value) : kind(value) {}

    static PixelProgramOp color_matrix(const std::array<float, 12>& values) noexcept {
        PixelProgramOp op(PixelProgramOpKind::ColorMatrix);
        std::copy(values.begin(), values.end(), op.params.begin()); return op;
    }
    static PixelProgramOp opacity(float value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Opacity); op.params[0] = value; return op;
    }
    static PixelProgramOp blend(std::uint8_t value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Blend); op.blend_mode = value; return op;
    }
    static PixelProgramOp vignette(float radius, float softness, float amount,
                                   float red, float green, float blue) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Vignette);
        op.params[0]=radius; op.params[1]=softness; op.params[2]=amount;
        op.params[3]=red; op.params[4]=green; op.params[5]=blue; return op;
    }
    static PixelProgramOp noise(const std::array<float, 8>& values) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Noise);
        std::copy(values.begin(), values.end(), op.params.begin()); return op;
    }
    static PixelProgramOp levels(const std::array<float, 20>& values) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Levels);
        std::copy(values.begin(), values.end(), op.params.begin()); return op;
    }
    static PixelProgramOp threshold(float threshold_value, float softness) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Threshold);
        op.params[0]=threshold_value; op.params[1]=softness; return op;
    }
    static PixelProgramOp brightness(float value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Brightness); op.params[0]=value; return op;
    }
    static PixelProgramOp contrast(float value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Contrast); op.params[0]=value; return op;
    }
    static PixelProgramOp saturation(float value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Saturation); op.params[0]=value; return op;
    }
    static PixelProgramOp exposure(float value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Exposure); op.params[0]=value; return op;
    }
    static PixelProgramOp tint(float red, float green, float blue, float amount) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Tint);
        op.params[0]=red; op.params[1]=green; op.params[2]=blue; op.params[3]=amount; return op;
    }
    static PixelProgramOp invert(float value) noexcept {
        PixelProgramOp op(PixelProgramOpKind::Invert); op.params[0]=value; return op;
    }
};

struct FusionCertificationInputs {
    std::string plan_fingerprint;
    std::string asset_fingerprint;
    std::string environment_fingerprint;
    std::string chronon_version;
    std::uint64_t frame_index{0};
    std::uint32_t slot_index{0};
};

struct PixelProgram {
    std::uint32_t ir_version{kPixelProgramIrVersion};
    std::uint32_t abi_version{kPixelProgramAbiVersion};
    std::vector<PixelProgramOp> ops;
    determinism::BitExactContract determinism_contract{};
    determinism::FusionCertification certification{};
    std::size_t bytes_per_pixel{16};
    std::size_t pixel_count{0};
    std::uint32_t frame_width{0};
    std::uint32_t frame_height{0};

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool certified_for_execution() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
    [[nodiscard]] std::size_t bytes_unfused() const noexcept {
        return 3u * ops.size() * pixel_count * bytes_per_pixel;
    }
    [[nodiscard]] std::size_t bytes_fused() const noexcept {
        return 3u * pixel_count * bytes_per_pixel;
    }
    [[nodiscard]] std::size_t bytes_saved() const noexcept {
        return certified_for_execution() && bytes_unfused() >= bytes_fused()
            ? bytes_unfused() - bytes_fused() : 0;
    }
    [[nodiscard]] bool execute(float* dst_rgba, const float* src_rgba,
                               std::size_t pixels) const;
    [[nodiscard]] bool execute_unfused_reference(float* dst_rgba,
                                                 const float* src_rgba,
                                                 std::size_t pixels) const;
    [[nodiscard]] bool execute_fused_candidate(float* dst_rgba,
                                               const float* src_rgba,
                                               std::size_t pixels) const;
};

[[nodiscard]] bool certify_fusion(PixelProgram& program,
                                  const float* initial_dst_rgba,
                                  const float* src_rgba,
                                  std::size_t pixels,
                                  const FusionCertificationInputs& inputs);
[[nodiscard]] bool make_pointwise_stack(PixelProgram& program,
                                        std::span<const PixelProgramOp> color_ops,
                                        std::size_t pixel_count,
                                        std::uint32_t frame_width = 0,
                                        std::uint32_t frame_height = 0);
[[nodiscard]] PixelProgram make_identity_pilot_program(std::size_t pixel_count) noexcept;
[[nodiscard]] PixelProgram make_pilot_program(
    const std::array<float, 12>& color_matrix, float opacity,
    std::uint8_t blend_mode, std::size_t pixel_count) noexcept;
[[nodiscard]] bool is_canonical_pilot_triple(const PixelProgram& program) noexcept;

} // namespace chronon3d::graph::pixel
