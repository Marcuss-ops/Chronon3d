#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "src/backends/vulkan/debug/vulkan_debug_context.hpp"
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Portability: setenv/unsetenv are POSIX-only; Windows uses _putenv_s
// (convention: see tests/runtime/test_scheduler_determinism.cpp).  // drift-class: historical — retired path (see git history)
namespace {
void env_unset(const char* name) {
#if defined(_WIN32)
    (void)::_putenv_s(name, "");
#else
    (void)::unsetenv(name);
#endif
}
void env_set(const char* name, const char* value) {
#if defined(_WIN32)
    (void)::_putenv_s(name, value);
#else
    (void)::setenv(name, value, 1);
#endif
}
} // anonymous namespace

TEST_SUITE("VulkanDebugContext") {

TEST_CASE("VulkanDebugConfig default values") {
    env_unset("CHRONON3D_VULKAN_VALIDATION");
    env_unset("CHRONON3D_VULKAN_SYNC_VALIDATION");
    env_unset("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION");
    env_unset("CHRONON3D_VULKAN_DEBUG_NAMES");

    const auto cfg = chronon3d::backends::vulkan::VulkanDebugConfig::from_environment();
    CHECK_FALSE(cfg.enable_validation);
    CHECK_FALSE(cfg.enable_sync_validation);
    CHECK_FALSE(cfg.enable_gpu_assisted);
    CHECK(cfg.enable_debug_names);
}

TEST_CASE("VulkanDebugConfig environment parsing") {
    env_set("CHRONON3D_VULKAN_VALIDATION", "1");
    env_set("CHRONON3D_VULKAN_SYNC_VALIDATION", "true");
    env_set("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION", "ON");
    env_set("CHRONON3D_VULKAN_DEBUG_NAMES", "0");

    const auto cfg = chronon3d::backends::vulkan::VulkanDebugConfig::from_environment();
    CHECK(cfg.enable_validation);
    CHECK(cfg.enable_sync_validation);
    CHECK(cfg.enable_gpu_assisted);
    CHECK_FALSE(cfg.enable_debug_names);

    env_unset("CHRONON3D_VULKAN_VALIDATION");
    env_unset("CHRONON3D_VULKAN_SYNC_VALIDATION");
    env_unset("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION");
    env_unset("CHRONON3D_VULKAN_DEBUG_NAMES");
}

TEST_CASE("VulkanDebugContext lifecycle and reporting") {
    using namespace chronon3d::backends::vulkan;
    VulkanDebugContext ctx;

    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    VulkanDebugConfig cfg{};
    cfg.enable_validation = false;
    cfg.enable_debug_names = true;

    ctx.configure_instance_requirements(layers, extensions, cfg);

    CHECK(ctx.report().error_count == 0);
    CHECK(ctx.report().warning_count == 0);
    CHECK(ctx.report().vuids.empty());

    // Object naming before device association should be safe no-op
    ctx.set_image_name(VK_NULL_HANDLE, "test_image");
    ctx.set_buffer_name(VK_NULL_HANDLE, "test_buffer");
    ctx.set_pipeline_name(VK_NULL_HANDLE, "test_pipeline");

    ctx.shutdown();
}

TEST_CASE("VulkanDebugContext attaches to a real Vulkan instance") {
    using namespace chronon3d::backends::vulkan;
    VulkanDebugContext ctx;
    VulkanDebugConfig cfg{};
    cfg.enable_validation = true;
    cfg.enable_debug_names = true;

    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    ctx.configure_instance_requirements(layers, extensions, cfg);

    VkApplicationInfo app_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Chronon3dDebugTest",
        VK_MAKE_VERSION(1, 0, 0), "Chronon3d", VK_MAKE_VERSION(1, 0, 0),
        VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app_info,
        static_cast<std::uint32_t>(layers.size()), layers.data(),
        static_cast<std::uint32_t>(extensions.size()), extensions.data()};
    VkInstance instance = VK_NULL_HANDLE;
    REQUIRE(vkCreateInstance(&instance_info, nullptr, &instance) == VK_SUCCESS);

    ctx.initialize(instance, cfg);
    CHECK(ctx.is_debug_utils_active() ==
          std::any_of(extensions.begin(), extensions.end(), [](const char* name) {
              return std::strcmp(name, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
          }));

    ctx.shutdown();
    vkDestroyInstance(instance, nullptr);
}

TEST_CASE("VulkanBackend records a native solid surface fill") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    auto backend_base = make_vulkan_backend(0);
    auto* backend = dynamic_cast<VulkanBackend*>(backend_base.get());
    REQUIRE(backend != nullptr);

    const runtime::RenderSurfaceHandle handle = 0xC3D001u;
    const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
        64, 48, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::FrameTransient,
        runtime::ColorMetadata{});
    REQUIRE(backend->create_surface(handle, desc).ok());
    backend->begin_frame_batch();
    const auto fill = backend->fill_rect_surface(
        handle, 4, 5, 32, 27, Color{0.25f, 0.5f, 0.75f, 1.0f});
    CHECK(fill.ok());
    backend->end_frame_batch();
    const auto stats = backend->stats();
    CHECK(stats.passes_executed >= 1);
    CHECK(stats.submissions >= 1);
    CHECK(backend->release_surface(handle).ok());
}

