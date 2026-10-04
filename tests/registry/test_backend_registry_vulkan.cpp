// test_backend_registry_vulkan.cpp — Vulkan device, node dispatch and
// surface upload tests (split from test_backend_registry.cpp).
//
// Split parts (each ≤ 400 LOC) keep the Vulkan-enabled coverage compiled in
// exactly the same configurations as this origin file:
//   - test_backend_registry_vulkan_text_run.cpp    (packed/cached text-run)
//   - test_backend_registry_vulkan_dispatch.cpp    (TrackMatte/SourceNode ops)
//   - test_backend_registry_vulkan_surfaces.cpp    (matte/effect/composite)
//   - test_backend_registry_vulkan_transforms.cpp  (upload/transform/blur/batch)
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
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <tests/helpers/cpu_gpu_parity.hpp>
#include <tests/helpers/gpu_readiness_gate.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <blend2d.h>
#endif

#include "../../src/render_graph/nodes/text_run/gpu_text_run.hpp"

namespace chronon3d::renderer {
void draw_transformed_shape(Framebuffer&, const Shape&, const Mat4&, const Color&,
                            float, const RenderState*, const Fill*, float);
}

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
#ifdef CHRONON3D_ENABLE_VULKAN
TEST_CASE("Vulkan solid closed-shape strokes match software alignment pixels") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    VulkanBackend backend;
    constexpr int width = 96;
    constexpr int height = 64;
    constexpr std::array alignments{
        StrokeAlignment::Inside, StrokeAlignment::Center,
        StrokeAlignment::Outside};
    const std::array types{ShapeType::Rect, ShapeType::RoundedRect,
                           ShapeType::Circle};
    struct StrokeTransformCase {
        Vec2 scale;
        float rotation;
        float shear;
    };
    const std::array transforms{
        StrokeTransformCase{{0.75f, 0.75f}, 0.0f, 0.0f},
        StrokeTransformCase{{1.0f, 1.0f}, 0.0f, 0.0f},
        StrokeTransformCase{{1.25f, 1.25f}, 0.0f, 0.0f},
        StrokeTransformCase{{2.0f, 2.0f}, 0.0f, 0.0f},
        StrokeTransformCase{{1.25f, 0.75f}, 0.0f, 0.0f},
        StrokeTransformCase{{0.75f, 2.0f}, 0.0f, 0.0f},
        StrokeTransformCase{{1.0f, 1.0f}, 0.37f, 0.0f},
        StrokeTransformCase{{1.25f, 0.75f}, -0.29f, 0.0f}};
    runtime::RenderSurfaceHandle handle = 0xC3D200u;

    for (const auto transform : transforms) {
      for (const auto type : types) {
        for (const auto alignment : alignments) {
            RenderNode node;
            node.shape.set_type(type);
            node.color = Color{0.16f, 0.28f, 0.52f, 0.72f};
            if (type == ShapeType::Rect) {
                node.shape.rect().size = {30.0f, 20.0f};
                node.shape.rect().stroke.enabled = true;
                node.shape.rect().stroke.width = 4.0f;
                node.shape.rect().stroke.color = Color{0.9f, 0.18f, 0.34f, 0.63f};
                node.shape.rect().stroke.alignment = alignment;
            } else if (type == ShapeType::RoundedRect) {
                node.shape.rounded_rect().size = {30.0f, 20.0f};
                node.shape.rounded_rect().radius = 6.0f;
                node.shape.rounded_rect().stroke.enabled = true;
                node.shape.rounded_rect().stroke.width = 4.0f;
                node.shape.rounded_rect().stroke.color = Color{0.9f, 0.18f, 0.34f, 0.63f};
                node.shape.rounded_rect().stroke.alignment = alignment;
            } else {
                node.shape.circle().radius = 12.0f;
                node.shape.circle().stroke.enabled = true;
                node.shape.circle().stroke.width = 4.0f;
                node.shape.circle().stroke.color = Color{0.9f, 0.18f, 0.34f, 0.63f};
                node.shape.circle().stroke.alignment = alignment;
            }

            Framebuffer expected(width, height);
            Color background = Color{0.08f, 0.12f, 0.2f, 0.7f}.to_linear();
            background.r *= background.a;
            background.g *= background.a;
            background.b *= background.a;
            expected.clear(background);
            RenderState state;
            state.matrix = Mat4{1.0f};
            const float cosine = std::cos(transform.rotation);
            const float sine = std::sin(transform.rotation);
            state.matrix[0][0] = cosine * transform.scale.x;
            state.matrix[0][1] = sine * transform.scale.x;
            state.matrix[1][0] = (-sine + transform.shear * cosine) * transform.scale.y;
            state.matrix[1][1] = (cosine + transform.shear * sine) * transform.scale.y;
            state.matrix[3][0] = 10.0f;
            state.matrix[3][1] = 8.0f;
            state.clip_rect = raster::BBox{12, 10, 42, 30};
            state.opacity = 1.0f;
            const auto desc = runtime::SurfaceDesc::make(
                width, height, runtime::PixelFormat::Rgba32Float,
                runtime::ResourceUsage::Generic,
                runtime::LifetimeClass::FrameTransient, runtime::ColorMetadata{});
            REQUIRE(backend.create_surface(handle, desc).ok());
            std::vector<float> initial(static_cast<std::size_t>(width * height * 4));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const Color c = expected.get_pixel(x, y);
                    const auto i = static_cast<std::size_t>((y * width + x) * 4);
                    initial[i] = c.r; initial[i + 1] = c.g;
                    initial[i + 2] = c.b; initial[i + 3] = c.a;
                }
            }
            REQUIRE(backend.upload_surface(handle, desc, initial).ok());
            const Color linear_fill = node.color.to_linear();
            renderer::draw_transformed_shape(
                expected, node.shape, state.matrix, linear_fill, 0.0f,
                &state, nullptr, node.corner_radius);
            Framebuffer actual(width, height);
            actual.set_surface_handle(handle);
            backend.begin_frame_batch();
            const auto drawn = backend.draw_node(actual, node, state, Camera{}, width, height);
            backend.end_frame_batch();
            REQUIRE(drawn.ok());

            std::vector<float> pixels(initial.size());
            REQUIRE(backend.download_surface(handle, pixels).ok());
            INFO("shape=" << static_cast<int>(type)
                 << " alignment=" << static_cast<int>(alignment)
                 << " scale=" << transform.scale.x << "x" << transform.scale.y
                 << " rotation=" << transform.rotation
                 << " shear=" << transform.shear);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const Color c = expected.get_pixel(x, y);
                    const float wanted[] = {c.r, c.g, c.b, c.a};
                    const auto i = static_cast<std::size_t>((y * width + x) * 4);
                    for (int channel = 0; channel < 4; ++channel) {
                        INFO("x=" << x << " y=" << y << " channel=" << channel);
                        REQUIRE(pixels[i + static_cast<std::size_t>(channel)] ==
                                doctest::Approx(wanted[channel]).epsilon(2.0e-6));
                    }
                }
            }
            REQUIRE(backend.release_surface(handle).ok());
            ++handle;
        }
      }
    }
}

