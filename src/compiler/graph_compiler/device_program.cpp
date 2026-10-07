#include <chronon3d/render_graph/compiler/device_program.hpp>

#include <chronon3d/core/hash/hash_builder.hpp>

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::graph::pixel {

namespace {

constexpr std::array<std::uint8_t, 4> kCpuPayloadMagic = {'C', 'P', 'I', 'X'};
constexpr std::uint32_t kCpuPayloadVersion = 1;
constexpr std::size_t kCpuPayloadSize =
    4 + 4 + 4 + 4 + 12 * sizeof(float) + sizeof(float) + 1 + 3;

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

[[nodiscard]] std::string_view target_lowering_kind(DeviceTarget target) noexcept {
    switch (target) {
    case DeviceTarget::CpuSimd: return "cpu-simd";
    case DeviceTarget::SpirV: return "slang-spirv";
    case DeviceTarget::Ptx: return "slang-ptx";
    }
    return "unknown";
}

[[nodiscard]] std::string_view target_backend(DeviceTarget target) noexcept {
    switch (target) {
    case DeviceTarget::CpuSimd: return kCpuArtifactBackend;
    case DeviceTarget::SpirV: return kGpuArtifactBackendA;
    case DeviceTarget::Ptx: return kGpuArtifactBackendB;
    }
    return "unknown";
}

[[nodiscard]] std::vector<std::uint8_t> encode_cpu_payload(
    const PixelProgram& program) {
    std::vector<std::uint8_t> out;
    out.resize(kCpuPayloadSize, 0);
    std::uint8_t* p = out.data();
    std::memcpy(p, kCpuPayloadMagic.data(), 4);
    p += 4;
    write_u32_le(p, kCpuPayloadVersion);
    p += 4;
    write_u32_le(p, program.ir_version);
    p += 4;
    write_u32_le(p, program.abi_version);
    p += 4;
    // ops[0] ColorMatrix params[12]
    std::memcpy(p, program.ops[0].params.data(), 12 * sizeof(float));
    p += 12 * sizeof(float);
    // ops[1] Opacity params[0]
    const float alpha = program.ops[1].params[0];
    std::memcpy(p, &alpha, sizeof(float));
    p += sizeof(float);
    p[0] = program.ops[2].blend_mode;
    p[1] = static_cast<std::uint8_t>(program.determinism_contract.required);
    p[2] = program.certification.bit_exact() ? 1 : 0;
    p[3] = 0;
    return out;
}

[[nodiscard]] bool decode_and_execute_cpu_payload(
    std::span<const std::uint8_t> payload,
    const determinism::FusionCertification& certification,
    std::optional<std::uint64_t> expected_fingerprint,
    float* dst_rgba,
    const float* src_rgba,
    std::size_t pixels) {
    if (!dst_rgba || !src_rgba || pixels == 0) return false;
    if (payload.size() != kCpuPayloadSize) return false;
    const std::uint8_t* p = payload.data();
    if (std::memcmp(p, kCpuPayloadMagic.data(), 4) != 0) return false;
    p += 4;
    if (read_u32_le(p) != kCpuPayloadVersion) return false;
    p += 4;
    const std::uint32_t ir_version = read_u32_le(p);
    p += 4;
    const std::uint32_t abi_version = read_u32_le(p);
    p += 4;
    if (ir_version != kPixelProgramIrVersion) return false;
    if (abi_version != kPixelProgramAbiVersion) return false;

    std::array<float, 12> cm{};
    std::memcpy(cm.data(), p, 12 * sizeof(float));
    p += 12 * sizeof(float);
    float alpha{};
    std::memcpy(&alpha, p, sizeof(float));
    p += sizeof(float);
    const std::uint8_t blend_mode = p[0];
    const std::uint8_t det_raw = p[1];
    const std::uint8_t certified_flag = p[2];
    if (blend_mode != 0) return false;
    if (!(alpha >= 0.0f && alpha <= 1.0f)) return false;
    if (det_raw > 2) return false;
    const auto required = static_cast<determinism::DeterminismClass>(det_raw);
    if (certified_flag > 1 ||
        (required == determinism::DeterminismClass::BitExact && certified_flag == 0))
        return false;

    // Raw payloads have no attached evidence, so they are executable only
    // under the explicitly non-certifying Approximate contract.
    if (        !expected_fingerprint && required != determinism::DeterminismClass::Approximate)
        return false;
    if (expected_fingerprint &&
        required != determinism::DeterminismClass::Approximate &&
        !certification.bound_to(*expected_fingerprint))
        return false;
    if (!expected_fingerprint && required == determinism::DeterminismClass::Approximate) {
        // Approximate raw payload execution has no attached evidence.
    } else if (!certification.permits(required)) {
        return false;
    }

    PixelProgram program = make_pilot_program(cm, alpha, blend_mode, pixels);
    program.determinism_contract.required = required;
    if (expected_fingerprint && program.fingerprint() != *expected_fingerprint)
        return false;
    program.certification = certification;
    return program.execute(dst_rgba, src_rgba, pixels);
}

} // namespace

std::string_view to_string(DeviceTarget target) noexcept {
    switch (target) {
    case DeviceTarget::CpuSimd: return "cpu-simd";
    case DeviceTarget::SpirV: return "spirv";
    case DeviceTarget::Ptx: return "ptx";
    }
    return "unknown";
}

std::optional<DeviceTarget> device_target_from_string(
    std::string_view value) noexcept {
    if (value == "cpu-simd") return DeviceTarget::CpuSimd;
    if (value == "spirv") return DeviceTarget::SpirV;
    if (value == "ptx") return DeviceTarget::Ptx;
    return std::nullopt;
}

bool DeviceProgramKey::operator==(const DeviceProgramKey& other) const noexcept {
    return pixel_program_fingerprint == other.pixel_program_fingerprint &&
           target == other.target &&
           determinism_class == other.determinism_class &&
           specialization == other.specialization &&
           compiler_version == other.compiler_version &&
           backend_abi_version == other.backend_abi_version &&
           device_capability == other.device_capability;
}

std::string DeviceProgramKey::stable_name() const {
    using core::hash::HashBuilder;
    const auto digest = HashBuilder{}
        .add("chronon3d.device-program-key.v1")
        .add(pixel_program_fingerprint)
        .add_enum(target)
        .add_enum(determinism_class)
        .add(specialization)
        .add(compiler_version)
        .add(backend_abi_version)
        .add(device_capability)
        .finish();
    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(digest));
    return "device-program-v1-" + std::string(buffer);
}