TEST_CASE("VulkanBackend records native rounded-rect and circle nodes") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    auto backend_base = make_vulkan_backend(0);
    auto* backend = dynamic_cast<VulkanBackend*>(backend_base.get());
    REQUIRE(backend != nullptr);

    const runtime::RenderSurfaceHandle handle = 0xC3D002u;
    const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
        96, 64, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::FrameTransient,
        runtime::ColorMetadata{});
    REQUIRE(backend->create_surface(handle, desc).ok());

    Framebuffer framebuffer(96, 64);
    framebuffer.set_surface_handle(handle);
    RenderState state;
    state.matrix = Mat4{1.0f};
    state.opacity = 1.0f;
    Camera camera;

    RenderNode rounded;
    rounded.shape.set_type(ShapeType::RoundedRect);
    rounded.shape.rounded_rect().size = {30.0f, 20.0f};
    rounded.shape.rounded_rect().radius = 5.0f;
    rounded.color = Color{0.8f, 0.2f, 0.1f, 1.0f};

    RenderNode circle;
    circle.shape.set_type(ShapeType::Circle);
    circle.shape.circle().radius = 12.0f;
    circle.color = Color{0.1f, 0.4f, 0.9f, 1.0f};

    RenderNode line;
    line.shape.set_type(ShapeType::Line);
    line.shape.line().to = {40.0f, 0.0f, 0.0f};
    line.shape.line().thickness = 3.0f;
    line.shape.line().stroke.width = 3.0f;
    line.shape.line().stroke.color = Color{0.2f, 0.9f, 0.3f, 1.0f};

    RenderNode diagonal;
    diagonal.shape.set_type(ShapeType::Line);
    diagonal.shape.line().to = {36.0f, 24.0f, 0.0f};
    diagonal.shape.line().thickness = 2.0f;
    diagonal.shape.line().stroke.width = 2.0f;
    diagonal.shape.line().stroke.color = Color{0.9f, 0.8f, 0.1f, 1.0f};

    RenderNode stroked_path;
    stroked_path.shape.set_type(ShapeType::Path);
    stroked_path.shape.path().fill.enabled = false;
    stroked_path.shape.path().stroke.width = 2.0f;
    stroked_path.shape.path().commands = {
        PathCommand::move_to({2.0f, 40.0f}),
        PathCommand::cubic_to({12.0f, 20.0f}, {24.0f, 60.0f}, {40.0f, 40.0f})};
    stroked_path.shape.path().stroke.color = Color{0.8f, 0.2f, 0.8f, 1.0f};

    RenderNode dashed_rect_path;
    dashed_rect_path.shape.set_type(ShapeType::Path);
    dashed_rect_path.shape.path().fill = Fill::solid_color(Color{0.1f, 0.2f, 0.3f, 1.0f});
    dashed_rect_path.shape.path().commands = {
        PathCommand::move_to({4.0f, 4.0f}), PathCommand::line_to({34.0f, 4.0f}),
        PathCommand::line_to({34.0f, 28.0f}), PathCommand::line_to({4.0f, 28.0f}),
        PathCommand::close()};
    dashed_rect_path.shape.path().stroke.enabled = true;
    dashed_rect_path.shape.path().stroke.width = 2.0f;
    dashed_rect_path.shape.path().stroke.color = Color{0.9f, 0.7f, 0.2f, 1.0f};
    dashed_rect_path.shape.path().stroke.dash_array = {7.0f, 3.0f, 2.0f};
    dashed_rect_path.shape.path().stroke.dash_offset = 1.5f;

    RenderNode dashed_open_path;
    dashed_open_path.shape.set_type(ShapeType::Path);
    dashed_open_path.shape.path().fill.enabled = false;
    dashed_open_path.shape.path().commands = {
        PathCommand::move_to({4.0f, 38.0f}), PathCommand::line_to({28.0f, 38.0f}),
        PathCommand::line_to({28.0f, 54.0f}), PathCommand::line_to({52.0f, 54.0f})};
    dashed_open_path.shape.path().stroke.width = 2.0f;
    dashed_open_path.shape.path().stroke.color = Color{0.2f, 0.9f, 0.7f, 1.0f};
    dashed_open_path.shape.path().stroke.dash_array = {5.0f, 2.0f, 1.0f};

    RenderNode filled_path;
    filled_path.shape.set_type(ShapeType::Path);
    filled_path.shape.path().fill = Fill::solid_color(Color{0.2f, 0.7f, 0.9f, 1.0f});
    filled_path.shape.path().stroke.enabled = false;
    filled_path.shape.path().commands = {
        PathCommand::move_to({48.0f, 8.0f}),
        PathCommand::line_to({82.0f, 12.0f}),
        PathCommand::line_to({64.0f, 34.0f}),
        PathCommand::close()};

    // Solid closed-shape strokes use the certified solid SDF subset.
    // Gradients / non-uniform scale / masks remain unsupported.
    RenderNode stroked_rounded;
    stroked_rounded.shape.set_type(ShapeType::RoundedRect);
    stroked_rounded.shape.rounded_rect().size = rounded.shape.rounded_rect().size;
    stroked_rounded.shape.rounded_rect().radius = rounded.shape.rounded_rect().radius;
    stroked_rounded.shape.rounded_rect().stroke.enabled = true;
    stroked_rounded.shape.rounded_rect().stroke.color = Color{0.2f, 0.8f, 0.8f, 1.0f};
    stroked_rounded.shape.rounded_rect().stroke.width = 3.0f;
    stroked_rounded.color = Color{0.1f, 0.1f, 0.8f, 1.0f};
    RenderNode stroked_circle;
    stroked_circle.shape.set_type(ShapeType::Circle);
    stroked_circle.shape.circle().radius = circle.shape.circle().radius;
    stroked_circle.shape.circle().stroke.enabled = true;
    stroked_circle.shape.circle().stroke.color = Color{0.9f, 0.2f, 0.2f, 1.0f};
    stroked_circle.shape.circle().stroke.width = 2.0f;
    stroked_circle.color = Color{0.2f, 0.8f, 0.2f, 1.0f};
    RenderNode rect;
    rect.shape.set_type(ShapeType::Rect);
    rect.shape.rect().size = {20.0f, 14.0f};
    rect.color = Color{0.7f, 0.6f, 0.2f, 1.0f};
    RenderNode stroked_rect;
    stroked_rect.shape.set_type(ShapeType::Rect);
    stroked_rect.shape.rect().size = rect.shape.rect().size;
    stroked_rect.shape.rect().stroke.enabled = true;
    stroked_rect.shape.rect().stroke.color = Color{0.8f, 0.2f, 0.6f, 1.0f};
    stroked_rect.shape.rect().stroke.width = 2.0f;
    stroked_rect.color = Color{0.2f, 0.2f, 0.9f, 1.0f};

    backend->begin_frame_batch();
    backend->draw_node(framebuffer, rect, state, camera, 96, 64);
    backend->draw_node(framebuffer, rounded, state, camera, 96, 64);
    REQUIRE(backend->draw_node(framebuffer, circle, state, camera, 96, 64).ok());
    constexpr std::array alignments{
        StrokeAlignment::Inside, StrokeAlignment::Center,
        StrokeAlignment::Outside};
    for (auto* stroked : {&stroked_rect, &stroked_rounded, &stroked_circle}) {
        for (const auto alignment : alignments) {
            switch (stroked->shape.type()) {
            case ShapeType::Rect:
                stroked->shape.rect().stroke.alignment = alignment;
                break;
            case ShapeType::RoundedRect:
                stroked->shape.rounded_rect().stroke.alignment = alignment;
                break;
            case ShapeType::Circle:
                stroked->shape.circle().stroke.alignment = alignment;
                break;
            default:
                FAIL("unexpected stroked primitive in test");
            }
            const auto result = backend->draw_node(
                framebuffer, *stroked, state, camera, 96, 64);
            REQUIRE(result.ok());
        }
    }
    backend->draw_node(framebuffer, line, state, camera, 96, 64);
    backend->draw_node(framebuffer, diagonal, state, camera, 96, 64);
    backend->draw_node(framebuffer, stroked_path, state, camera, 96, 64);
    backend->draw_node(framebuffer, filled_path, state, camera, 96, 64);
    REQUIRE(backend->draw_node(framebuffer, dashed_rect_path, state, camera, 96, 64).ok());
    REQUIRE(backend->draw_node(framebuffer, dashed_open_path, state, camera, 96, 64).ok());
    backend->end_frame_batch();

    const auto stats = backend->stats();
    // Basic fills/lines, the filled path and closed-shape solid strokes take
    // native passes; only the cubic-stroke Path fails closed.
    CHECK(stats.passes_executed >= 6);
    REQUIRE(backend->release_surface(handle).ok());
}

