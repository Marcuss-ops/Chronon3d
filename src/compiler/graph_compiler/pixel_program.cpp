#include <chronon3d/render_graph/compiler/pixel_program.hpp>

#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/effects/color_contract.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../../assets/sha256.hpp"

namespace chronon3d::graph::pixel {

namespace {

[[nodiscard]] std::string sha256_hex(std::string_view bytes) {
    assets::detail::Sha256 hash;
    hash.update(bytes.data(), bytes.size());
    const auto digest = hash.finish();
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.resize(digest.bytes.size() * 2);
    for (std::size_t i = 0; i < digest.bytes.size(); ++i) {
        const auto value = std::to_integer<unsigned char>(digest.bytes[i]);
        result[2 * i] = digits[value >> 4];
        result[2 * i + 1] = digits[value & 0x0f];
    }
    return result;
}

[[nodiscard]] bool finite_params(const PixelProgramOp& op, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i)
        if (!std::isfinite(op.params[i])) return false;
    for (std::size_t i = count; i < op.params.size(); ++i)
        if (op.params[i] != 0.0f) return false;
    return true;
}

[[nodiscard]] float levels_channel(float value, float input_black,
                                   float input_white, float gamma,
                                   float output_black, float output_white) noexcept {
    const float range = std::max(input_white - input_black, 1.0e-6f);
    const float normalized = (value - input_black) / range;
    const float inverse_gamma = 1.0f / std::max(gamma, 1.0e-6f);
    const float shaped = normalized >= 0.0f
        ? std::pow(normalized, inverse_gamma)
        : -std::pow(-normalized, inverse_gamma);
    return output_black + shaped * (output_white - output_black);
}

[[nodiscard]] std::uint32_t noise_hash(std::uint32_t x, std::uint32_t y,
                                       std::uint32_t seed,
                                       std::uint32_t frame) noexcept {
    std::uint64_t h = static_cast<std::uint64_t>(x);
    h = h * 0x9E3779B97F4A7C15ULL + y;
    h = h * 0xBF58476D1CE4E5B9ULL + seed;
    h = h * 0x94D049BB133111EBULL + frame;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27; h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return static_cast<std::uint32_t>(h);
}

void apply_color_op(float* rgba, const PixelProgramOp& op,
                    std::size_t pixel, std::uint32_t width,
                    std::uint32_t height) noexcept {
    float& r = rgba[0]; float& g = rgba[1];
    float& b = rgba[2]; float& a = rgba[3];
    switch (op.kind) {
    case PixelProgramOpKind::ColorMatrix: {
        const float sr = r, sg = g, sb = b, sa = a;
        r = op.params[0]*sr + op.params[1]*sg + op.params[2]*sb + op.params[3]*sa;
        g = op.params[4]*sr + op.params[5]*sg + op.params[6]*sb + op.params[7]*sa;
        b = op.params[8]*sr + op.params[9]*sg + op.params[10]*sb + op.params[11]*sa;
        break;
    }
    case PixelProgramOpKind::Opacity:
        r *= op.params[0]; g *= op.params[0]; b *= op.params[0]; a *= op.params[0];
        break;
    case PixelProgramOpKind::Vignette: {
        if (a <= 0.0f || op.params[2] <= 0.0f) break;
        const float dx = static_cast<float>(pixel % width) - static_cast<float>(width) * 0.5f;
        const float dy = static_cast<float>(pixel / width) - static_cast<float>(height) * 0.5f;
        const float width_f = static_cast<float>(width);
        const float height_f = static_cast<float>(height);
        const float diagonal = std::sqrt(width_f * width_f + height_f * height_f);
        const float inner = op.params[0] * diagonal * 0.5f;
        const float outer = inner + op.params[1] * diagonal * 0.5f;
        const float inverse_range = 1.0f / std::max(1.0f, outer - inner);
        float amount = std::clamp((std::sqrt(dx*dx + dy*dy) - inner) * inverse_range,
                                  0.0f, 1.0f);
        amount = amount * amount * (3.0f - 2.0f * amount);
        const float t = amount * op.params[2];
        r = r * (1.0f - t) + op.params[3] * t;
        g = g * (1.0f - t) + op.params[4] * t;
        b = b * (1.0f - t) + op.params[5] * t;
        break;
    }
    case PixelProgramOpKind::Noise: {
        if (a <= 0.0f || op.params[0] <= 0.0f) break;
        if (a <= 1.0e-8f) { r = g = b = a = 0.0f; break; }
        constexpr float kNoiseScale = 2.0f / 16777215.0f;
        const float inverse_alpha = 1.0f / a;
        float sr = r * inverse_alpha, sg = g * inverse_alpha, sb = b * inverse_alpha;
        const float luma = std::clamp(0.2126f*sr + 0.7152f*sg + 0.0722f*sb, 0.0f, 1.0f);
        const float response = luma <= 0.5f
            ? op.params[4] + (op.params[5] - op.params[4]) * (luma * 2.0f)
            : op.params[5] + (op.params[6] - op.params[5]) * ((luma - 0.5f) * 2.0f);
        const float grain_size = op.params[3];
        const auto nx = static_cast<std::uint32_t>(std::floor(
            static_cast<float>(pixel % width) / grain_size));
        const auto ny = static_cast<std::uint32_t>(std::floor(
            static_cast<float>(pixel / width) / grain_size));
        const auto seed = std::bit_cast<std::uint32_t>(op.params[1]);
        const auto frame = std::bit_cast<std::uint32_t>(op.params[2]);
        const auto sample = [&](std::uint32_t x) {
            return static_cast<float>(noise_hash(x, ny, seed, frame) >> 8) * kNoiseScale - 1.0f;
        };
        const float scale = op.params[0] * response;
        if (op.params[7] > 0.5f) {
            sr += sample(nx * 3u) * scale;
            sg += sample(nx * 3u + 1u) * scale;
            sb += sample(nx * 3u + 2u) * scale;
        } else {
            const float n = sample(nx) * scale; sr += n; sg += n; sb += n;
        }
        r = sr * a; g = sg * a; b = sb * a;
        break;
    }
    case PixelProgramOpKind::Levels: {
        if (a <= 0.0f) break;
        if (a <= 1.0e-8f) { r = g = b = a = 0.0f; break; }
        const float alpha = a;
        float channels[3] = {r / alpha, g / alpha, b / alpha};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::size_t base = channel * 5;
            channels[channel] = levels_channel(channels[channel], op.params[base],
                op.params[base+1], op.params[base+2], op.params[base+3], op.params[base+4]);
            channels[channel] = levels_channel(channels[channel], op.params[15],
                op.params[16], op.params[17], op.params[18], op.params[19]);
        }
        r = channels[0] * alpha; g = channels[1] * alpha; b = channels[2] * alpha;
        break;
    }
    case PixelProgramOpKind::Threshold: {
        const float alpha = std::clamp(a, 0.0f, 1.0f);
        if (alpha <= 1.0e-8f) { r = g = b = a = 0.0f; break; }
        const float luma = (0.2126f*r + 0.7152f*g + 0.0722f*b) / alpha;
        float value = 0.0f;
        if (op.params[1] <= 0.0f) value = luma >= op.params[0] ? 1.0f : 0.0f;
        else {
            const float t = std::clamp((luma - op.params[0]) / op.params[1], 0.0f, 1.0f);
            value = t * t * (3.0f - 2.0f * t);
        }
        r = g = b = value * alpha; a = alpha;
        break;
    }
    case PixelProgramOpKind::Brightness:
        if (a > 0.0f && op.params[0] != 0.0f) {
            const auto adjust = [&](float value) {
                return std::clamp((value + op.params[0] - 0.5f) * 1.0f + 0.5f,
                                  0.0f, 1.0f);
            };
            r = adjust(r); g = adjust(g); b = adjust(b);
        }
        break;
    case PixelProgramOpKind::Contrast:
        if (a > 0.0f && op.params[0] != 1.0f) {
            const auto adjust = [&](float value) {
                return std::clamp((value + 0.0f - 0.5f) * op.params[0] + 0.5f,
                                  0.0f, 1.0f);
            };
            r = adjust(r); g = adjust(g); b = adjust(b);
        }
        break;
    case PixelProgramOpKind::Saturation:
        if (a > 0.0f && op.params[0] != 1.0f) {
            const float luma = 0.2126f*r + 0.7152f*g + 0.0722f*b;
            r = std::clamp(luma + op.params[0] * (r - luma), 0.0f, 1.0f);
            g = std::clamp(luma + op.params[0] * (g - luma), 0.0f, 1.0f);
            b = std::clamp(luma + op.params[0] * (b - luma), 0.0f, 1.0f);
        }
        break;
    case PixelProgramOpKind::Exposure:
        if (a > 0.0f && op.params[0] != 0.0f) {
            auto straight = color::unpremultiply_rgb(Color{r, g, b, a});
            const float multiplier = std::exp2(op.params[0]);
            straight.r *= multiplier;
            straight.g *= multiplier;
            straight.b *= multiplier;
            const Color premultiplied = color::premultiply_rgb(straight, a);
            r = premultiplied.r; g = premultiplied.g; b = premultiplied.b;
            a = premultiplied.a;
        }
        break;
    case PixelProgramOpKind::Tint:
        if (a > 0.0f && op.params[3] > 0.0f) {
            const float amount = op.params[3];
            r = r * (1.0f - amount) + op.params[0] * amount;
            g = g * (1.0f - amount) + op.params[1] * amount;
            b = b * (1.0f - amount) + op.params[2] * amount;
        }
        break;
    case PixelProgramOpKind::Invert:
        if (a > 0.0f && op.params[0] > 0.0f) {
            r = r + op.params[0] * (1.0f - 2.0f*r);
            g = g + op.params[0] * (1.0f - 2.0f*g);
            b = b + op.params[0] * (1.0f - 2.0f*b);
        }
        break;
    case PixelProgramOpKind::Blend:
        break;
    }
}

