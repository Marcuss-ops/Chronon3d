#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <chronon3d/render_graph/backend_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/render_graph/checkbackend.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif

namespace {

class StubBackend final : public chronon3d::graph::RenderBackend {
public:
    void apply_per_pixel_dof(chronon3d::Framebuffer&, std::span<const float>,
                             const chronon3d::DepthOfFieldSettings&,
                             const chronon3d::LensModel&,
                             const std::optional<chronon3d::raster::BBox>&) override {}
    void draw_node(chronon3d::Framebuffer&, const chronon3d::RenderNode&,
                   const chronon3d::RenderState&, const chronon3d::Camera&,
                   int, int) override {}
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

#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("Vulkan backend creates a persistent headless device") {
    auto backend = chronon3d::backends::vulkan::make_vulkan_backend();
    REQUIRE(backend != nullptr);
    const auto* vulkan = dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(backend.get());
    REQUIRE(vulkan != nullptr);
    const auto device_stats = vulkan->stats();
    CHECK(device_stats.discrete_gpu);
    CHECK(device_stats.device_name.find("RTX A4000") != std::string::npos);
    CHECK(vulkan->kernel_registry().size() == 6);
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Composite));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Transform));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::AffineTransform));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Blur));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::ColorAdjust));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Matte));
}

TEST_CASE("Vulkan alpha and luma matte match the CPU coverage formulas") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> target{
        1.0f, 0.5f, 0.25f, 1.0f,
        0.4f, 0.3f, 0.2f, 0.5f,
        0.8f, 0.7f, 0.6f, 1.0f,
        0.1f, 0.2f, 0.3f, 0.25f};
    const std::vector<float> matte{
        0.0f, 0.0f, 0.0f, 0.25f,
        0.4f, 0.1f, 0.05f, 0.5f,
        1.0f, 0.0f, 0.0f, 0.0f,
        0.2f, 0.3f, 0.4f, 1.0f};
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.create_surface(431, desc).ok());
    REQUIRE(backend.create_surface(432, desc).ok());
    REQUIRE(backend.create_surface(433, desc).ok());
    REQUIRE(backend.upload_surface(432, desc, target).ok());
    REQUIRE(backend.upload_surface(433, desc, matte).ok());
    REQUIRE(backend.matte_surface(431, 432, 433, false, false).ok());
    REQUIRE(backend.download_surface(431, output).ok());
    CHECK(output[0] == doctest::Approx(0.25f));
    CHECK(output[1] == doctest::Approx(0.125f));
    CHECK(output[2] == doctest::Approx(0.0625f));
    CHECK(output[3] == doctest::Approx(0.25f));

    REQUIRE(backend.matte_surface(431, 432, 433, true, false).ok());
    REQUIRE(backend.download_surface(431, output).ok());
    const float luma = 0.4f * 0.2126f + 0.1f * 0.7152f + 0.05f * 0.0722f;
    CHECK(output[4] == doctest::Approx(0.4f * luma).epsilon(1e-5));
    CHECK(output[7] == doctest::Approx(0.5f * luma).epsilon(1e-5));
}