cache::ProgramArtifactKey DeviceProgramKey::to_program_artifact_key() const {
    return {
        pixel_program_fingerprint,
        std::string(target_lowering_kind(target)),
        std::string(target_backend(target)),
        device_capability,
        "chronon-device-program",
        compiler_version.empty() ? "1" : compiler_version,
        specialization,
        backend_abi_version,
    };
}

DeviceProgramKey make_device_program_key(
    const PixelProgram& program,
    DeviceTarget target,
    std::string specialization,
    std::string compiler_version,
    std::string device_capability) noexcept {
    DeviceProgramKey key;
    key.pixel_program_fingerprint = program.fingerprint();
    key.target = target;
    key.determinism_class = program.determinism_contract.required;
    key.specialization = std::move(specialization);
    key.compiler_version = std::move(compiler_version);
    key.backend_abi_version = program.abi_version;
    key.device_capability = std::move(device_capability);
    return key;
}

LoweringResult lower_to_cpu(const PixelProgram& program) noexcept {
    if (!program.is_valid()) {
        return {LoweringStatus::InvalidProgram,
                "PixelProgram v2 is malformed or violates the supported operation contracts", {}};
    }
    if (!program.certified_for_execution()) {
        return {LoweringStatus::UncertifiedBitExact,
                "BitExact PixelProgram requires an exact FusionCertification", {}};
    }
    if (!is_canonical_pilot_triple(program)) {
        return {LoweringStatus::UnsupportedTarget,
                "CPU DeviceProgram payload ABI currently supports only the v1 pilot triple; v2 stack lowering remains fail-closed", {}};
    }
    LoweringResult result;
    result.status = LoweringStatus::Ok;
    result.payload = encode_cpu_payload(program);
    result.device_program.key_ = make_device_program_key(program, DeviceTarget::CpuSimd);
    result.device_program.payload_ = result.payload;
    result.device_program.certification_ = program.certification;
    return result;
}

bool execute_lowered_cpu(
    const DeviceProgram& device_program,
    float* dst_rgba,
    const float* src_rgba,
    std::size_t pixels) noexcept {
    if (device_program.key_.target != DeviceTarget::CpuSimd ||
        device_program.key_.backend_abi_version != kPixelProgramAbiVersion ||
        (device_program.key_.determinism_class != determinism::DeterminismClass::Approximate &&
         !device_program.certification_.bound_to(
             device_program.key_.pixel_program_fingerprint)) ||
        !device_program.certification_.permits(device_program.key_.determinism_class))
        return false;
    return decode_and_execute_cpu_payload(device_program.payload_,
        device_program.certification_, device_program.key_.pixel_program_fingerprint,
        dst_rgba, src_rgba, pixels);
}

bool execute_lowered_cpu(
    std::span<const std::uint8_t> payload,
    float* dst_rgba,
    const float* src_rgba,
    std::size_t pixels) noexcept {
    return decode_and_execute_cpu_payload(payload, {}, std::nullopt,
                                          dst_rgba, src_rgba, pixels);
}

LoweringResult lower_to_spirv(const PixelProgram& program) noexcept {
    (void)program;
    return {LoweringStatus::UnsupportedTarget,
            "SPIR-V lowering is reserved until the PixelProgram v2 device-kernel entry gate", {}};
}

LoweringResult lower_to_ptx(const PixelProgram& program) noexcept {
    (void)program;
    return {LoweringStatus::UnsupportedTarget,
            "PTX lowering is reserved until SPIR-V lowering is certified", {}};
}

} // namespace chronon3d::graph::pixel