void blend_over(float* dst, const float* src) noexcept {
    const float inverse = 1.0f - src[3];
    dst[0] = src[0] + dst[0] * inverse;
    dst[1] = src[1] + dst[1] * inverse;
    dst[2] = src[2] + dst[2] * inverse;
    dst[3] = src[3] + dst[3] * inverse;
}

} // namespace

bool PixelProgram::is_valid() const noexcept {
    if (ir_version != kPixelProgramIrVersion || abi_version != kPixelProgramAbiVersion ||
        ops.size() < 2) return false;
    std::size_t blends = 0;
    bool needs_extent = false;
    for (std::size_t index = 0; index < ops.size(); ++index) {
        const auto& op = ops[index];
        if (op.kind == PixelProgramOpKind::Blend) {
            ++blends;
            if (index + 1 != ops.size() || op.blend_mode != 0 ||
                !finite_params(op, 0)) return false;
            continue;
        }
        switch (op.kind) {
        case PixelProgramOpKind::ColorMatrix:
            if (!finite_params(op, 12)) return false;
            break;
        case PixelProgramOpKind::Opacity:
            if (!finite_params(op, 1) || op.params[0] < 0.0f || op.params[0] > 1.0f) return false;
            break;
        case PixelProgramOpKind::Vignette:
            needs_extent = true;
            if (!finite_params(op, 6) || op.params[0] < 0.0f || op.params[0] > 1.0f ||
                op.params[1] < 0.0f || op.params[1] > 1.0f || op.params[2] < 0.0f ||
                op.params[2] > 1.0f || op.params[3] < 0.0f || op.params[3] > 1.0f ||
                op.params[4] < 0.0f || op.params[4] > 1.0f || op.params[5] < 0.0f ||
                op.params[5] > 1.0f) return false;
            break;
        case PixelProgramOpKind::Noise:
            needs_extent = true;
            if (!std::isfinite(op.params[0]) || !std::isfinite(op.params[3]) ||
                !std::isfinite(op.params[4]) || !std::isfinite(op.params[5]) ||
                !std::isfinite(op.params[6]) || !std::isfinite(op.params[7]) ||
                op.params[0] < 0.0f || op.params[3] < 1.0f ||
                op.params[4] < 0.0f || op.params[5] < 0.0f || op.params[6] < 0.0f ||
                (op.params[7] != 0.0f && op.params[7] != 1.0f)) return false;
            for (std::size_t i = 8; i < op.params.size(); ++i)
                if (op.params[i] != 0.0f) return false;
            break;
        case PixelProgramOpKind::Levels:
            if (!finite_params(op, 20)) return false;
            for (std::size_t i = 0; i < 4; ++i)
                if (op.params[i*5+2] <= 0.0f) return false;
            if (op.params[17] <= 0.0f) return false;
            break;
        case PixelProgramOpKind::Threshold:
            if (!finite_params(op, 2) || op.params[0] < 0.0f || op.params[0] > 1.0f ||
                op.params[1] < 0.0f || op.params[1] > 1.0f) return false;
            break;
        case PixelProgramOpKind::Brightness:
        case PixelProgramOpKind::Contrast:
        case PixelProgramOpKind::Saturation:
        case PixelProgramOpKind::Exposure:
        case PixelProgramOpKind::Invert:
            if (!finite_params(op, 1)) return false;
            break;
        case PixelProgramOpKind::Tint:
            if (!finite_params(op, 4) || op.params[3] < 0.0f || op.params[3] > 1.0f ||
                op.params[0] < 0.0f || op.params[0] > 1.0f ||
                op.params[1] < 0.0f || op.params[1] > 1.0f ||
                op.params[2] < 0.0f || op.params[2] > 1.0f) return false;
            break;
        case PixelProgramOpKind::Blend:
            break;
        default:
            return false;
        }
    }
    if (blends != 1) return false;
    if (pixel_count == 0) return false;
    if (needs_extent && (frame_width == 0 || frame_height == 0 ||
        static_cast<std::uint64_t>(frame_width) * frame_height != pixel_count)) return false;
    if (!needs_extent && ((frame_width != 0 || frame_height != 0) &&
        static_cast<std::uint64_t>(frame_width) * frame_height != pixel_count)) return false;
    return true;
}

