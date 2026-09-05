// test_backend_registry.cpp — backend registry selection policy tests.
// Vulkan device/ops tests live in test_backend_registry_vulkan.cpp,
// plan/batch in test_backend_registry_plan.cpp and
// test_backend_registry_batches.cpp, parity in
// test_backend_registry_parity.cpp, surface identity in
// test_backend_registry_surfaces.cpp.
#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <chronon3d/render_graph/backend_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/overlay_template.hpp>
#include <chronon3d/render_graph/checkbackend.hpp>
#include <tests/helpers/command_plan_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <tests/helpers/cpu_gpu_parity.hpp>
#include <tests/helpers/gpu_readiness_gate.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#endif

#include "../../src/render_graph/nodes/text_run/gpu_text_run.hpp"

namespace {

class StubBackend final : public chronon3d::graph::RenderBackend {
public:
    void apply_per_pixel_dof(chronon3d::Framebuffer&, std::span<const float>,
                             const chronon3d::DepthOfFieldSettings&,
                             const chronon3d::LensModel&,
                             const std::optional<chronon3d::raster::BBox>&) override {}
    chronon3d::graph::RenderOpResult draw_node(
        chronon3d::Framebuffer&, const chronon3d::RenderNode&,
        const chronon3d::RenderState&, const chronon3d::Camera&, int, int) override {
        return chronon3d::graph::RenderOpResult{chronon3d::graph::RenderOpOutcome{}};
    }
    void apply_effect_stack(chronon3d::Framebuffer&, const chronon3d::EffectStack&,
                            const chronon3d::effects::EffectExecutionContext&) override {}
    void composite_layer(chronon3d::Framebuffer&, const chronon3d::Framebuffer&,
                         chronon3d::BlendMode,
                         const std::optional<chronon3d::raster::BBox>&,
                         chronon3d::CompositeOperator) override {}
    void apply_blur(chronon3d::Framebuffer&, float,
                    const std::optional<chronon3d::raster::BBox>&) override {}
};

} // namespace

TEST_CASE("backend registry rejects duplicates and resolves explicit preferences") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    const auto software_factory = [] { return std::make_unique<StubBackend>(); };

    CHECK(registry.register_backend(BackendType::Software,
                                    BackendCapabilities{true, false, false, 4096, 4096, 0},
                                    software_factory));
    CHECK_FALSE(registry.register_backend(BackendType::Software,
                                          BackendCapabilities{}, software_factory));
    CHECK(registry.contains(BackendType::Software));
    CHECK_FALSE(registry.contains(BackendType::Vulkan));

    BackendResolver resolver(registry);
    auto explicit_cpu = resolver.resolve(BackendPreference::Software);
    REQUIRE(explicit_cpu.ok());
    CHECK(dynamic_cast<StubBackend*>(explicit_cpu.value().get()) != nullptr);

    auto strict_gpu = resolver.resolve(BackendPreference::GPU);
    CHECK_FALSE(strict_gpu.ok());
    CHECK(strict_gpu.error().code == BackendResolveErrorCode::NotRegistered);
}

TEST_CASE("auto prefers Vulkan and falls back to software") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    CHECK(registry.register_backend(BackendType::Software, BackendCapabilities{true},
                                    [] { return std::make_unique<StubBackend>(); }));

    BackendResolver resolver(registry);
    auto auto_backend = resolver.resolve(BackendPreference::Auto);
    REQUIRE(auto_backend.ok());
    CHECK(dynamic_cast<StubBackend*>(auto_backend.value().get()) != nullptr);
}

TEST_CASE("requirements are checked before factory invocation") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    CHECK(registry.register_backend(BackendType::Vulkan,
                                    BackendCapabilities{true, true, false, 8192, 8192, 1ull << 30},
                                    [] { return std::make_unique<StubBackend>(); }));
    BackendResolver resolver(registry);

    auto missing_compute = resolver.resolve(BackendPreference::GPU,
                                            BackendRequirements{true, true, true});
    CHECK_FALSE(missing_compute.ok());
    CHECK(missing_compute.error().code == BackendResolveErrorCode::NoMatchingBackend);

    auto supported = resolver.resolve(BackendPreference::GPU,
                                      BackendRequirements{true, true, false, 1920, 1080});
    CHECK(supported.ok());
}

TEST_CASE("factory exceptions become structured resolution failures") {
    using namespace chronon3d::graph;
    BackendRegistry registry;
    CHECK(registry.register_backend(BackendType::Vulkan, BackendCapabilities{true}, []() -> std::unique_ptr<RenderBackend> {
        throw std::runtime_error("device unavailable");
    }));

    const auto result = BackendResolver{registry}.resolve(BackendPreference::GPU);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == BackendResolveErrorCode::FactoryFailed);
    CHECK(result.error().message.find("device unavailable") != std::string::npos);
}