TEST_CASE("Vulkan closed-shape strokes reject sheared transforms") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    VulkanBackend backend;
    constexpr int width = 48;
    constexpr int height = 32;
    constexpr std::array alignments{
        StrokeAlignment::Inside, StrokeAlignment::Center,
        StrokeAlignment::Outside};
    const auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, runtime::ColorMetadata{});
    constexpr runtime::RenderSurfaceHandle handle = 0xC3D2F0u;
    REQUIRE(backend.create_surface(handle, desc).ok());

    RenderNode node;
    node.shape.set_type(ShapeType::Rect);
    node.shape.rect().size = {24.0f, 16.0f};
    node.shape.rect().stroke.enabled = true;
    node.shape.rect().stroke.width = 3.0f;
    node.shape.rect().stroke.color = Color{1.0f, 0.0f, 0.0f, 1.0f};
    RenderState state;
    state.matrix = Mat4{1.0f};
    state.matrix[1][0] = 0.28f;
    state.matrix[3][0] = 8.0f;
    state.matrix[3][1] = 6.0f;

    Framebuffer target(width, height);
    target.set_surface_handle(handle);
    for (const auto alignment : alignments) {
        node.shape.rect().stroke.alignment = alignment;
        const auto result = backend.draw_node(target, node, state, Camera{}, width, height);
        CHECK_FALSE(result.ok());
        CHECK(result.error().code == graph::RenderBackendErrorCode::UnsupportedCapability);
    }
    REQUIRE(backend.release_surface(handle).ok());
}

