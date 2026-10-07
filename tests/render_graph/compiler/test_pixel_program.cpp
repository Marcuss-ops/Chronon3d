#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <vector>

#include <chronon3d/render_graph/compiler/bit_exact_contract.hpp>
#include <chronon3d/render_graph/compiler/pixel_program.hpp>
#include <chronon3d/render_graph/compiler/device_program.hpp>
#include <chronon3d/cache/compiled_artifact_cache.hpp>
#include <chronon3d/cache/cache_taxonomy.hpp>
#include <chronon3d/simd/detail/scalar_kernels.hpp>

namespace pp = chronon3d::graph::pixel;
namespace cd = chronon3d::graph::determinism;
namespace cs = chronon3d::simd;

namespace {
cd::FusionCertification exact_cert() {
    cd::FusionCertification c;
    c.reference_sha256 = std::string(64, '0');
    c.fused_sha256 = c.reference_sha256;
    c.environment_fingerprint = "test-platform";
    c.chronon_version = "test-sha";
    c.comparison_input_fingerprint = std::string(64, '1');
    c.max_ulp_error = 0;
    c.same_plan = true;
    c.same_assets = true;
    c.same_environment = true;
    c.same_chronon_version = true;
    return c;
}
cd::FusionCertification bound_cert(const pp::PixelProgram& program) {
    auto certificate = exact_cert();
    certificate.program_fingerprint = program.fingerprint();
    return certificate;
}
} // namespace

// ── ProgramCache taxonomy lock ──────────────────────────────────────────────

static_assert(
    chronon3d::cache::cache_family_annotation<chronon3d::cache::CacheFamily::ProgramCache>,
    "PixelProgram/DeviceProgram belong to ProgramCache");

// ── PixelProgram v2: IR + fingerprint + validity ────────────────────────────

TEST_CASE("PixelProgram v2: IR and ABI version constants are 2") {
    CHECK(pp::kPixelProgramIrVersion == 2);
    CHECK(pp::kPixelProgramAbiVersion == 2);
}

TEST_CASE("PixelProgram v2: identity compatibility triple is valid") {
    auto p = pp::make_identity_pilot_program(1920u * 1080u);
    CHECK(p.is_valid());
    CHECK(pp::is_canonical_pilot_triple(p));
    CHECK(p.ops.size() == 3);
    CHECK(p.ir_version == pp::kPixelProgramIrVersion);
    CHECK(p.abi_version == pp::kPixelProgramAbiVersion);
}

TEST_CASE("PixelProgram v2: fingerprint is deterministic and discriminates") {
    auto a = pp::make_identity_pilot_program(100);
    auto b = pp::make_identity_pilot_program(100);
    CHECK(a.fingerprint() == b.fingerprint());

    // opacity differs → fingerprint differs
    std::array<float, 12> cm = {1,0,0,0, 0,1,0,0, 0,0,1,0};
    auto c = pp::make_pilot_program(cm, 0.5f, 0, 100);
    CHECK(a.fingerprint() != c.fingerprint());

    // determinism class is folded into fingerprint
    auto d = pp::make_identity_pilot_program(100);
    d.determinism_contract.required = cd::DeterminismClass::Approximate;
    CHECK(a.fingerprint() != d.fingerprint());
}

TEST_CASE("PixelProgram v2: certification does NOT affect fingerprint") {
    auto a = pp::make_identity_pilot_program(42);
    auto b = pp::make_identity_pilot_program(42);
    b.certification = bound_cert(b);
    CHECK(a.fingerprint() == b.fingerprint());
}

TEST_CASE("PixelProgram v2: invalid programs are rejected") {
    auto p = pp::make_identity_pilot_program(10);
    CHECK(p.is_valid());

    // wrong ir_version → invalid
    auto q = p;
    q.ir_version = 999;
    CHECK_FALSE(q.is_valid());
    CHECK_FALSE(pp::is_canonical_pilot_triple(q));

    // wrong arity → invalid
    auto r = p;
    r.ops.pop_back();
    CHECK_FALSE(r.is_valid());

    // opacity outside [0,1] → invalid
    std::array<float, 12> cm = {1,0,0,0, 0,1,0,0, 0,0,1,0};
    auto s = pp::make_pilot_program(cm, 1.5f, 0, 10);
    CHECK_FALSE(s.is_valid());
}

TEST_CASE("PixelProgram v2: BitExact is fail-closed without certificate") {
    auto p = pp::make_identity_pilot_program(4);
    CHECK_FALSE(p.certified_for_execution());
    float dst[16]{}, src[16]{};
    for (int i = 0; i < 16; ++i) src[i] = 0.5f;
    CHECK_FALSE(p.execute(dst, src, 4));
}