TEST_CASE("TrackMatteNode dispatches aligned full-frame matte to Vulkan") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 2;
    ctx.frame_input.height = 2;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    auto target = std::make_shared<Framebuffer>(2, 2);
    auto matte = std::make_shared<Framebuffer>(2, 2);
    target->set_pixel(0, 0, Color{1.0f, 0.5f, 0.25f, 1.0f});
    matte->set_pixel(0, 0, Color{0.0f, 0.0f, 0.0f, 0.25f});
    target->set_pixel(1, 0, Color{0.4f, 0.3f, 0.2f, 0.5f});
    matte->set_pixel(1, 0, Color{0.4f, 0.1f, 0.05f, 0.5f});
    target->set_pixel(0, 1, Color{0.8f, 0.7f, 0.6f, 1.0f});
    matte->set_pixel(0, 1, Color{1.0f, 0.0f, 0.0f, 0.0f});
    target->set_pixel(1, 1, Color{0.1f, 0.2f, 0.3f, 0.25f});
    matte->set_pixel(1, 1, Color{0.2f, 0.3f, 0.4f, 1.0f});

    TrackMatteNode node(TrackMatteType::Alpha, "gpu-matte", {});
    const std::array<FramebufferRef, 2> inputs{target.get(), matte.get()};
    const std::array<std::optional<raster::BBox>, 2> bboxes{
        raster::BBox{0, 0, 2, 2}, raster::BBox{0, 0, 2, 2}};
    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    CHECK(output[0] == doctest::Approx(0.25f));
    CHECK(output[1] == doctest::Approx(0.125f));
    CHECK(output[2] == doctest::Approx(0.0625f));
    CHECK(output[3] == doctest::Approx(0.25f));
    CHECK(output[8] == doctest::Approx(0.0f));
    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("Vulkan color adjust matches the CPU scalar oracle") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t width = 2;
    constexpr std::uint32_t height = 2;
    const chronon3d::runtime::SurfaceDesc desc{
        width, height, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        0.2f, 0.4f, 0.6f, 1.0f,
        0.8f, 0.1f, 0.3f, 0.5f,
        0.9f, 0.9f, 0.9f, 0.0f,
        0.0f, 0.2f, 1.0f, 1.0f};
    std::vector<float> actual(source.size(), 0.0f);
    REQUIRE(backend.create_surface(421, desc).ok());
    REQUIRE(backend.create_surface(422, desc).ok());
    REQUIRE(backend.upload_surface(422, desc, source).ok());
    REQUIRE(backend.color_adjust_surface(
        421, 422, 0.1f, 1.5f,
        chronon3d::Color{0.2f, 0.8f, 0.4f, 1.0f}, 0.25f).ok());
    REQUIRE(backend.download_surface(421, actual).ok());

    for (std::size_t pixel = 0; pixel < source.size() / 4; ++pixel) {
        const auto base = pixel * 4;
        if (source[base + 3] == 0.0f) {
            CHECK(actual[base + 0] == doctest::Approx(source[base + 0]));
            CHECK(actual[base + 1] == doctest::Approx(source[base + 1]));
            CHECK(actual[base + 2] == doctest::Approx(source[base + 2]));
            CHECK(actual[base + 3] == doctest::Approx(0.0f));
            continue;
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const float adjusted = std::clamp(
                (source[base + channel] + 0.1f - 0.5f) * 1.5f + 0.5f,
                0.0f, 1.0f);
            const float tint = channel == 0 ? 0.2f : (channel == 1 ? 0.8f : 0.4f);
            CHECK(actual[base + channel] == doctest::Approx(
                adjusted * 0.75f + tint * 0.25f).epsilon(1e-5));
        }
        CHECK(actual[base + 3] == doctest::Approx(source[base + 3]));
    }
}

TEST_CASE("EffectStackNode dispatches a full-frame glow through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;

    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(8, 8);
    source->clear(Color::transparent());
    source->set_pixel(4, 4, Color{1.0f, 0.5f, 0.25f, 1.0f});

    GlowParams params;
    params.radius = 2.0f;
    params.intensity = 0.5f;
    params.color = Color{0.25f, 0.5f, 1.0f, 1.0f};
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));

    const auto before = backend.stats();
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 8, 8}};
    const auto result = node.execute(ctx, inputs, bboxes);

    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    CHECK(surfaces.size() == 1); // output survives; H/V scratch is released
    CHECK(backend.stats().submissions > before.submissions);

    std::vector<float> output(8 * 8 * 4, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    const auto center = static_cast<std::size_t>((4 * 8 + 4) * 4);
    const auto neighbor = static_cast<std::size_t>((4 * 8 + 5) * 4);
    CHECK(output[center + 0] > 1.0f);
    CHECK(output[neighbor + 0] > 0.0f);

    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("EffectStackNode dispatches a full-frame tint through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 2;
    ctx.frame_input.height = 2;
    ctx.services.backend = &backend;
    ctx.services.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(2, 2);
    source->set_pixel(0, 0, Color{0.2f, 0.4f, 0.6f, 1.0f});
    source->set_pixel(1, 0, Color{0.8f, 0.1f, 0.3f, 0.5f});
    source->set_pixel(0, 1, Color{0.9f, 0.9f, 0.9f, 0.0f});
    source->set_pixel(1, 1, Color{0.0f, 0.2f, 1.0f, 1.0f});

    EffectStack effects;
    effects.push_back(effects::EffectInstance{
        TintParams{.color = Color{0.2f, 0.8f, 0.4f, 1.0f}, .amount = 0.25f}});
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 2, 2}};
    const auto result = node.execute(ctx, inputs, bboxes);

    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.download_surface(handle, output).ok());
    CHECK(output[0] == doctest::Approx(0.2f).epsilon(1e-5));
    CHECK(output[1] == doctest::Approx(0.5f).epsilon(1e-5));
    CHECK(output[2] == doctest::Approx(0.55f).epsilon(1e-5));
    CHECK(output[3] == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(output[8] == doctest::Approx(0.9f)); // transparent source is preserved

    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("Vulkan backend composites premultiplied source-over pixels") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::Framebuffer destination(2, 2);
    chronon3d::Framebuffer source(2, 2);
    destination.clear(chronon3d::Color{0.1f, 0.2f, 0.3f, 0.8f});
    source.clear(chronon3d::Color{0.5f, 0.25f, 0.125f, 0.5f});

    backend.composite_layer(destination, source, chronon3d::BlendMode::Normal,
                            std::nullopt, chronon3d::CompositeOperator::SourceOver);

    const auto pixel = destination.get_pixel(0, 0);
    CHECK(pixel.r == doctest::Approx(0.5f + 0.1f * 0.5f));
    CHECK(pixel.g == doctest::Approx(0.25f + 0.2f * 0.5f));
    CHECK(pixel.b == doctest::Approx(0.125f + 0.3f * 0.5f));
    CHECK(pixel.a == doctest::Approx(0.5f + 0.8f * 0.5f));

    backend.composite_layer(destination, source, chronon3d::BlendMode::Normal,
                            std::nullopt, chronon3d::CompositeOperator::SourceOver);
    const auto reused_pixel = destination.get_pixel(0, 0);
    CHECK(reused_pixel.r == doctest::Approx(0.5f + (0.5f + 0.1f * 0.5f) * 0.5f));
    CHECK(reused_pixel.a == doctest::Approx(0.5f + (0.5f + 0.8f * 0.5f) * 0.5f));
}