bool PixelProgram::certified_for_execution() const noexcept {
    if (!is_valid()) return false;
    if (determinism_contract.required != determinism::DeterminismClass::Approximate &&
        !certification.bound_to(fingerprint())) return false;
    return certification.permits(determinism_contract.required);
}

std::uint64_t PixelProgram::fingerprint() const noexcept {
    using core::hash::HashBuilder;
    HashBuilder hb;
    hb.add("chronon3d.pixel-program.v2");
    hb.add(ir_version);
    hb.add(abi_version);
    hb.add(frame_width);
    hb.add(frame_height);
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

bool PixelProgram::execute_unfused_reference(float* dst_rgba,
                                             const float* src_rgba,
                                             std::size_t pixels) const {
    if (!dst_rgba || !src_rgba || pixels == 0 ||
        pixels > std::numeric_limits<std::size_t>::max() / (4 * sizeof(float)) ||
        !is_valid() || pixels != pixel_count) return false;
    std::vector<float> current(src_rgba, src_rgba + pixels * 4);
    std::vector<float> next(pixels * 4);
    for (const auto& op : ops) {
        if (op.kind == PixelProgramOpKind::Blend) {
            for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                blend_over(dst_rgba + pixel * 4, current.data() + pixel * 4);
            continue;
        }
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            float rgba[4] = {current[pixel*4], current[pixel*4+1],
                             current[pixel*4+2], current[pixel*4+3]};
            apply_color_op(rgba, op, pixel, frame_width, frame_height);
            std::copy(rgba, rgba + 4, next.data() + pixel * 4);
        }
        current.swap(next);
    }
    return true;
}