TEST_CASE("Vulkan per-corner rounded strokes match software pixels") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    VulkanBackend backend;
    constexpr int width = 80;
    constexpr int height = 64;
    constexpr std::array alignments{
        StrokeAlignment::Inside, StrokeAlignment::Center,
        StrokeAlignment::Outside};
    runtime::RenderSurfaceHandle handle = 0xC3D300u;

    for (const auto alignment : alignments) {
        RenderNode node;
        node.shape.set_type(ShapeType::RoundedRect);
        node.shape.rounded_rect().size = {34.0f, 26.0f};
        node.shape.rounded_rect().corner_radius =
            std::array<float, 4>{4.0f, 10.0f, 12.0f, 2.0f};
        node.shape.rounded_rect().stroke.enabled = true;
        node.shape.rounded_rect().stroke.width = 4.0f;
        node.shape.rounded_rect().stroke.color = Color{0.9f, 0.18f, 0.34f, 0.63f};
        node.shape.rounded_rect().stroke.alignment = alignment;
        node.color = Color{0.16f, 0.28f, 0.52f, 0.72f};

        RenderState state;
        state.matrix = Mat4{1.0f};
        const Vec2 scale{0.9f, 1.1f};
        const float angle = 0.22f;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        state.matrix[0][0] = cosine * scale.x;
        state.matrix[0][1] = sine * scale.x;
        state.matrix[1][0] = -sine * scale.y;
        state.matrix[1][1] = cosine * scale.y;
        state.matrix[3][0] = 25.0f;
        state.matrix[3][1] = 14.0f;
        state.clip_rect = raster::BBox{15, 8, 66, 54};

        Framebuffer expected(width, height);
        Color background = Color{0.08f, 0.12f, 0.2f, 0.7f}.to_linear();
        background.r *= background.a;
        background.g *= background.a;
        background.b *= background.a;
        expected.clear(background);
        const Color linear_fill = node.color.to_linear();
        const auto desc = runtime::SurfaceDesc::make(
            width, height, runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Generic,
            runtime::LifetimeClass::FrameTransient, runtime::ColorMetadata{});
        REQUIRE(backend.create_surface(handle, desc).ok());
        std::vector<float> initial(static_cast<std::size_t>(width * height * 4));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const Color pixel = expected.get_pixel(x, y);
                const auto i = static_cast<std::size_t>((y * width + x) * 4);
                initial[i] = pixel.r; initial[i + 1] = pixel.g;
                initial[i + 2] = pixel.b; initial[i + 3] = pixel.a;
            }
        renderer::draw_transformed_shape(
            expected, node.shape, state.matrix, linear_fill, 0.0f,
            &state, nullptr, node.corner_radius);
        REQUIRE(backend.upload_surface(handle, desc, initial).ok());
        Framebuffer actual(width, height);
        actual.set_surface_handle(handle);
        const auto drawn = backend.draw_node(actual, node, state, Camera{}, width, height);
        REQUIRE(drawn.ok());
        std::vector<float> pixels(initial.size());
        REQUIRE(backend.download_surface(handle, pixels).ok());
        INFO("per-corner stroke alignment=" << static_cast<int>(alignment));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const Color pixel = expected.get_pixel(x, y);
                const float wanted[] = {pixel.r, pixel.g, pixel.b, pixel.a};
                const auto i = static_cast<std::size_t>((y * width + x) * 4);
                for (int channel = 0; channel < 4; ++channel) {
                    INFO("x=" << x << " y=" << y << " channel=" << channel);
                    CHECK(pixels[i + static_cast<std::size_t>(channel)] ==
                          doctest::Approx(wanted[channel]).epsilon(2.0e-6));
                }
            }
        REQUIRE(backend.release_surface(handle).ok());
        ++handle;
    }
}