TEST_CASE("Vulkan native surfaces stay device-local across upload and composite") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> destination(16, 0.0f);
    const std::vector<float> source{
        0.5f, 0.25f, 0.125f, 0.5f,
        0.5f, 0.25f, 0.125f, 0.5f,
        0.5f, 0.25f, 0.125f, 0.5f,
        0.5f, 0.25f, 0.125f, 0.5f};
    std::vector<float> output(16, 0.0f);

    REQUIRE(backend.create_surface(101, desc).ok());
    REQUIRE(backend.create_surface(102, desc).ok());
    REQUIRE(backend.upload_surface(101, desc, destination).ok());
    REQUIRE(backend.upload_surface(102, desc, source).ok());
    REQUIRE(backend.composite_surfaces(
        101, 102, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    REQUIRE(backend.download_surface(101, output).ok());
    CHECK(output[0] == doctest::Approx(0.5f));
    CHECK(output[1] == doctest::Approx(0.25f));
    CHECK(output[2] == doctest::Approx(0.125f));
    CHECK(output[3] == doctest::Approx(0.5f));
}

TEST_CASE("Vulkan source-over composite matches SoftwareCompositor oracle") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::Framebuffer cpu_destination(4, 3);
    chronon3d::Framebuffer cpu_source(4, 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float f = static_cast<float>(y * 4 + x) / 12.0f;
            cpu_destination.set_pixel(x, y, chronon3d::Color{0.1f + f * 0.2f, 0.2f, 0.3f, 0.4f + f * 0.2f});
            cpu_source.set_pixel(x, y, chronon3d::Color{0.05f, 0.1f + f * 0.1f, 0.2f, 0.2f + f * 0.5f});
        }
    }
    auto expected = cpu_destination;
    chronon3d::SoftwareCompositor::composite_layer(
        expected, cpu_source, chronon3d::BlendMode::Normal, std::nullopt,
        chronon3d::CompositeOperator::SourceOver, true);

    const chronon3d::runtime::SurfaceDesc desc{
        4, 3, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> destination(4 * 3 * 4);
    std::vector<float> source(4 * 3 * 4);
    std::vector<float> actual(destination.size(), 0.0f);
    auto pack = [](const chronon3d::Framebuffer& framebuffer, std::vector<float>& output) {
        std::size_t index = 0;
        for (int y = 0; y < framebuffer.height(); ++y) {
            for (int x = 0; x < framebuffer.width(); ++x) {
                const auto color = framebuffer.get_pixel(x, y);
                output[index++] = color.r;
                output[index++] = color.g;
                output[index++] = color.b;
                output[index++] = color.a;
            }
        }
    };
    pack(cpu_destination, destination);
    pack(cpu_source, source);
    REQUIRE(backend.create_surface(131, desc).ok());
    REQUIRE(backend.create_surface(132, desc).ok());
    REQUIRE(backend.upload_surface(131, desc, destination).ok());
    REQUIRE(backend.upload_surface(132, desc, source).ok());
    REQUIRE(backend.composite_surfaces(
        131, 132, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    REQUIRE(backend.download_surface(131, actual).ok());

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const auto expected_pixel = expected.get_pixel(x, y);
            const std::size_t index = (static_cast<std::size_t>(y) * 4 + x) * 4;
            CHECK(actual[index + 0] == doctest::Approx(expected_pixel.r).epsilon(1e-5));
            CHECK(actual[index + 1] == doctest::Approx(expected_pixel.g).epsilon(1e-5));
            CHECK(actual[index + 2] == doctest::Approx(expected_pixel.b).epsilon(1e-5));
            CHECK(actual[index + 3] == doctest::Approx(expected_pixel.a).epsilon(1e-5));
        }
    }
}