TEST_CASE("Vulkan long dashed polylines match the software stroke oracle") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    auto backend_base = make_vulkan_backend(0);
    auto* backend = dynamic_cast<VulkanBackend*>(backend_base.get());
    REQUIRE(backend != nullptr);

    constexpr int width = 64;
    constexpr int height = 24;
    const runtime::RenderSurfaceHandle handle = 0xC3D100u;
    const auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::FrameTransient,
        runtime::ColorMetadata{});
    REQUIRE(backend->create_surface(handle, desc).ok());
    REQUIRE(backend->initialize_transparent_surface(handle).ok());

    Framebuffer gpu(width, height);
    gpu.set_surface_handle(handle);
    RenderState state;
    state.matrix = Mat4{1.0f};
    Camera camera;
    RenderNode node;
    node.shape.set_type(ShapeType::Path);
    node.shape.path().fill.enabled = false;
    node.shape.path().stroke.width = 3.0f;
    node.shape.path().stroke.color = Color{0.2f, 0.6f, 0.9f, 1.0f};
    node.shape.path().stroke.dash_array = {200.0f, 1.0f};
    node.shape.path().stroke.dash_offset = 1.5f;
    node.shape.path().commands = {
        PathCommand::move_to({2.0f, 2.0f}), PathCommand::line_to({10.0f, 22.0f}),
        PathCommand::line_to({18.0f, 2.0f}), PathCommand::line_to({26.0f, 22.0f}),
        PathCommand::line_to({34.0f, 2.0f}), PathCommand::line_to({42.0f, 22.0f}),
        PathCommand::line_to({50.0f, 2.0f}), PathCommand::line_to({62.0f, 22.0f})};

    RenderNode rectangle;
    rectangle.shape.set_type(ShapeType::Path);
    rectangle.shape.path().fill = Fill::solid_color(Color{0.1f, 0.25f, 0.4f, 1.0f});
    rectangle.shape.path().stroke.width = 2.0f;
    rectangle.shape.path().stroke.color = Color{0.9f, 0.7f, 0.2f, 1.0f};
    rectangle.shape.path().stroke.dash_array = {110.0f, 2.0f};
    rectangle.shape.path().stroke.dash_offset = 1.5f;
    rectangle.shape.path().commands = {
        PathCommand::move_to({8.0f, 4.0f}), PathCommand::line_to({36.0f, 4.0f}),
        PathCommand::line_to({36.0f, 16.0f}), PathCommand::line_to({8.0f, 16.0f}),
        PathCommand::close()};

    RenderNode polygon;
    polygon.shape.set_type(ShapeType::Path);
    polygon.shape.path().fill = Fill::solid_color(Color{0.25f, 0.15f, 0.05f, 1.0f});
    polygon.shape.path().stroke.width = 1.5f;
    polygon.shape.path().stroke.color = Color{0.8f, 0.3f, 0.1f, 1.0f};
    polygon.shape.path().stroke.dash_array = {70.0f, 2.0f};
    polygon.shape.path().commands = {
        PathCommand::move_to({10.0f, 5.0f}), PathCommand::line_to({28.0f, 5.0f}),
        PathCommand::line_to({34.0f, 12.0f}), PathCommand::line_to({24.0f, 20.0f}),
        PathCommand::line_to({8.0f, 16.0f}), PathCommand::close()};

    Framebuffer oracle(width, height);
    renderer::PathRasterizer::draw_path(oracle, node, state, camera, width, height);
    renderer::PathRasterizer::draw_path(oracle, rectangle, state, camera, width, height);
    renderer::PathRasterizer::draw_path(oracle, polygon, state, camera, width, height);
    backend->begin_frame_batch();
    const auto draw = backend->draw_node(gpu, node, state, camera, width, height);
    const auto draw_rectangle = backend->draw_node(
        gpu, rectangle, state, camera, width, height);
    const auto draw_polygon = backend->draw_node(
        gpu, polygon, state, camera, width, height);
    backend->end_frame_batch();
    REQUIRE(draw.ok());
    REQUIRE(draw_rectangle.ok());
    REQUIRE(draw_polygon.ok());

    std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4U);
    REQUIRE(backend->download_surface(handle, rgba).ok());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color expected = oracle.get_pixel(x, y);
            const auto index = (static_cast<std::size_t>(y) * width + x) * 4U;
            CHECK(rgba[index] == doctest::Approx(expected.r).epsilon(1e-5));
            CHECK(rgba[index + 1] == doctest::Approx(expected.g).epsilon(1e-5));
            CHECK(rgba[index + 2] == doctest::Approx(expected.b).epsilon(1e-5));
            CHECK(rgba[index + 3] == doctest::Approx(expected.a).epsilon(1e-5));
        }
    }
    REQUIRE(backend->release_surface(handle).ok());
}