TEST_CASE("PixelProgram v2: exact certificate enables execution") {
    auto p = pp::make_identity_pilot_program(1);
    p.certification = bound_cert(p);
    CHECK(p.certified_for_execution());
    float dst[4]{0.1f, 0.2f, 0.3f, 0.4f};
    const float src[4]{0.8f, 0.6f, 0.4f, 0.5f};
    CHECK(p.execute(dst, src, 1));
    // identity ColorMatrix + opacity 1.0 + blend(0) → src blended over dst
    // scalar_blend: dst = src*alpha_src + dst*(1-alpha_src) with src alpha = src[3]
    // our execute path: color_matrix identity, then opacity 1.0 (no-op), then blend
    // blend kernel itself blends transformed src over dst. For identity we get
    // a deterministic result — just check it executed and changed dst.
    CHECK(dst[3] != doctest::Approx(0.4f));
}

TEST_CASE("PixelProgram v2: 1 ULP is not BitExact") {
    auto p = pp::make_identity_pilot_program(1);
    auto c = bound_cert(p);
    c.fused_sha256 = std::string(64, '2');
    c.max_ulp_error = 1;
    p.certification = c;
    CHECK_FALSE(p.certification.bit_exact());
    CHECK(p.certification.deterministic_within_platform());
    CHECK_FALSE(p.certified_for_execution()); // BitExact requires bit_exact()
}

TEST_CASE("PixelProgram v2: bytes_saved is gated by exact certification") {
    auto p = pp::make_identity_pilot_program(10);
    CHECK(p.bytes_saved() == 0);
    p.certification = bound_cert(p);
    CHECK(p.bytes_saved() > 0);
}

TEST_CASE("PixelProgram v2: all requested color opcodes validate; temporal/spatial opcodes do not exist") {
    using K = pp::PixelProgramOpKind;
    CHECK(static_cast<unsigned>(K::Vignette) == 3);
    CHECK(static_cast<unsigned>(K::Noise) == 4);
    CHECK(static_cast<unsigned>(K::Levels) == 5);
    CHECK(static_cast<unsigned>(K::Threshold) == 6);
    CHECK(static_cast<unsigned>(K::Brightness) == 7);
    CHECK(static_cast<unsigned>(K::Contrast) == 8);
    CHECK(static_cast<unsigned>(K::Saturation) == 9);
    CHECK(static_cast<unsigned>(K::Exposure) == 10);
    CHECK(static_cast<unsigned>(K::Tint) == 11);
    CHECK(static_cast<unsigned>(K::Invert) == 12);
    const std::array<float, 20> levels{
        0,1,1,0,1, 0,1,1,0,1, 0,1,1,0,1, 0,1,1,0,1};
    const std::array<pp::PixelProgramOp, 11> ops{
        pp::PixelProgramOp::vignette(0.4f,0.3f,0.5f,0,0,0),
        pp::PixelProgramOp::noise({0.1f,std::bit_cast<float>(std::uint32_t{9}),
            std::bit_cast<float>(std::uint32_t{3}),2.0f,1,1,1,0}),
        pp::PixelProgramOp::levels(levels), pp::PixelProgramOp::threshold(0.5f,0.1f),
        pp::PixelProgramOp::brightness(0.1f), pp::PixelProgramOp::contrast(1.2f),
        pp::PixelProgramOp::saturation(0.8f), pp::PixelProgramOp::exposure(0.2f),
        pp::PixelProgramOp::tint(0.1f,0.2f,0.3f,0.5f), pp::PixelProgramOp::invert(0.5f),
        pp::PixelProgramOp::opacity(0.9f)};
    pp::PixelProgram program;
    REQUIRE(pp::make_pointwise_stack(program, ops, 16, 4, 4));
    CHECK(program.is_valid());
    CHECK(program.ops.size() == 12);
    const std::array blend_only{pp::PixelProgramOp(K::Blend)};
    CHECK_FALSE(pp::make_pointwise_stack(program, blend_only, 1));
    CHECK(program.is_valid()); // failed builder leaves prior valid program intact
}