TEST_CASE("Vulkan upload staging is reused after warmup and telemetry is observable") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 0.25f);
    REQUIRE(backend.create_surface(111, desc).ok());
    REQUIRE(backend.upload_surface(111, desc, pixels).ok());
    const auto warmed = backend.stats();
    REQUIRE(backend.upload_surface(111, desc, pixels).ok());
    const auto reused = backend.stats();
    CHECK(warmed.staging_allocations == 1);
    CHECK(reused.staging_allocations == warmed.staging_allocations);
    CHECK(reused.upload_calls == warmed.upload_calls + 1);
    CHECK(reused.upload_bytes == warmed.upload_bytes + pixels.size() * sizeof(float));
    CHECK(reused.submissions == warmed.submissions + 1);
}

TEST_CASE("Vulkan async upload returns a timeline ticket and waits before readback") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 0.75f);
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.create_surface(121, desc).ok());
    chronon3d::runtime::UploadTicket ticket{};
    REQUIRE(backend.upload_surface_async(121, desc, pixels, ticket).ok());
    CHECK(ticket.valid());
    REQUIRE(backend.wait_upload(ticket).ok());
    REQUIRE(backend.download_surface(121, output).ok());
    CHECK(output[0] == doctest::Approx(0.75f));
    CHECK(backend.stats().submissions >= 2);
}

TEST_CASE("Vulkan surface release waits for asynchronous work") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 0.35f);
    chronon3d::runtime::UploadTicket ticket{};
    REQUIRE(backend.create_surface(122, desc).ok());
    REQUIRE(backend.upload_surface_async(122, desc, pixels, ticket).ok());
    REQUIRE(backend.release_surface(122).ok());
    CHECK(backend.stats().surface_releases == 1);
}

TEST_CASE("Vulkan upload ring queues three assets before waiting") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    // Locals are named ticket_a/b/c (not f-prefixed) so the
    // check_frame_value_convention gate (targeting Frame::value) does not
    // over-match: these are UploadTicket ordering probes, not Frame reads.
    const std::vector<float> ticket_a_pixels(16, 0.1f);
    const std::vector<float> ticket_b_pixels(16, 0.2f);
    const std::vector<float> ticket_c_pixels(16, 0.3f);
    chronon3d::runtime::UploadTicket ticket_a{};
    chronon3d::runtime::UploadTicket ticket_b{};
    chronon3d::runtime::UploadTicket ticket_c{};

    REQUIRE(backend.create_surface(123, desc).ok());
    REQUIRE(backend.create_surface(124, desc).ok());
    REQUIRE(backend.create_surface(125, desc).ok());
    REQUIRE(backend.upload_surface_async(123, desc, ticket_a_pixels, ticket_a).ok());
    REQUIRE(backend.upload_surface_async(124, desc, ticket_b_pixels, ticket_b).ok());
    REQUIRE(backend.upload_surface_async(125, desc, ticket_c_pixels, ticket_c).ok());
    CHECK(ticket_a.value < ticket_b.value);
    CHECK(ticket_b.value < ticket_c.value);
    CHECK(backend.stats().staging_allocations >= 3);
    REQUIRE(backend.wait_upload(ticket_a).ok());
    REQUIRE(backend.wait_upload(ticket_b).ok());
    REQUIRE(backend.wait_upload(ticket_c).ok());
    std::vector<float> output(16, 0.0f);
    REQUIRE(backend.download_surface(125, output).ok());
    CHECK(output[0] == doctest::Approx(0.3f));
}

TEST_CASE("Vulkan native transform applies integer translation and opacity") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc source_desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const chronon3d::runtime::SurfaceDesc destination_desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source(16, 1.0f);
    std::vector<float> output(64, -1.0f);
    REQUIRE(backend.create_surface(201, source_desc).ok());
    REQUIRE(backend.create_surface(202, destination_desc).ok());
    REQUIRE(backend.upload_surface(201, source_desc, source).ok());
    REQUIRE(backend.transform_surface(202, 201, 1, 1, 0.5f).ok());
    REQUIRE(backend.download_surface(202, output).ok());
    CHECK(output[(1 * 4 + 1) * 4] == doctest::Approx(0.5f));
    CHECK(output[(2 * 4 + 2) * 4 + 3] == doctest::Approx(0.5f));
    CHECK(output[(0 * 4 + 0) * 4] == doctest::Approx(0.0f));
}

TEST_CASE("Vulkan native affine transform matches inverse mapping") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc source_desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const chronon3d::runtime::SurfaceDesc destination_desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<float> output(64, -1.0f);
    chronon3d::runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f;
    transform.source_x[3] = 0.0f;
    transform.source_y[1] = 1.0f;
    transform.source_y[3] = 0.0f;
    transform.max_x = 2.0f;
    transform.max_y = 2.0f;
    transform.opacity = 0.5f;
    transform.bilinear = 0;

    REQUIRE(backend.create_surface(301, source_desc).ok());
    REQUIRE(backend.create_surface(302, destination_desc).ok());
    REQUIRE(backend.upload_surface(301, source_desc, source).ok());
    REQUIRE(backend.transform_surface_affine(302, 301, transform).ok());
    REQUIRE(backend.download_surface(302, output).ok());
    CHECK(output[0] == doctest::Approx(0.5f));
    CHECK(output[3] == doctest::Approx(0.5f));
    CHECK(output[(3 * 4 + 3) * 4] == doctest::Approx(0.0f));
}