TEST_CASE("Vulkan dashed curves, contours and translucent strokes match software") {
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    auto backend_base = make_vulkan_backend(0);
    auto* backend = dynamic_cast<VulkanBackend*>(backend_base.get());
    REQUIRE(backend != nullptr);

    constexpr int width = 64;
    constexpr int height = 48;
    constexpr runtime::RenderSurfaceHandle handle = 0xC3D101u;
    const auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::FrameTransient,
        runtime::ColorMetadata{});
    REQUIRE(backend->create_surface(handle, desc).ok());
    REQUIRE(backend->initialize_transparent_surface(handle).ok());

    Framebuffer gpu(width, height);
    gpu.set_surface_handle(handle);
    RenderState state;
    state.matrix = Mat4{1.0f};
    Camera camera;
    RenderNode node;
    node.shape.set_type(ShapeType::Path);
    auto& path = node.shape.path();
    path.fill.enabled = false;
    path.stroke.width = 3.0f;
    path.stroke.color = Color{0.25f, 0.7f, 0.45f, 0.48f};
    path.stroke.dash_array = {7.0f, 3.0f, 2.0f};
    path.stroke.dash_offset = 1.25f;
    path.commands = {
        PathCommand::move_to({3.0f, 5.0f}),
        PathCommand::cubic_to({10.0f, 1.0f}, {18.0f, 22.0f}, {26.0f, 10.0f}),
        PathCommand::quadratic_to({32.0f, 2.0f}, {38.0f, 13.0f}),
        PathCommand::move_to({5.0f, 34.0f}),
        PathCommand::line_to({15.0f, 31.0f}),
        PathCommand::line_to({23.0f, 43.0f})};

    Framebuffer oracle(width, height);
    renderer::PathRasterizer::draw_path(oracle, node, state, camera, width, height);
    backend->begin_frame_batch();
    const auto draw = backend->draw_node(gpu, node, state, camera, width, height);
    backend->end_frame_batch();
    REQUIRE(draw.ok());

    std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4U);
    REQUIRE(backend->download_surface(handle, rgba).ok());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color expected = oracle.get_pixel(x, y);
            const auto index = (static_cast<std::size_t>(y) * width + x) * 4U;
            CHECK(rgba[index] == doctest::Approx(expected.r).epsilon(1e-5));
            CHECK(rgba[index + 1] == doctest::Approx(expected.g).epsilon(1e-5));
            CHECK(rgba[index + 2] == doctest::Approx(expected.b).epsilon(1e-5));
            CHECK(rgba[index + 3] == doctest::Approx(expected.a).epsilon(1e-5));
        }
    }
    REQUIRE(backend->release_surface(handle).ok());
}

