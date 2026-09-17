#include <chronon3d/render_graph/compiler/pixel_program.hpp>

#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/simd/kernel_resolver.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace chronon3d::graph::pixel {

namespace {

[[nodiscard]] const simd::PixelKernelSet& resolved_pixel_kernels() noexcept {
    static const simd::PixelKernelSet kernels =
        simd::resolve_pixel_kernels(simd::detect_cpu_capabilities());
    return kernels;
}

inline constexpr std::size_t kMaxRetainedScratchBytes = 8u * 1024u * 1024u;

class TransformedScratch {
public:
    explicit TransformedScratch(std::size_t scalar_count) {
        if (scalar_count * sizeof(float) <= kMaxRetainedScratchBytes) {
            static thread_local std::vector<float> retained;
            if (retained.size() < scalar_count) retained.resize(scalar_count);
            std::fill_n(retained.data(), scalar_count, 0.0f);
            data_ = retained.data();
            return;
        }
        owned_.assign(scalar_count, 0.0f);
        data_ = owned_.data();
    }
    [[nodiscard]] float* data() const noexcept { return data_; }
private:
    std::vector<float> owned_;
    float* data_{nullptr};
};

} // namespace

bool PixelProgram::is_valid() const noexcept {
    if (ir_version != kPixelProgramIrVersion) return false;
    if (abi_version != kPixelProgramAbiVersion) return false;
    if (ops.size() != 3) return false;
    if (ops[0].kind != PixelProgramOpKind::ColorMatrix) return false;
    if (ops[1].kind != PixelProgramOpKind::Opacity) return false;
    if (ops[2].kind != PixelProgramOpKind::Blend) return false;
    if (ops[2].blend_mode != 0) return false;
    const float alpha = ops[1].params[0];
    if (!(alpha >= 0.0f && alpha <= 1.0f)) return false;
    return true;
}

bool PixelProgram::certified_for_execution() const noexcept {
    if (!is_valid()) return false;
    return certification.permits(determinism_contract.required);
}

std::uint64_t PixelProgram::fingerprint() const noexcept {
    using core::hash::HashBuilder;
    HashBuilder hb;
    hb.add("chronon3d.pixel-program.v1");
    hb.add(ir_version);
    hb.add(abi_version);
    hb.add_enum(determinism_contract.required);
    hb.add(static_cast<std::uint64_t>(ops.size()));
    for (const auto& op : ops) {
        hb.add_enum(op.kind);
        // Hash params as raw bytes for bit-stability (fingerprint is
        // semantic identity, not execution residency).
        hb.add_bytes(op.params.data(), sizeof(float) * op.params.size());
        hb.add(static_cast<std::uint64_t>(op.blend_mode));
    }
    // Certification is NOT folded into the semantic fingerprint: the same
    // program hashes the same way before and after the harness attaches
    // evidence. Execution fail-closed is certification-gated, not
    // fingerprint-gated.
    return hb.finish();
}

bool PixelProgram::execute(float* dst_rgba,
                           const float* src_rgba,
                           std::size_t pixels) const {
    if (!dst_rgba || !src_rgba || pixels == 0) return false;
    if (!is_valid()) return false;
    if (!certified_for_execution()) return false;

    const PixelProgramOp* color_matrix = &ops[0];
    const PixelProgramOp* opacity = &ops[1];
    const PixelProgramOp* blend = &ops[2];
    if (blend->blend_mode != 0) return false;

    const std::size_t scalar_count = pixels * 4;
    const TransformedScratch scratch_buffer(scalar_count);
    float* const transformed = scratch_buffer.data();
    const auto& kernels = resolved_pixel_kernels();
    if (!kernels.color_matrix.apply) return false;
    kernels.color_matrix.apply(
        transformed, src_rgba, pixels, color_matrix->params.data());

    const float alpha = opacity->params[0];
    if (!(alpha >= 0.0f && alpha <= 1.0f)) return false;
    for (std::size_t index = 0; index < scalar_count; ++index) {
        transformed[index] *= alpha;
    }

    const auto& blend_kernel = kernels.blend.apply;
    if (!blend_kernel) return false;
    blend_kernel(dst_rgba, transformed, pixels);
    return true;
}

PixelProgram make_identity_pilot_program(std::size_t pixel_count) noexcept {
    const std::array<float, 12> identity_cm = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
    };
    return make_pilot_program(identity_cm, 1.0f, 0, pixel_count);
}

PixelProgram make_pilot_program(
    const std::array<float, 12>& color_matrix,
    float opacity,
    std::uint8_t blend_mode,
    std::size_t pixel_count) noexcept {
    PixelProgram program;
    program.ops.reserve(3);
    program.ops.push_back(PixelProgramOp::color_matrix(color_matrix));
    program.ops.push_back(PixelProgramOp::opacity(opacity));
    program.ops.push_back(PixelProgramOp::blend(blend_mode));
    program.pixel_count = pixel_count;
    program.bytes_per_pixel = 16;
    // BitExact is the default contract. No certificate is invented here:
    // the harness must attach one explicitly for certified execution.
    program.determinism_contract.required =
        determinism::DeterminismClass::BitExact;
    return program;
}

bool is_canonical_pilot_triple(const PixelProgram& program) noexcept {
    if (program.ir_version != kPixelProgramIrVersion) return false;
    if (program.abi_version != kPixelProgramAbiVersion) return false;
    if (program.ops.size() != 3) return false;
    return program.ops[0].kind == PixelProgramOpKind::ColorMatrix &&
           program.ops[1].kind == PixelProgramOpKind::Opacity &&
           program.ops[2].kind == PixelProgramOpKind::Blend;
}

} // namespace chronon3d::graph::pixel