TEST_CASE("Vulkan separable blur stays on device across two passes") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t size = 9;
    const chronon3d::runtime::SurfaceDesc desc{
        size, size, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(size * size * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(size / 2) * size + size / 2) * 4;
    source[center + 0] = 1.0f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(401, desc).ok());
    REQUIRE(backend.create_surface(402, desc).ok());
    REQUIRE(backend.create_surface(403, desc).ok());
    REQUIRE(backend.upload_surface(401, desc, source).ok());
    REQUIRE(backend.blur_surface(402, 401, 1.5f, true).ok());
    REQUIRE(backend.blur_surface(403, 402, 1.5f, false).ok());
    REQUIRE(backend.download_surface(403, output).ok());

    const auto neighbor = (static_cast<std::size_t>(size / 2) * size + size / 2 + 1) * 4;
    CHECK(output[center + 0] < 1.0f);
    CHECK(output[neighbor + 0] > 0.0f);
    CHECK(output[neighbor + 0] < output[center + 0]);
}

TEST_CASE("Vulkan glow sequence uses blur passes followed by additive composite") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t size = 9;
    const chronon3d::runtime::SurfaceDesc desc{
        size, size, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(size * size * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(size / 2) * size + size / 2) * 4;
    source[center + 0] = 0.8f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(411, desc).ok());
    REQUIRE(backend.create_surface(412, desc).ok());
    REQUIRE(backend.create_surface(413, desc).ok());
    REQUIRE(backend.create_surface(414, desc).ok());
    REQUIRE(backend.upload_surface(411, desc, source).ok());
    REQUIRE(backend.upload_surface(413, desc, source).ok());
    const auto submissions_before = backend.stats().submissions;
    REQUIRE(backend.glow_surfaces(
        413, 411, 412, 414, 1.5f, 1.0f,
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}).ok());
    CHECK(backend.stats().submissions == submissions_before + 1);
    REQUIRE(backend.download_surface(413, output).ok());

    CHECK(output[center + 0] > source[center + 0]);
    const auto neighbor = (static_cast<std::size_t>(size / 2) * size + size / 2 + 1) * 4;
    CHECK(output[neighbor + 0] > 0.0f);

    // The pass descriptor sets are persistent/reusable across frames; a
    // second one-submission glow must not exhaust or reallocate the pool.
    const auto second_before = backend.stats().submissions;
    REQUIRE(backend.glow_surfaces(
        413, 411, 412, 414, 1.5f, 1.0f,
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}).ok());
    CHECK(backend.stats().submissions == second_before + 1);
}

TEST_CASE("Vulkan frame batch records every pass into a single submission") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    source[center + 0] = 0.8f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(521, desc).ok());
    REQUIRE(backend.create_surface(522, desc).ok());
    REQUIRE(backend.create_surface(523, desc).ok());
    REQUIRE(backend.create_surface(524, desc).ok());
    REQUIRE(backend.upload_surface(521, desc, source).ok());

    const auto submissions_before = backend.stats().submissions;
    backend.begin_frame_batch();
    REQUIRE(backend.transform_surface(522, 521, 0, 0, 1.0f).ok());
    REQUIRE(backend.blur_surface(523, 522, 1.5f, true).ok());
    REQUIRE(backend.color_adjust_surface(
        524, 523, 0.0f, 1.0f,
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f).ok());
    REQUIRE(backend.composite_surfaces(
        524, 521, chronon3d::BlendMode::Normal,
        chronon3d::CompositeOperator::SourceOver).ok());
    backend.end_frame_batch();
    // Four recorded passes must coalesce into exactly one queue submission:
    // the whole frame is submitted once, not once per operation.
    CHECK(backend.stats().submissions == submissions_before + 1);

    REQUIRE(backend.download_surface(524, output).ok());
    // The transform → blur → color adjust → composite chain preserved the
    // bright opaque center pixel.
    CHECK(output[center + 0] > 0.0f);
    CHECK(output[center + 3] > 0.0f);

    // The frame-batch ring keeps one fence per slot: four consecutive
    // batches rotate through all three slots, and the last one reuses a
    // slot whose previous submission is still in flight — begin_frame_batch()
    // must wait on that slot's fence instead of stalling the whole device.
    const auto ring_before = backend.stats().submissions;
    for (int i = 0; i < 4; ++i) {
        backend.begin_frame_batch();
        REQUIRE(backend.transform_surface(522, 521, 0, 0, 1.0f).ok());
        backend.end_frame_batch();
    }
    CHECK(backend.stats().submissions == ring_before + 4);
    REQUIRE(backend.download_surface(522, output).ok());
    CHECK(output[center + 0] == doctest::Approx(0.8f).epsilon(1e-3f));
}