TEST_CASE("Vulkan native N-vertex path fills match the software rasterizer") {
    // TICKET-MAP-VECTOR-FILL-NATIVE-20261116. Fill geometry travels through
    // the path storage buffer now, so a plate past the eight push-constant
    // vertices stays native. Stroked paths clip the stroke against their fill
    // polygon from that same buffer, and the fill interior gets its own pass:
    // without it a solid-stroked path drew a hollow shape, because the stroke
    // shader returns early on interior pixels.
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    auto backend_base = make_vulkan_backend(0);
    auto* backend = dynamic_cast<VulkanBackend*>(backend_base.get());
    REQUIRE(backend != nullptr);

    constexpr int width = 96;
    constexpr int height = 96;
    RenderState state;
    state.matrix = Mat4{1.0f};
    Camera camera;

    // Twelve vertices with two radii: every edge is diagonal, so the analytic
    // coverage ramp is exercised, and the vertex count is past the old cap.
    RenderNode node;
    node.shape.set_type(ShapeType::Path);
    auto& path = node.shape.path();
    path.fill = Fill::solid_color(Color{0.10f, 0.35f, 0.60f, 1.0f});
    std::vector<Vec2> plate_vertices;
    for (int i = 0; i < 12; ++i) {
        const float angle = static_cast<float>(i) * 6.2831853f / 12.0f;
        plate_vertices.push_back({48.0f + (32.0f * std::cos(angle)),
                                  48.0f + (26.0f * std::sin(angle))});
    }
    auto add_plate_commands = [&path, &plate_vertices] {
        path.commands.push_back(PathCommand::move_to(plate_vertices.front()));
        for (std::size_t i = 1; i < plate_vertices.size(); ++i)
            path.commands.push_back(PathCommand::line_to(plate_vertices[i]));
        path.commands.push_back(PathCommand::close());
    };
    add_plate_commands();
    REQUIRE(plate_vertices.size() == 12);

    // The compute fill smooths its edge over a one-pixel analytic ramp, the
    // CPU rasterizer integrates exact pixel area, and a stroked path's interior
    // uses binary coverage (the stroke covers its seam). The stroke itself may
    // fill a vertex join corner the software path leaves empty. All three agree
    // on geometry, so the comparison is exact outside a band that contains the
    // outline and the outer half of the stroke, and coverage is compared across
    // the whole fixture to catch a fill that is missing or misplaced.
    const auto distance_to_outline = [&plate_vertices](float x, float y) {
        float best = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < plate_vertices.size(); ++i) {
            const Vec2 a = plate_vertices[i];
            const Vec2 b = plate_vertices[(i + 1U) % plate_vertices.size()];
            const Vec2 ab = b - a;
            const float length_sq = ab.x * ab.x + ab.y * ab.y;
            const float t = length_sq <= 1e-9f ? 0.0f : std::clamp(
                (((x - a.x) * ab.x) + ((y - a.y) * ab.y)) / length_sq, 0.0f, 1.0f);
            const float dx = x - (a.x + (t * ab.x));
            const float dy = y - (a.y + (t * ab.y));
            best = std::min(best, std::sqrt((dx * dx) + (dy * dy)));
        }
        return best;
    };

    struct StrokeCase {
        const char* name;
        bool enabled;
        float width;
        std::vector<float> dash_array;
    };
    const std::array<StrokeCase, 3> cases{{
        {"fill only", false, 0.0f, {}},
        {"fill plus solid stroke", true, 5.0f, {}},
        {"fill plus dashed stroke", true, 4.0f, {9.0f, 4.0f}},
    }};

    std::uint32_t handle_seed = 0xC3D200u;
    for (const auto& stroke_case : cases) {
        INFO("case=" << std::string(stroke_case.name));
        node.shape.path().stroke.enabled = stroke_case.enabled;
        node.shape.path().stroke.width = stroke_case.width;
        node.shape.path().stroke.color = Color{0.90f, 0.70f, 0.20f, 1.0f};
        node.shape.path().stroke.dash_array = stroke_case.dash_array;

        const runtime::RenderSurfaceHandle handle = handle_seed++;
        const auto desc = runtime::SurfaceDesc::make(
            width, height, runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Generic,
            runtime::LifetimeClass::FrameTransient,
            runtime::ColorMetadata{});
        REQUIRE(backend->create_surface(handle, desc).ok());
        REQUIRE(backend->initialize_transparent_surface(handle).ok());
        Framebuffer gpu(width, height);
        gpu.set_surface_handle(handle);

        Framebuffer oracle(width, height);
        renderer::PathRasterizer::draw_path(oracle, node, state, camera, width, height);
        backend->begin_frame_batch();
        const auto draw = backend->draw_node(gpu, node, state, camera, width, height);
        backend->end_frame_batch();
        REQUIRE(draw.ok());

        std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4U);
        REQUIRE(backend->download_surface(handle, rgba).ok());
        std::size_t mismatches = 0;
        std::size_t compared = 0;
        double gpu_total = 0.0;
        double software_total = 0.0;
        double gpu_edge = 0.0;
        double software_edge = 0.0;
        const float edge_band = stroke_case.enabled
            ? (stroke_case.width * 0.5f) + 1.0f : 1.5f;
        std::string first_mismatch;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const Color expected = oracle.get_pixel(x, y);
                const auto index = (static_cast<std::size_t>(y) * width + x) * 4U;
                const bool interior =
                    distance_to_outline(static_cast<float>(x) + 0.5f,
                                        static_cast<float>(y) + 0.5f) >= edge_band;
                gpu_total += rgba[index + 3];
                software_total += expected.a;
                if (!interior) {
                    gpu_edge += rgba[index + 3];
                    software_edge += expected.a;
                    continue;
                }
                ++compared;
                const bool matches =
                    std::abs(rgba[index] - expected.r) <= 1e-4f &&
                    std::abs(rgba[index + 1] - expected.g) <= 1e-4f &&
                    std::abs(rgba[index + 2] - expected.b) <= 1e-4f &&
                    std::abs(rgba[index + 3] - expected.a) <= 1e-4f;
                if (matches) continue;
                if (mismatches < 3) {
                    std::ostringstream line;
                    line << "pixel (" << x << ", " << y << ") gpu=("
                         << rgba[index] << ", " << rgba[index + 1] << ", "
                         << rgba[index + 2] << ", " << rgba[index + 3]
                         << ") software=(" << expected.r << ", " << expected.g
                         << ", " << expected.b << ", " << expected.a << ") | ";
                    first_mismatch += line.str();
                }
                ++mismatches;
            }
        }
        INFO("case=" << std::string(stroke_case.name)
                      << " interior pixels compared: " << compared
                      << " mismatched: " << mismatches << " " << first_mismatch);
        CHECK(compared > 4000);
        CHECK(mismatches == 0);
        // Edge coverage may use a different kernel, but it must not trade area:
        // a misplaced, missing or doubled fill moves these sums far more.
        INFO("case=" << std::string(stroke_case.name)
                      << " coverage gpu=" << gpu_total << " software=" << software_total
                      << " edge gpu=" << gpu_edge << " software=" << software_edge);
        REQUIRE(software_total > 1000.0);
        REQUIRE(software_edge > 100.0);
        CHECK(std::abs(gpu_total - software_total) / software_total < 0.005);
        CHECK(std::abs(gpu_edge - software_edge) / software_edge < 0.05);
        REQUIRE(backend->release_surface(handle).ok());
    }
}