TEST_CASE("PixelProgram v2: comparison harness binds same inputs/frame/slot and exact SHA-256") {
    pp::PixelProgram program;
    const std::array ops{pp::PixelProgramOp::brightness(0.05f),
                         pp::PixelProgramOp::invert(0.2f)};
    REQUIRE(pp::make_pointwise_stack(program, ops, 4));
    float dst_reference[16]{0.1f,0.2f,0.3f,0.8f, 0.3f,0.1f,0.2f,0.6f,
                            0.2f,0.4f,0.1f,0.9f, 0.5f,0.2f,0.1f,1.0f};
    const float dst_before[16]{0.1f,0.2f,0.3f,0.8f, 0.3f,0.1f,0.2f,0.6f,
                               0.2f,0.4f,0.1f,0.9f, 0.5f,0.2f,0.1f,1.0f};
    const float source[16]{0.4f,0.3f,0.2f,0.8f, 0.2f,0.5f,0.1f,0.5f,
                           0.1f,0.2f,0.6f,0.7f, 0.2f,0.3f,0.5f,0.9f};
    pp::FusionCertificationInputs inputs{"plan-12", "asset-34", "cpu-test", "sha-test", 17, 2};
    REQUIRE(pp::certify_fusion(program, dst_reference, source, 4, inputs));
    CHECK(program.certification.bit_exact());
    CHECK(program.certification.reference_sha256.size() == 64);
    CHECK(program.certification.reference_sha256 == program.certification.fused_sha256);
    float out[16]; std::copy(std::begin(dst_before), std::end(dst_before), out);
    CHECK(program.execute(out, source, 4));
    CHECK(std::equal(std::begin(out), std::end(out), std::begin(dst_reference)));
}

TEST_CASE("PixelProgram v2: 1 ULP-equivalent evidence never passes BitExact") {
    auto program = pp::make_identity_pilot_program(1);
    auto certificate = bound_cert(program);
    certificate.reference_sha256 = std::string(64, '2');
    certificate.fused_sha256 = std::string(64, '3');
    certificate.max_ulp_error = 1;
    CHECK_FALSE(certificate.permits(cd::DeterminismClass::BitExact));
    CHECK(certificate.permits(cd::DeterminismClass::DeterministicWithinPlatform));
}

// ── DeviceProgramKey: stable_name + ProgramCache mapping ────────────────────

TEST_CASE("DeviceProgramKey: stable_name is deterministic and target-sensitive") {
    auto prog = pp::make_identity_pilot_program(100);
    prog.certification = bound_cert(prog);
    auto k_cpu = pp::make_device_program_key(prog, pp::DeviceTarget::CpuSimd);
    auto k_spirv = pp::make_device_program_key(prog, pp::DeviceTarget::SpirV);
    auto k_ptx = pp::make_device_program_key(prog, pp::DeviceTarget::Ptx);
    auto k_cpu2 = pp::make_device_program_key(prog, pp::DeviceTarget::CpuSimd);
    CHECK(k_cpu.stable_name() == k_cpu2.stable_name());
    CHECK(k_cpu.stable_name() != k_spirv.stable_name());
    CHECK(k_cpu.stable_name() != k_ptx.stable_name());
    CHECK(k_spirv.stable_name() != k_ptx.stable_name());
    // program fingerprint is distinct from device key (different prefix)
    CHECK(k_cpu.stable_name().rfind("device-program-v1-", 0) == 0);
}

TEST_CASE("DeviceProgramKey: determinism class is part of key") {
    auto prog_be = pp::make_identity_pilot_program(100);
    prog_be.certification = bound_cert(prog_be);
    prog_be.determinism_contract.required = cd::DeterminismClass::BitExact;
    auto prog_approx = pp::make_identity_pilot_program(100);
    prog_approx.determinism_contract.required = cd::DeterminismClass::Approximate;
    auto k_be = pp::make_device_program_key(prog_be, pp::DeviceTarget::CpuSimd);
    auto k_ap = pp::make_device_program_key(prog_approx, pp::DeviceTarget::CpuSimd);
    CHECK(k_be.stable_name() != k_ap.stable_name());
}

TEST_CASE("DeviceProgramKey: specialization and compiler_version are part of key") {
    auto prog = pp::make_identity_pilot_program(50);
    prog.certification = bound_cert(prog);
    auto k0 = pp::make_device_program_key(prog, pp::DeviceTarget::CpuSimd);
    auto k1 = pp::make_device_program_key(prog, pp::DeviceTarget::CpuSimd, "specialized");
    auto k2 = pp::make_device_program_key(prog, pp::DeviceTarget::CpuSimd, "", "2.0");
    CHECK(k0.stable_name() != k1.stable_name());
    CHECK(k0.stable_name() != k2.stable_name());
}

TEST_CASE("DeviceProgramKey: to_program_artifact_key maps into ProgramCache stably") {
    auto prog = pp::make_identity_pilot_program(64);
    prog.certification = bound_cert(prog);
    auto key = pp::make_device_program_key(prog, pp::DeviceTarget::CpuSimd, "", "1.2.3", "sm_86");
    auto pak = key.to_program_artifact_key();
    auto pak2 = key.to_program_artifact_key();
    CHECK(pak.stable_name() == pak2.stable_name());
    CHECK(pak.semantic_program_fingerprint == prog.fingerprint());
    CHECK(pak.target_backend == "cpu");
    CHECK(pak.lowering_kind == "cpu-simd");
    CHECK(pak.stable_name().rfind("program-v1-", 0) == 0);
}