TEST_CASE("Vulkan frame batch descriptor allocator grows past the first chunk") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        4, 4, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::FrameTransient, 0};
    std::vector<float> source(4 * 4 * 4, 0.0f);
    const auto center = (static_cast<std::size_t>(2) * 4 + 2) * 4;
    source[center + 0] = 0.9f;
    source[center + 3] = 1.0f;
    std::vector<float> output(source.size(), 0.0f);

    REQUIRE(backend.create_surface(531, desc).ok());
    REQUIRE(backend.create_surface(532, desc).ok());
    REQUIRE(backend.upload_surface(531, desc, source).ok());

    // 100 recorded passes need 200 storage-image descriptors, exceeding the
    // first 64-set chunk (192 descriptors): the allocator must grow a second
    // chunk (128 sets) while the whole frame still submits exactly once.
    const auto submissions_before = backend.stats().submissions;
    backend.begin_frame_batch();
    for (int i = 0; i < 100; ++i) {
        REQUIRE(backend.transform_surface(532, 531, 0, 0, 1.0f).ok());
    }
    backend.end_frame_batch();
    CHECK(backend.stats().submissions == submissions_before + 1);

    REQUIRE(backend.download_surface(532, output).ok());
    CHECK(output[center + 0] == doctest::Approx(0.9f).epsilon(1e-3f));
}

TEST_CASE("GPU asset cache reuses uploads and evicts by byte budget") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::runtime::RenderSurfaceRegistry registry;
    chronon3d::runtime::GpuAssetCache cache(64);
    cache.attach(registry, backend);

    const chronon3d::runtime::SurfaceDesc desc{
        2, 2, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> pixels(16, 1.0f);
    chronon3d::runtime::GpuAssetKey first{};
    first.content_digest.bytes[0] = std::byte{0x01};
    first.format = desc.format;
    first.width = desc.width;
    first.height = desc.height;

    const auto uploaded = cache.acquire(first, desc, pixels);
    REQUIRE(uploaded.ok());
    CHECK_FALSE(uploaded.cache_hit);
    const auto reused = cache.acquire(first, desc, pixels);
    REQUIRE(reused.ok());
    CHECK(reused.cache_hit);
    CHECK(reused.handle == uploaded.handle);
    CHECK(cache.stats().upload_bytes == 64);
    CHECK(cache.stats().hits == 1);

    auto second = first;
    second.content_digest.bytes[0] = std::byte{0x02};
    const auto second_upload = cache.acquire(second, desc, pixels);
    REQUIRE(second_upload.ok());
    CHECK(cache.stats().evictions == 1);
    CHECK(cache.stats().resident_bytes == 64);
    CHECK(registry.lookup(uploaded.handle) == nullptr);
}
#endif

TEST_CASE("resource planner preserves opaque surface identity across aliasing") {
    using namespace chronon3d::runtime;
    ResourcePlanner planner;
    const RenderSurfaceHandle first{41};
    const RenderSurfaceHandle second{42};
    ResourceDesc desc{64, 64, PixelFormat::Rgba8Unorm,
                      ResourceUsage::ColorAttachment, 64 * 64 * 4};
    planner.add(ResourceRequest{"first", ResourceKind::Color, desc.bytes,
                                LifetimeClass::FrameTransient, 0, 1,
                                alignof(std::max_align_t), desc, first});
    planner.add(ResourceRequest{"second", ResourceKind::Color, desc.bytes,
                                LifetimeClass::FrameTransient, 2, 3,
                                alignof(std::max_align_t), desc, second});

    const auto plan = planner.build();
    REQUIRE(plan.allocations.size() == 2);
    CHECK(plan.allocations[0].surface == first);
    CHECK(plan.allocations[1].surface == second);
    CHECK(plan.allocations[0].physical_slot == plan.allocations[1].physical_slot);
}

TEST_CASE("surface registry owns logical identity independently of physical slots") {
    using namespace chronon3d::runtime;
    RenderSurfaceRegistry registry;
    CHECK(registry.create(SurfaceDesc{}) == kInvalidRenderSurfaceHandle);

    const auto handle = registry.create(SurfaceDesc{
        16, 8, PixelFormat::Rgba8Unorm, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0});
    REQUIRE(handle != kInvalidRenderSurfaceHandle);
    REQUIRE(registry.lookup(handle) != nullptr);
    CHECK(registry.lookup(handle)->desc.bytes == 16u * 8u * sizeof(float) * 4u);
    CHECK(registry.lookup(handle)->physical_slot == std::numeric_limits<std::size_t>::max());
    CHECK(registry.bind_physical_slot(handle, 3));
    CHECK(registry.lookup(handle)->physical_slot == 3);
    CHECK(registry.size() == 1);
    CHECK(registry.release(handle));
    CHECK(registry.lookup(handle) == nullptr);
    CHECK_FALSE(registry.release(handle));
}