TEST_CASE("Vulkan stroke-only path with round caps draws through draw_node") {
    // Regression for TICKET-VULKAN-STROKE-ONLY-DRAW. A fill-less path whose
    // stroke uses round caps used to be rejected by try_native_path_stroke
    // (it accepted LineCap::Butt only); with no fallback attached draw_node
    // then reported UnsupportedCapability while the CLI kept exit 0 and wrote
    // a background-only frame. The kernel-level test in
    // test_backend_registry_vulkan_dispatch.cpp could not catch this because
    // it calls fill_dashed_path_surface directly, bypassing the routing.
    using namespace chronon3d;
    using namespace chronon3d::backends::vulkan;

    auto backend_base = make_vulkan_backend(0);
    auto* backend = dynamic_cast<VulkanBackend*>(backend_base.get());
    REQUIRE(backend != nullptr);

    constexpr int width = 64;
    constexpr int height = 64;
    constexpr runtime::RenderSurfaceHandle handle = 0xC3D102u;
    const auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Generic,
        runtime::LifetimeClass::FrameTransient,
        runtime::ColorMetadata{});
    REQUIRE(backend->create_surface(handle, desc).ok());
    REQUIRE(backend->initialize_transparent_surface(handle).ok());

    Framebuffer gpu(width, height);
    gpu.set_surface_handle(handle);
    RenderState state;
    state.matrix = Mat4{1.0f};
    Camera camera;

    RenderNode ring;
    ring.shape.set_type(ShapeType::Path);
    auto& path = ring.shape.path();
    path.fill.enabled = false;
    path.stroke.enabled = true;
    path.stroke.width = 4.0f;
    path.stroke.color = Color{0.49f, 0.49f, 0.39f, 1.0f};
    path.stroke.cap = LineCap::Round;
    constexpr int segments = 32;
    for (int i = 0; i <= segments; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 *
            static_cast<double>(i) / static_cast<double>(segments);
        const auto x = static_cast<float>(32.0 + 20.0 * std::cos(angle));
        const auto y = static_cast<float>(32.0 + 20.0 * std::sin(angle));
        if (i == 0)
            path.commands.push_back(PathCommand::move_to({x, y}));
        else
            path.commands.push_back(PathCommand::line_to({x, y}));
    }
    path.commands.push_back(PathCommand::close());

    Framebuffer oracle(width, height);
    renderer::PathRasterizer::draw_path(oracle, ring, state, camera, width, height);
    std::size_t oracle_covered = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (oracle.get_pixel(x, y).a > 0.05f) ++oracle_covered;
        }
    }
    // The software oracle must paint the ring, otherwise the comparison below
    // would silently degrade into "nothing versus nothing".
    REQUIRE(oracle_covered > 0);

    backend->begin_frame_batch();
    const auto draw = backend->draw_node(gpu, ring, state, camera, width, height);
    backend->end_frame_batch();
    REQUIRE(draw.ok());

    std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4U);
    REQUIRE(backend->download_surface(handle, rgba).ok());
    std::size_t gpu_covered = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = (static_cast<std::size_t>(y) * width + x) * 4U;
            if (rgba[index + 3] > 0.05f) ++gpu_covered;
            const Color expected = oracle.get_pixel(x, y);
            CHECK(rgba[index] == doctest::Approx(expected.r).epsilon(1e-5));
            CHECK(rgba[index + 1] == doctest::Approx(expected.g).epsilon(1e-5));
            CHECK(rgba[index + 2] == doctest::Approx(expected.b).epsilon(1e-5));
            CHECK(rgba[index + 3] == doctest::Approx(expected.a).epsilon(1e-5));
        }
    }
    // Fail-closed guard: the native route must actually produce coverage.
    CHECK(gpu_covered > 0);
    CHECK(gpu_covered == oracle_covered);
    REQUIRE(backend->release_surface(handle).ok());
}