TEST_CASE("DeviceProgramKey: device_capability participates in both key and artifact key") {
    auto prog = pp::make_identity_pilot_program(64);
    auto k_a = pp::make_device_program_key(prog, pp::DeviceTarget::Ptx, "", "", "sm_86");
    auto k_b = pp::make_device_program_key(prog, pp::DeviceTarget::Ptx, "", "", "sm_89");
    CHECK(k_a.stable_name() != k_b.stable_name());
    CHECK(k_a.to_program_artifact_key().stable_name() !=
          k_b.to_program_artifact_key().stable_name());
}

TEST_CASE("DeviceTarget: string round-trip") {
    CHECK(pp::to_string(pp::DeviceTarget::CpuSimd) == "cpu-simd");
    CHECK(pp::to_string(pp::DeviceTarget::SpirV) == "spirv");
    CHECK(pp::to_string(pp::DeviceTarget::Ptx) == "ptx");
    CHECK(pp::device_target_from_string("cpu-simd") == pp::DeviceTarget::CpuSimd);
    CHECK(pp::device_target_from_string("spirv") == pp::DeviceTarget::SpirV);
    CHECK(pp::device_target_from_string("ptx") == pp::DeviceTarget::Ptx);
    CHECK_FALSE(pp::device_target_from_string("sass").has_value());
}

// ── Lowering: CPU is executable, GPU targets are reserved ───────────────────

TEST_CASE("lower_to_cpu: fail-closed BitExact without certificate") {
    auto prog = pp::make_identity_pilot_program(4);
    auto r = pp::lower_to_cpu(prog);
    CHECK(r.status == pp::LoweringStatus::UncertifiedBitExact);
    CHECK(r.payload.empty());
}

TEST_CASE("lower_to_cpu: certified program lowers and executes") {
    auto prog = pp::make_identity_pilot_program(1);
    prog.certification = bound_cert(prog);
    auto r = pp::lower_to_cpu(prog);
    REQUIRE(r.ok());
    CHECK_FALSE(r.payload.empty());
    float dst[4]{0.1f, 0.2f, 0.3f, 0.4f};
    const float src[4]{0.8f, 0.6f, 0.4f, 0.5f};
    CHECK(pp::execute_lowered_cpu(r.device_program, dst, src, 1));
    CHECK_FALSE(pp::execute_lowered_cpu(r.payload, dst, src, 1)); // raw payload has no BitExact evidence
    auto tampered = r.device_program;
    CHECK_FALSE(pp::execute_lowered_cpu(tampered, dst, src, 1));
    CHECK(r.device_program.valid());
}

TEST_CASE("lower_to_cpu: pointwise v2 program remains fail-closed until stack payload ABI exists") {
    pp::PixelProgram program;
    const std::array ops{pp::PixelProgramOp::brightness(0.1f),
                         pp::PixelProgramOp::invert(0.2f)};
    REQUIRE(pp::make_pointwise_stack(program, ops, 1));
    program.certification = bound_cert(program);
    const auto result = pp::lower_to_cpu(program);
    CHECK(result.status == pp::LoweringStatus::UnsupportedTarget);
    CHECK(result.payload.empty());
    CHECK(result.device_program.payload().empty());
    CHECK_FALSE(result.device_program.valid());
}

TEST_CASE("lower_to_cpu: invalid program is rejected") {
    pp::PixelProgram empty;
    auto r = pp::lower_to_cpu(empty);
    CHECK(r.status == pp::LoweringStatus::InvalidProgram);
}

TEST_CASE("lower_to_cpu: corrupt payload fails closed") {
    std::vector<std::uint8_t> corrupt = {0, 1, 2, 3};
    float dst[4]{}, src[4]{};
    CHECK_FALSE(pp::execute_lowered_cpu(corrupt, dst, src, 1));
    CHECK_FALSE(pp::execute_lowered_cpu(std::span<const std::uint8_t>{}, dst, src, 1));
    auto approximate = pp::make_identity_pilot_program(1);
    approximate.determinism_contract.required = cd::DeterminismClass::Approximate;
    auto lowering = pp::lower_to_cpu(approximate);
    REQUIRE(lowering.ok());
    CHECK(pp::execute_lowered_cpu(lowering.payload, dst, src, 1));
}

TEST_CASE("lower_to_spirv / lower_to_ptx: reserved until SLANG ENTRY GATE") {
    auto prog = pp::make_identity_pilot_program(4);
    prog.certification = bound_cert(prog);
    auto s = pp::lower_to_spirv(prog);
    auto p = pp::lower_to_ptx(prog);
    CHECK(s.status == pp::LoweringStatus::UnsupportedTarget);
    CHECK(p.status == pp::LoweringStatus::UnsupportedTarget);
    CHECK(s.payload.empty());
    CHECK(p.payload.empty());
}
