#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_VULKAN
#include "src/backends/vulkan/debug/vulkan_debug_context.hpp"
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>

TEST_SUITE("VulkanDebugContext") {

TEST_CASE("VulkanDebugConfig default values") {
    ::unsetenv("CHRONON3D_VULKAN_VALIDATION");
    ::unsetenv("CHRONON3D_VULKAN_SYNC_VALIDATION");
    ::unsetenv("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION");
    ::unsetenv("CHRONON3D_VULKAN_DEBUG_NAMES");

    const auto cfg = chronon3d::backends::vulkan::VulkanDebugConfig::from_environment();
    CHECK_FALSE(cfg.enable_validation);
    CHECK_FALSE(cfg.enable_sync_validation);
    CHECK_FALSE(cfg.enable_gpu_assisted);
    CHECK(cfg.enable_debug_names);
}

TEST_CASE("VulkanDebugConfig environment parsing") {
    ::setenv("CHRONON3D_VULKAN_VALIDATION", "1", 1);
    ::setenv("CHRONON3D_VULKAN_SYNC_VALIDATION", "true", 1);
    ::setenv("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION", "ON", 1);
    ::setenv("CHRONON3D_VULKAN_DEBUG_NAMES", "0", 1);

    const auto cfg = chronon3d::backends::vulkan::VulkanDebugConfig::from_environment();
    CHECK(cfg.enable_validation);
    CHECK(cfg.enable_sync_validation);
    CHECK(cfg.enable_gpu_assisted);
    CHECK_FALSE(cfg.enable_debug_names);

    ::unsetenv("CHRONON3D_VULKAN_VALIDATION");
    ::unsetenv("CHRONON3D_VULKAN_SYNC_VALIDATION");
    ::unsetenv("CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION");
    ::unsetenv("CHRONON3D_VULKAN_DEBUG_NAMES");
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

    RenderNode filled_path;
    filled_path.shape.set_type(ShapeType::Path);
    filled_path.shape.path().fill = Fill::solid_color(Color{0.2f, 0.7f, 0.9f, 1.0f});
    filled_path.shape.path().stroke.enabled = false;
    filled_path.shape.path().commands = {
        PathCommand::move_to({48.0f, 8.0f}),
        PathCommand::line_to({82.0f, 12.0f}),
        PathCommand::line_to({64.0f, 34.0f}),
        PathCommand::close()};

    // Fase 2: solid closed-shape strokes (rect / rounded-rect / circle,
    // Inside / Center / Outside, 2D uniform scale) are now promoted to the
    // stroked kernel alongside basic fills. Gradients / non-uniform scale /
    // masks still fall back to CPU.
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
    backend->draw_node(framebuffer, circle, state, camera, 96, 64);
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
            const auto ok = backend->draw_node(
                framebuffer, *stroked, state, camera, 96, 64);
            REQUIRE(ok.ok());
        }
    }
    backend->draw_node(framebuffer, line, state, camera, 96, 64);
    backend->draw_node(framebuffer, diagonal, state, camera, 96, 64);
    backend->draw_node(framebuffer, stroked_path, state, camera, 96, 64);
    backend->draw_node(framebuffer, filled_path, state, camera, 96, 64);
    backend->end_frame_batch();

    const auto stats = backend->stats();
    // Six fill/line/path nodes + 3×3 stroked primitives (3 alignments each)
    // all take native passes. The cubic-stroke Path remains rejected.
    CHECK(stats.passes_executed >= 15);
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