TEST_CASE("VulkanDebugContext object naming helpers coverage") {
    using namespace chronon3d::backends::vulkan;
    VulkanDebugContext ctx;
    ctx.set_image_name(VK_NULL_HANDLE, "img");
    ctx.set_buffer_name(VK_NULL_HANDLE, "buf");
    ctx.set_pipeline_name(VK_NULL_HANDLE, "pipe");
    ctx.set_semaphore_name(VK_NULL_HANDLE, "sem");
    ctx.set_fence_name(VK_NULL_HANDLE, "fence");
    ctx.set_command_buffer_name(VK_NULL_HANDLE, "cmd");
    ctx.set_pipeline_layout_name(VK_NULL_HANDLE, "playout");
    ctx.set_descriptor_set_layout_name(VK_NULL_HANDLE, "dlayout");
    ctx.set_descriptor_pool_name(VK_NULL_HANDLE, "dpool");
    ctx.set_descriptor_set_name(VK_NULL_HANDLE, "dset");
    ctx.set_image_view_name(VK_NULL_HANDLE, "view");
    ctx.set_sampler_name(VK_NULL_HANDLE, "sampler");
    ctx.set_query_pool_name(VK_NULL_HANDLE, "qpool");
    ctx.set_command_pool_name(VK_NULL_HANDLE, "cpool");
    CHECK(ctx.report().error_count == 0);
}

TEST_CASE("VulkanDebugContext generates structured validation artifact") {
    using namespace chronon3d::backends::vulkan;
    VulkanDebugContext ctx;
    const std::string artifact_path = "vulkan_validation_test_report.json";
    bool written = ctx.write_validation_artifact(artifact_path, "TestGPU", "1.0", "1.2");
    CHECK(written);
    std::remove(artifact_path.c_str());
}

} // TEST_SUITE
#endif
