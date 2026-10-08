#pragma once

// Host-side lowering of a pointwise PixelProgram into the op table read by
// shaders/pixel_program_pointwise.comp. Pure C++: no Vulkan dependency, so the
// encoding contract is unit-testable on any host.
//
// Fail-closed contract:
//   * only the nine pointwise kinds the shader implements are accepted;
//   * Blend is legal only as the final op and is returned as trailing_blend
//     (composited outside this stack, never emitted into the table);
//   * Vignette needs the frame extent to match pixel_count;
//   * the program-level gate requires certified_for_execution().

#include <chronon3d/render_graph/compiler/pixel_program.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace chronon3d::backends::vulkan {

/// std430 record read by pixel_program_pointwise.comp (112 bytes):
/// header.x = kind, then the 24 PixelProgramOp params in order.
struct PointwiseGpuOp {
    std::uint32_t kind{0};
    std::uint32_t reserved[3]{0, 0, 0};
    std::array<float, 24> params{};
};
static_assert(sizeof(PointwiseGpuOp) == 112,
              "PointwiseGpuOp must match the shader's std430 op layout");

[[nodiscard]] inline bool is_gpu_pointwise_kind(
    graph::pixel::PixelProgramOpKind kind) noexcept {
    using K = graph::pixel::PixelProgramOpKind;
    switch (kind) {
    case K::ColorMatrix:
    case K::Opacity:
    case K::Vignette:
    case K::Brightness:
    case K::Contrast:
    case K::Saturation:
    case K::Exposure:
    case K::Tint:
    case K::Invert:
        return true;
    default:
        return false;
    }
}

/// Encode a raw op sequence. Returns false with `reason` set for any op the
/// shader does not implement, or for a Blend that is not the final op.
[[nodiscard]] inline bool encode_pointwise_ops(
    std::span<const graph::pixel::PixelProgramOp> ops,
    std::vector<PointwiseGpuOp>& out,
    bool& trailing_blend,
    std::string& reason) {
    using K = graph::pixel::PixelProgramOpKind;
    out.clear();
    trailing_blend = false;
    out.reserve(ops.size());
    for (std::size_t index = 0; index < ops.size(); ++index) {
        const auto& op = ops[index];
        if (op.kind == K::Blend) {
            if (index + 1 != ops.size()) {
                reason = "pointwise encoder: Blend must be the final op";
                return false;
            }
            trailing_blend = true;
            continue;
        }
        if (!is_gpu_pointwise_kind(op.kind)) {
            reason = "pointwise encoder: op kind " +
                     std::to_string(static_cast<unsigned>(op.kind)) +
                     " is not a pointwise GPU op";
            return false;
        }
        PointwiseGpuOp record{};
        record.kind = static_cast<std::uint32_t>(op.kind);
        record.params = op.params;
        out.push_back(record);
    }
    return true;
}

/// Program-level gate for real GPU use. Requires a certified program so an
/// uncertified fusion can never reach the device, and an exact frame extent
/// when the stack contains a Vignette.
[[nodiscard]] inline bool encode_certified_pointwise_program(
    const graph::pixel::PixelProgram& program,
    std::vector<PointwiseGpuOp>& out,
    bool& trailing_blend,
    std::string& reason) {
    if (!program.certified_for_execution()) {
        reason = "pointwise encoder: program is not certified for execution";
        return false;
    }
    if (!encode_pointwise_ops(program.ops, out, trailing_blend, reason)) {
        return false;
    }
    for (const auto& record : out) {
        if (record.kind ==
            static_cast<std::uint32_t>(graph::pixel::PixelProgramOpKind::Vignette)) {
            const auto extent = static_cast<std::uint64_t>(program.frame_width) *
                                program.frame_height;
            if (extent == 0 || extent != program.pixel_count) {
                reason = "pointwise encoder: Vignette requires frame extent == pixel_count";
                return false;
            }
            break;
        }
    }
    return true;
}

} // namespace chronon3d::backends::vulkan