TEST_CASE("framebuffer copies preserve opaque surface identity") {
    chronon3d::Framebuffer original(2, 2);
    original.set_surface_handle(77);
    chronon3d::Framebuffer copy(original);
    CHECK(copy.surface_handle() == 77);
    chronon3d::Framebuffer moved(std::move(copy));
    CHECK(moved.surface_handle() == 77);
    moved.clear_surface_handle();
    CHECK(moved.surface_handle() == chronon3d::runtime::kInvalidRenderSurfaceHandle);
}

TEST_CASE("command planner preserves pass order and kinds") {
    using namespace chronon3d::runtime;
    GpuCommandPlanner planner;
    planner.composite(CompositePass{.destination = 1, .source = 2, .blend_mode = 0});
    planner.transform(TransformPass{.destination = 3, .source = 1,
                                    .offset_x = 1, .offset_y = 1, .opacity = 0.5f});
    planner.matte(MattePass{.destination = 4, .target = 3, .matte = 2,
                            .luma = 0, .inverted = 0});

    const auto plan = planner.build();
    REQUIRE(plan.pass_count() == 3);
    CHECK(plan.passes.passes[0].kind == GpuPassKind::Composite);
    CHECK(plan.passes.passes[1].kind == GpuPassKind::Transform);
    CHECK(plan.passes.passes[2].kind == GpuPassKind::Matte);
    CHECK(std::get<TransformPass>(plan.passes.passes[1].params).destination == 3);
    CHECK(std::get<MattePass>(plan.passes.passes[2].params).target == 3);
}

TEST_CASE("command planner aliases non-overlapping transient surfaces") {
    using namespace chronon3d::runtime;
    GpuCommandPlanner planner;
    const RenderSurfaceHandle input{1};
    const RenderSurfaceHandle scratch{2};
    const RenderSurfaceHandle output{3};
    const ResourceDesc desc{16, 8, PixelFormat::Rgba8Unorm, ResourceUsage::Storage,
                            16 * 8 * 4, alignof(std::max_align_t),
                            ResourceLifetime::Transient};
    planner.declare_surface(input, desc);
    planner.declare_surface(scratch, desc);
    planner.declare_surface(output, desc);

    // scratch (0..1) bridges the two passes; input (0..0) and output (1..1)
    // never overlap so the planner must alias them onto one physical slot.
    planner.blur(BlurPass{.destination = scratch, .source = input,
                          .radius = 2.0f, .horizontal = 1});
    planner.blur(BlurPass{.destination = output, .source = scratch,
                          .radius = 2.0f, .horizontal = 0});

    const auto plan = planner.build();
    REQUIRE(plan.pass_count() == 2);

    const auto slot_for = [&](RenderSurfaceHandle handle) {
        for (const auto& allocation : plan.resources.allocations) {
            if (allocation.surface == handle) return allocation.physical_slot;
        }
        return std::numeric_limits<std::size_t>::max();
    };

    const auto input_slot = slot_for(input);
    const auto scratch_slot = slot_for(scratch);
    const auto output_slot = slot_for(output);
    CHECK(input_slot != std::numeric_limits<std::size_t>::max());
    CHECK(scratch_slot != std::numeric_limits<std::size_t>::max());
    CHECK(output_slot != std::numeric_limits<std::size_t>::max());
    CHECK(input_slot == output_slot);
    CHECK(input_slot != scratch_slot);
}

TEST_CASE("command planner emits read and write barriers per pass") {
    using namespace chronon3d::runtime;
    GpuCommandPlanner planner;
    const RenderSurfaceHandle input{1};
    const RenderSurfaceHandle scratch{2};
    const RenderSurfaceHandle output{3};
    planner.blur(BlurPass{.destination = scratch, .source = input,
                          .radius = 1.0f, .horizontal = 1});
    planner.blur(BlurPass{.destination = output, .source = scratch,
                          .radius = 1.0f, .horizontal = 0});

    const auto plan = planner.build();
    REQUIRE(plan.barriers.size() == 4);
    // Pass 0: input sampled (Read), scratch written (Write).
    CHECK(plan.barriers.transitions[0].pass_index == 0);
    CHECK(plan.barriers.transitions[0].surface == input);
    CHECK(plan.barriers.transitions[0].access == ResourceAccess::Read);
    CHECK(plan.barriers.transitions[1].pass_index == 0);
    CHECK(plan.barriers.transitions[1].surface == scratch);
    CHECK(plan.barriers.transitions[1].access == ResourceAccess::Write);
    // Pass 1: scratch sampled (Read) — the write→read transition — and
    // output written (Write).
    CHECK(plan.barriers.transitions[2].pass_index == 1);
    CHECK(plan.barriers.transitions[2].surface == scratch);
    CHECK(plan.barriers.transitions[2].access == ResourceAccess::Read);
    CHECK(plan.barriers.transitions[3].pass_index == 1);
    CHECK(plan.barriers.transitions[3].surface == output);
    CHECK(plan.barriers.transitions[3].access == ResourceAccess::Write);
}