bool PixelProgram::execute_fused_candidate(float* dst_rgba,
                                           const float* src_rgba,
                                           std::size_t pixels) const {
    if (!dst_rgba || !src_rgba || pixels == 0 ||
        pixels > std::numeric_limits<std::size_t>::max() / (4 * sizeof(float)) ||
        !is_valid() || pixels != pixel_count) return false;
    const std::vector<float> source(src_rgba, src_rgba + pixels * 4);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        float rgba[4] = {source[pixel*4], source[pixel*4+1],
                         source[pixel*4+2], source[pixel*4+3]};
        for (const auto& op : ops) {
            if (op.kind == PixelProgramOpKind::Blend) {
                blend_over(dst_rgba + pixel * 4, rgba);
                break;
            }
            apply_color_op(rgba, op, pixel, frame_width, frame_height);
        }
    }
    return true;
}

bool PixelProgram::execute(float* dst_rgba,
                           const float* src_rgba,
                           std::size_t pixels) const {
    if (!certified_for_execution()) return false;
    return execute_fused_candidate(dst_rgba, src_rgba, pixels);
}

bool certify_fusion(PixelProgram& program, const float* initial_dst_rgba,
                    const float* src_rgba, std::size_t pixels,
                    const FusionCertificationInputs& inputs) {
    program.certification = {};
    if (!initial_dst_rgba || !src_rgba || pixels == 0 ||
        pixels > std::numeric_limits<std::size_t>::max() / (4 * sizeof(float)) || !program.is_valid() ||
        pixels != program.pixel_count || inputs.plan_fingerprint.empty() ||
        inputs.asset_fingerprint.empty() || inputs.environment_fingerprint.empty() ||
        inputs.chronon_version.empty()) return false;
    std::vector<float> reference(initial_dst_rgba, initial_dst_rgba + pixels * 4);
    std::vector<float> fused = reference;
    if (!program.execute_unfused_reference(reference.data(), src_rgba, pixels) ||
        !program.execute_fused_candidate(fused.data(), src_rgba, pixels)) return false;

    const std::size_t byte_count = pixels * 4 * sizeof(float);
    const auto reference_hash = sha256_hex(std::string_view(
        reinterpret_cast<const char*>(reference.data()), byte_count));
    const auto fused_hash = sha256_hex(std::string_view(
        reinterpret_cast<const char*>(fused.data()), byte_count));
    std::string input_material;
    const auto append_field = [&input_material](std::string_view field) {
        const std::uint64_t size = field.size();
        for (unsigned byte = 0; byte < 8; ++byte)
            input_material.push_back(static_cast<char>((size >> (byte * 8U)) & 0xffU));
        input_material.append(field.data(), field.size());
    };
    const auto append_u64 = [&append_field](std::uint64_t value) {
        std::array<char, 8> bytes{};
        for (unsigned byte = 0; byte < bytes.size(); ++byte)
            bytes[byte] = static_cast<char>((value >> (byte * 8U)) & 0xffU);
        append_field(std::string_view(bytes.data(), bytes.size()));
    };
    append_field("chronon.pixel-fusion-inputs.v2");
    append_field(sha256_hex(std::string_view(
        reinterpret_cast<const char*>(initial_dst_rgba), byte_count)));
    append_field(sha256_hex(std::string_view(
        reinterpret_cast<const char*>(src_rgba), byte_count)));
    append_u64(program.fingerprint());
    append_u64(static_cast<std::uint64_t>(pixels));
    append_u64(program.frame_width); append_u64(program.frame_height);
    append_u64(inputs.frame_index); append_u64(inputs.slot_index);
    append_field(inputs.plan_fingerprint); append_field(inputs.asset_fingerprint);
    append_field(inputs.environment_fingerprint); append_field(inputs.chronon_version);
    const auto input_fingerprint = sha256_hex(input_material);
    const auto semantic_fingerprint = program.fingerprint();
    program.certification.reference_sha256 = reference_hash;
    program.certification.fused_sha256 = fused_hash;
    program.certification.environment_fingerprint = inputs.environment_fingerprint;
    program.certification.chronon_version = inputs.chronon_version;
    program.certification.comparison_input_fingerprint = input_fingerprint;
    program.certification.frame_index = inputs.frame_index;
    program.certification.slot_index = inputs.slot_index;
    program.certification.program_fingerprint = semantic_fingerprint;
    std::uint32_t max_ulp_error = 0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const float reference_value = reference[index];
        const float fused_value = fused[index];
        if (reference_value == fused_value) continue;
        if (!std::isfinite(reference_value) || !std::isfinite(fused_value)) {
            max_ulp_error = std::numeric_limits<std::uint32_t>::max();
            break;
        }
        auto ordered_bits = [](float value) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
            return (bits & 0x80000000U) != 0 ? ~bits : (bits | 0x80000000U);
        };
        const auto a = ordered_bits(reference_value);
        const auto b = ordered_bits(fused_value);
        const auto distance = a > b ? a - b : b - a;
        max_ulp_error = std::max(max_ulp_error, distance);
    }
    program.certification.max_ulp_error = max_ulp_error;
    program.certification.same_plan = !inputs.plan_fingerprint.empty();
    program.certification.same_assets = !inputs.asset_fingerprint.empty();
    program.certification.same_environment = !inputs.environment_fingerprint.empty();
    program.certification.same_chronon_version = !inputs.chronon_version.empty();
    return program.certification.bit_exact();
}

PixelProgram make_identity_pilot_program(std::size_t pixel_count) noexcept {
    const std::array<float, 12> identity_cm = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
    };
    return make_pilot_program(identity_cm, 1.0f, 0, pixel_count);
}

bool make_pointwise_stack(PixelProgram& program,
                          std::span<const PixelProgramOp> color_ops,
                          std::size_t pixel_count,
                          std::uint32_t frame_width,
                          std::uint32_t frame_height) {
    if (color_ops.empty() || pixel_count == 0 || color_ops.size() > 64) return false;
    try {
        PixelProgram candidate;
        candidate.pixel_count = pixel_count;
        candidate.frame_width = frame_width;
        candidate.frame_height = frame_height;
        candidate.ops.assign(color_ops.begin(), color_ops.end());
        candidate.ops.push_back(PixelProgramOp::blend(0));
        if (!candidate.is_valid()) return false;
        program = std::move(candidate);
        return true;
    } catch (...) {
        return false;
    }
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