TEST_CASE("Vulkan backend creates a persistent headless device") {
    auto backend = chronon3d::backends::vulkan::make_vulkan_backend();
    REQUIRE(backend != nullptr);
    const auto* vulkan = dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(backend.get());
    REQUIRE(vulkan != nullptr);
    const auto device_stats = vulkan->stats();
    // The generic registry test can run on any Vulkan implementation. Keep
    // device model and discrete-GPU selection in the hardware-specific probe,
    // rather than making fallback-capable CI depend on this host's board.
    CHECK_FALSE(device_stats.device_name.empty());
    CHECK(vulkan->kernel_registry().size() == 15);
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::ColorMatrix));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Composite));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::GlowPrepare));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Transform));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::AffineTransform));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Blur));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::BloomThreshold));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::ColorAdjust));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Lighting));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::Matte));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextRun));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::FillRect));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::LayerBatch));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextTileBin));
    CHECK(vulkan->kernel_registry().contains(
        chronon3d::backends::vulkan::GpuKernelId::TextTileRaster));
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

TEST_CASE("Vulkan text-run kernel samples a packed glyph atlas into the canvas") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;

    // 2x1 packed glyph atlas holding two 1x1 glyph quads (premultiplied):
    //   atlas (0,0) = solid red, atlas (1,0) = solid green.
    const runtime::SurfaceDesc atlas_desc{
        2, 1, runtime::PixelFormat::Rgba32Float, runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    const runtime::SurfaceDesc canvas_desc{
        4, 2, runtime::PixelFormat::Rgba32Float, runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> atlas{
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f};
    std::vector<float> empty_canvas(4 * 2 * 4, 0.0f);

    REQUIRE(backend.create_surface(601, canvas_desc).ok());
    REQUIRE(backend.create_surface(602, atlas_desc).ok());
    REQUIRE(backend.upload_surface(602, atlas_desc, atlas).ok());
    REQUIRE(backend.upload_surface(601, canvas_desc, empty_canvas).ok());

    // Red glyph at canvas (1,0) full opacity; green glyph at (3,1) half opacity.
    const std::vector<runtime::GlyphInstance> glyphs{
        runtime::GlyphInstance{1, 0, 0, 0, 1, 1, 1.0f, 0.0f},
        runtime::GlyphInstance{3, 1, 1, 0, 1, 1, 0.5f, 0.0f}};
    REQUIRE(backend.draw_text_run_surface(601, 602, glyphs).ok());

    std::vector<float> output(4 * 2 * 4, 0.0f);
    REQUIRE(backend.download_surface(601, output).ok());
    const auto pixel = [&](int x, int y) -> const float* {
        return &output[static_cast<std::size_t>(y * 4 + x) * 4];
    };

    // Glyph A (red, full opacity) landed at (1,0).
    CHECK(pixel(1, 0)[0] == doctest::Approx(1.0f));
    CHECK(pixel(1, 0)[1] == doctest::Approx(0.0f));
    CHECK(pixel(1, 0)[3] == doctest::Approx(1.0f));
    // Glyph B (green, half opacity) landed at (3,1) premultiplied.
    CHECK(pixel(3, 1)[0] == doctest::Approx(0.0f));
    CHECK(pixel(3, 1)[1] == doctest::Approx(0.5f));
    CHECK(pixel(3, 1)[3] == doctest::Approx(0.5f));
    // No bleed outside the glyph quads.
    CHECK(pixel(0, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(2, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(3, 0)[3] == doctest::Approx(0.0f));
    CHECK(pixel(0, 1)[3] == doctest::Approx(0.0f));
    CHECK(pixel(2, 1)[3] == doctest::Approx(0.0f));
}

#endif