TEST_CASE("plan slot binding propagates aliasing to the surface registry") {
    using namespace chronon3d::runtime;
    RenderSurfaceRegistry registry;
    const auto input = registry.create(SurfaceDesc{16, 8, PixelFormat::Rgba8Unorm,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0});
    const auto scratch = registry.create(SurfaceDesc{16, 8, PixelFormat::Rgba8Unorm,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0});
    const auto output = registry.create(SurfaceDesc{16, 8, PixelFormat::Rgba8Unorm,
        ResourceUsage::Storage, LifetimeClass::FrameTransient, 0});
    REQUIRE(input != kInvalidRenderSurfaceHandle);
    REQUIRE(scratch != kInvalidRenderSurfaceHandle);
    REQUIRE(output != kInvalidRenderSurfaceHandle);

    const ResourceDesc desc{16, 8, PixelFormat::Rgba8Unorm, ResourceUsage::Storage,
                            16 * 8 * 4, alignof(std::max_align_t),
                            ResourceLifetime::Transient};
    GpuCommandPlanner planner;
    planner.declare_surface(input, desc);
    planner.declare_surface(scratch, desc);
    planner.declare_surface(output, desc);
    planner.blur(BlurPass{.destination = scratch, .source = input,
                          .radius = 2.0f, .horizontal = 1});
    planner.blur(BlurPass{.destination = output, .source = scratch,
                          .radius = 2.0f, .horizontal = 0});

    const auto plan = planner.build();
    bind_plan_slots(plan.resources, registry);

    const auto* input_record = registry.lookup(input);
    const auto* scratch_record = registry.lookup(scratch);
    const auto* output_record = registry.lookup(output);
    REQUIRE(input_record != nullptr);
    REQUIRE(scratch_record != nullptr);
    REQUIRE(output_record != nullptr);
    CHECK(input_record->physical_slot != std::numeric_limits<std::size_t>::max());
    CHECK(scratch_record->physical_slot != std::numeric_limits<std::size_t>::max());
    CHECK(output_record->physical_slot != std::numeric_limits<std::size_t>::max());
    CHECK(input_record->physical_slot == output_record->physical_slot);
    CHECK(input_record->physical_slot != scratch_record->physical_slot);
}

TEST_CASE("checkbackend pixel comparison matches identical buffers") {
    using namespace chronon3d::graph;
    const std::vector<float> buffer{0.1f, 0.2f, 0.3f, 0.4f,
                                    0.5f, 0.6f, 0.7f, 0.8f};
    const auto result = compare_pixels(buffer, buffer);
    CHECK(result.matched);
    CHECK(result.mismatched_pixels == 0);
    CHECK(result.max_delta == doctest::Approx(0.0f));
}

TEST_CASE("checkbackend pixel comparison rejects out-of-tolerance channels") {
    using namespace chronon3d::graph;
    const std::vector<float> reference{0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<float> result = reference;
    result[0] = 0.6f;  // delta 0.1 exceeds the relative gate
    const PixelTolerance tolerance{.epsilon = 1e-4f, .absolute = 1e-4f};
    const auto report = compare_pixels(reference, result, tolerance);
    CHECK_FALSE(report.matched);
    CHECK(report.mismatched_pixels == 1);
    CHECK(report.max_delta == doctest::Approx(0.1f).epsilon(1e-4));
}

TEST_CASE("checkbackend pixel comparison respects the absolute tolerance floor") {
    using namespace chronon3d::graph;
    // A near-zero reference makes the relative gate tiny; the absolute floor
    // must still admit a small delta.
    const std::vector<float> reference{0.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<float> result{1e-5f, 0.0f, 0.0f, 0.0f};
    const PixelTolerance tolerance{.epsilon = 1e-4f, .absolute = 1e-4f};
    CHECK(compare_pixels(reference, result, tolerance).matched);
}

TEST_CASE("checkbackend size mismatch is reported as a sentinel failure") {
    using namespace chronon3d::graph;
    const std::vector<float> reference(8, 0.0f);
    const std::vector<float> result(4, 0.0f);
    const auto report = compare_pixels(reference, result);
    CHECK_FALSE(report.matched);
    CHECK(report.mismatched_pixels == std::numeric_limits<std::size_t>::max());
}
