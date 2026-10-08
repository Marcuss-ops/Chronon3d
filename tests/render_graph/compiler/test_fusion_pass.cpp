#include <doctest/doctest.h>

#include <array>
#include <vector>

#include <chronon3d/render_graph/compiler/bit_exact_contract.hpp>
#include <chronon3d/render_graph/compiler/fused_pixel_program.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/simd/detail/scalar_kernels.hpp>
#include <chronon3d/simd/kernel_resolver.hpp>

namespace cg = chronon3d::graph::fusion;
namespace cd = chronon3d::graph::determinism;
namespace cs = chronon3d::simd;

namespace {
cd::FusionCertification exact_certificate(std::uint64_t fingerprint = 1) {
    cd::FusionCertification cert;
    cert.reference_sha256 = std::string(64, '0');
    cert.fused_sha256 = cert.reference_sha256;
    cert.program_fingerprint = 1;
    cert.environment_fingerprint = "test-platform";
    cert.chronon_version = "test-sha";
    cert.comparison_input_fingerprint = std::string(64, '1');
    cert.program_fingerprint = fingerprint;
    cert.max_ulp_error = 0;
    cert.same_plan = true;
    cert.same_assets = true;
    cert.same_environment = true;
    cert.same_chronon_version = true;
    return cert;
}

cg::FusedPixelProgram valid_program() {
    cg::FusedPixelProgram program;
    program.operations = {
        cg::PixelOperation::color_matrix({1,0,0,0, 0,1,0,0, 0,0,1,0}),
        cg::PixelOperation::opacity(0.5f),
        cg::PixelOperation::blend(0),
    };
    program.resolved_kernel = &cs::detail::scalar_blend;
    program.guards.math_order_preserved = true;
    program.guards.blend_mode_compatible = true;
    program.guards.dirty_rect_compatible = true;
    program.guards.precision_compatible = true;
    program.pixel_count = 1920u * 1080u;
    return program;
}
} // namespace

TEST_CASE("BitExactContract: 1 ULP is not BitExact") {
    cd::FusionCertification cert = exact_certificate();
    cert.fused_sha256 = std::string(64, '2');
    cert.max_ulp_error = 1;

    CHECK_FALSE(cert.bit_exact());
    CHECK(cert.deterministic_within_platform());
    CHECK_FALSE(cert.permits(cd::DeterminismClass::BitExact));
    CHECK(cert.permits(cd::DeterminismClass::DeterministicWithinPlatform));
}

TEST_CASE("BitExactContract: equal output hashes certify exactness") {
    const auto cert = exact_certificate();
    CHECK(cert.bit_exact());
    CHECK(cert.permits(cd::DeterminismClass::BitExact));
}

TEST_CASE("FusedPixelProgram: BitExact is fail-closed without certificate") {
    auto program = valid_program();
    CHECK_FALSE(program.certified_for_execution());
    CHECK(program.bytes_saved() == 0);

    float dst[4]{};
    const float src[4]{};
    CHECK_FALSE(program.execute(dst, src, 1));
}

TEST_CASE("FusedPixelProgram: exact certificate enables certified execution") {
    auto program = valid_program();
    program.certification = exact_certificate(program.fingerprint());
    CHECK(program.certified_for_execution());
    CHECK(program.bytes_saved() == 3u * 1920u * 1080u * 16u);

    float dst[] = {0.1f, 0.2f, 0.3f, 0.4f};
    const float src[] = {0.8f, 0.6f, 0.4f, 0.5f};
    CHECK(program.execute(dst, src, 1));
    CHECK(dst[0] == doctest::Approx(0.4f + 0.1f * 0.75f));
    CHECK(dst[1] == doctest::Approx(0.3f + 0.2f * 0.75f));
    CHECK(dst[2] == doctest::Approx(0.2f + 0.3f * 0.75f));
    CHECK(dst[3] == doctest::Approx(0.25f + 0.4f * 0.75f));
}

TEST_CASE("FusedPixelProgram: structural guards remain independently fail-closed") {
    auto program = valid_program();
    program.certification = exact_certificate(program.fingerprint());
    program.guards.dirty_rect_compatible = false;
    CHECK_FALSE(program.certified_for_execution());
}

TEST_CASE("FusionStats: default construct is zero") {
    cg::FusionStats stats;
    CHECK(stats.passes_before_fusion == 0);
    CHECK(stats.passes_after_fusion == 0);
    CHECK(stats.bytes_saved_by_fusion == 0);
}

TEST_CASE("Pixel fusion discovery does not report execution or savings") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    RenderGraph graph;
    auto root = graph.add_node(std::make_unique<EffectStackNode>(
        EffectStack{}, no_cache("fusion-test")));
    EffectStack color_stack;
    color_stack.emplace_back(effects::EffectDescriptor{.id = "color_matrix"}, BrightnessParams{0.1f});
    auto color = graph.add_node(std::make_unique<AdjustmentNode>(
        std::move(color_stack), no_cache("fusion-test")));
    EffectStack opacity_stack;
    opacity_stack.emplace_back(effects::EffectDescriptor{.id = "opacity"}, BrightnessParams{0.2f});
    auto opacity = graph.add_node(std::make_unique<AdjustmentNode>(
        std::move(opacity_stack), no_cache("fusion-test")));
    EffectStack blend_stack;
    blend_stack.emplace_back(effects::EffectDescriptor{.id = "blend"}, BrightnessParams{0.3f});
    auto blend = graph.add_node(std::make_unique<AdjustmentNode>(
        std::move(blend_stack), no_cache("fusion-test")));
    graph.connect(root, color);
    graph.connect(color, opacity);
    graph.connect(opacity, blend);
    graph.set_output(blend);

    RenderGraphContext context;
    context.frame_input.width = 16;
    context.frame_input.height = 16;
    std::vector<cg::FusedPixelProgram> candidates;
    const auto capabilities = cs::detect_cpu_capabilities();
    const auto& kernels = cs::resolve_pixel_kernels(capabilities);
    const auto stats = cg::fuse_color_opacity_blend(graph, context, kernels, candidates);

    CHECK(candidates.size() == 1);
    CHECK(stats.passes_before_fusion == 0);
    CHECK(stats.passes_after_fusion == 0);
    CHECK(stats.bytes_saved_by_fusion == 0);
    CHECK(graph.live_count() == 4);
    CHECK_FALSE(candidates.front().certified_for_execution());
}
