// test_backend_registry_vulkan_surfaces.cpp — Split from
// test_backend_registry_vulkan.cpp: color-adjust / EffectStackNode /
// composite / source-over parity surface coverage (Vulkan-gated as in
// the origin).
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
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/render_graph/checkbackend.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <src/backends/software/effects/render_effects_processor.hpp>
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

#ifdef CHRONON3D_ENABLE_VULKAN
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

TEST_CASE("JobSession stores frame-addressed Vulkan Echo surfaces without readback") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    auto runtime_result = RuntimeContext::create(
        RuntimeConfig{.config = Config{}, .assets_root = std::nullopt});
    REQUIRE(runtime_result);
    JobSession job(*runtime_result.value());
    constexpr std::size_t mib = 1024U * 1024U;
    CHECK(job.reserve_native_echo_history("budget-a", 256U * mib, 2));
    CHECK_FALSE(job.reserve_native_echo_history("budget-b", 256U * mib, 2));
    CHECK_FALSE(job.reserve_native_echo_history("oversized", 1024U * mib, 2));
    job.clear_native_echo_history();
    CHECK(job.reserve_native_echo_history("budget-b", 256U * mib, 2));
    job.clear_native_echo_history();
    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry registry;
    const SurfaceDesc desc{
        2, 2, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0};
    const auto scratch_slot0 = job.native_echo_scratch(
        "scratch/layer", 0, desc, backend, registry);
    REQUIRE(scratch_slot0);
    CHECK(std::all_of(scratch_slot0->begin(), scratch_slot0->end(),
        [&](RenderSurfaceHandle handle) { return backend.is_native_surface_valid(handle); }));
    CHECK(job.native_echo_scratch("scratch/layer", 0, desc, backend, registry) ==
          scratch_slot0);
    const auto scratch_slot1 = job.native_echo_scratch(
        "scratch/layer", 1, desc, backend, registry);
    REQUIRE(scratch_slot1);
    CHECK(*scratch_slot1 != *scratch_slot0);
    job.clear_native_echo_history();
    CHECK(std::all_of(scratch_slot0->begin(), scratch_slot0->end(),
        [&](RenderSurfaceHandle handle) { return !backend.is_native_surface_valid(handle); }));
    CHECK(std::all_of(scratch_slot1->begin(), scratch_slot1->end(),
        [&](RenderSurfaceHandle handle) { return !backend.is_native_surface_valid(handle); }));

    const auto source = registry.create(desc);
    REQUIRE(source != kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source, desc).ok());
    const std::vector<float> pixels{
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        0.25f, 0.5f, 0.75f, 1.0f};
    REQUIRE(backend.upload_surface(source, desc, pixels).ok());

    REQUIRE(job.record_native_echo_frame(
        "layer/effect", 10, source, 4, backend, registry));
    auto before = job.native_echo_frames("layer/effect", 11, 1, 1);
    REQUIRE(before.size() == 1);
    REQUIRE(before[0]);
    CHECK(before[0]->handle != source);
    CHECK(before[0]->desc.lifetime == LifetimeClass::JobPersistent);

    std::vector<float> copied(pixels.size(), 0.0f);
    REQUIRE(backend.download_surface(before[0]->handle, copied).ok());
    CHECK(copied == pixels);
    CHECK_FALSE(job.native_echo_frames("layer/effect", 12, 1, 1)[0]);
    CHECK_FALSE(job.native_echo_frames("other/layer", 11, 1, 1)[0]);

    job.clear_native_echo_history();
    CHECK_FALSE(job.native_echo_frames("layer/effect", 11, 1, 1)[0]);
    before.clear();

    RenderSurfaceHandle released_by_destructor = kInvalidRenderSurfaceHandle;
    {
        JobSession scoped_job(*runtime_result.value());
        REQUIRE(scoped_job.record_native_echo_frame(
            "destructor/layer", 20, source, 2, backend, registry));
        const auto retained = scoped_job.native_echo_frames(
            "destructor/layer", 21, 1, 1);
        REQUIRE(retained[0]);
        released_by_destructor = retained[0]->handle;
    }
    CHECK_FALSE(backend.is_native_surface_valid(released_by_destructor));
    CHECK_FALSE(registry.lookup(released_by_destructor).has_value());
    REQUIRE(backend.release_surface(source).ok());
    CHECK(registry.release(source));
}

TEST_CASE("EffectStackNode composes Echo, brightness and chroma key over time") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    using namespace chronon3d::runtime;

    auto runtime_result = RuntimeContext::create(
        RuntimeConfig{.config = Config{}, .assets_root = std::nullopt});
    REQUIRE(runtime_result);
    auto& runtime = *runtime_result.value();
    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry surfaces;
    JobSession job(runtime);
    RenderGraphContext ctx;
    ctx.frame_input.width = 4;
    ctx.frame_input.height = 2;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;
    EffectStack effects;
    effects.push_back(effects::EffectInstance{EchoParams{
        .echoes = 1, .decay = 0.5f, .time_offset_frames = 1}});
    effects.push_back(effects::EffectInstance{BrightnessParams{.value = 0.1f}});
    effects.push_back(effects::EffectInstance{KeyingParams{
        .mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 1.0f, 0.0f, 1.0f},
        .threshold = 0.16f,
        .softness = 0.42f,
        .spill = 0.12f}});
    EffectStackNode node(std::move(effects));
    node.set_temporal_history_key("echo-integration/layer0/effect0");

    const SurfaceDesc desc{
        4, 2, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0};
    std::unordered_map<FrameSlotId, std::array<RenderSurfaceHandle, 3>>
        echo_scratch_by_slot;
    const auto render_frame = [&](std::int64_t frame_number, std::size_t lit_pixel) {
        const auto source_handle = surfaces.create(desc);
        REQUIRE(source_handle != kInvalidRenderSurfaceHandle);
        REQUIRE(backend.create_surface(source_handle, desc).ok());
        std::vector<float> pixels(4 * 2 * 4, 0.0f);
        const auto base = lit_pixel * 4;
        pixels[base + (frame_number == 0 ? 1 : 0)] = 1.0f;
        pixels[base + 3] = 1.0f;
        REQUIRE(backend.upload_surface(source_handle, desc, pixels).ok());
        auto source = std::make_shared<Framebuffer>(4, 2, static_cast<Color*>(nullptr));
        source->set_surface_handle(source_handle);
        ctx.frame_input.frame = Frame{frame_number};
        ctx.frame_input.sample_time = SampleTime::from_frame_int(
            Frame{frame_number}, FrameRate{});
        auto* slot = job.slots().try_acquire(FrameSlotState::Evaluating);
        REQUIRE(slot != nullptr);
        auto& payload = job.slots().payload(slot->slot_id);
        FrameExecution execution(
            job, Frame{frame_number}, ctx.frame_input.sample_time, *slot, payload);
        ctx.execution = &execution;
        const std::array<FramebufferRef, 1> inputs{source.get()};
        const std::array<std::optional<raster::BBox>, 1> bboxes{
            raster::BBox{0, 0, 4, 2}};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.ok());
        REQUIRE(result.value() != nullptr);
        const auto scratch = job.native_echo_scratch(
            "echo-integration/layer0/effect0", slot->slot_id, desc,
            backend, surfaces);
        REQUIRE(scratch);
        const auto [scratch_it, inserted] = echo_scratch_by_slot.emplace(
            slot->slot_id, *scratch);
        if (!inserted) CHECK(scratch_it->second == *scratch);
        const auto output_handle = result.value()->surface_handle();
        REQUIRE(output_handle != kInvalidRenderSurfaceHandle);
        std::vector<float> output(4 * 2 * 4, 0.0f);
        REQUIRE(backend.download_surface(output_handle, output).ok());
        execution.complete();
        ctx.execution = nullptr;
        job.slots().release(*slot);
        REQUIRE(backend.release_surface(output_handle).ok());
        CHECK(surfaces.release(output_handle));
        REQUIRE(backend.release_surface(source_handle).ok());
        CHECK(surfaces.release(source_handle));
        return output;
    };

    const auto first = render_frame(0, 0);
    const auto second = render_frame(1, 1);

    // Keep a real software-processor golden beside the native graph result.
    // Inputs and temporal context match the two Vulkan frames above.
    renderer::SoftwareRegistry software_registry;
    renderer::register_builtin_processors(software_registry);
    EffectScratchResources software_scratch;
    EffectStack software_stack;
    software_stack.push_back(effects::EffectInstance{EchoParams{
        .echoes = 1, .decay = 0.5f, .time_offset_frames = 1}});
    software_stack.push_back(effects::EffectInstance{BrightnessParams{.value = 0.1f}});
    software_stack.push_back(effects::EffectInstance{KeyingParams{
        .mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 1.0f, 0.0f, 1.0f},
        .threshold = 0.16f,
        .softness = 0.42f,
        .spill = 0.12f}});
    std::array<std::vector<float>, 2> software_frames;
    for (std::int64_t frame_number = 0; frame_number < 2; ++frame_number) {
        Framebuffer input(4, 2);
        input.clear(Color::transparent());
        const std::size_t lit_pixel = static_cast<std::size_t>(frame_number);
        input.set_pixel(static_cast<int>(lit_pixel % 4),
                        static_cast<int>(lit_pixel / 4), frame_number == 0
                            ? Color{0, 1, 0, 1} : Color{1, 0, 0, 1});
        effects::EffectExecutionContext effect_context;
        effect_context.frame = Frame{frame_number};
        effect_context.temporal_history_key = "echo-integration/layer0/effect0";
        effect_context.effect_scratch = &software_scratch;
        renderer::apply_effect_stack(input, software_stack, effect_context, software_registry);
        auto& pixels = software_frames[static_cast<std::size_t>(frame_number)];
        pixels.reserve(4 * 2 * 4);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) {
                const Color pixel = input.get_pixel(x, y);
                pixels.insert(pixels.end(), {pixel.r, pixel.g, pixel.b, pixel.a});
            }
    }
    REQUIRE(first.size() == software_frames[0].size());
    REQUIRE(second.size() == software_frames[1].size());
    for (std::size_t i = 0; i < first.size(); ++i)
        CHECK(first[i] == doctest::Approx(software_frames[0][i]).epsilon(1e-6));
    for (std::size_t i = 0; i < second.size(); ++i)
        CHECK(second[i] == doctest::Approx(software_frames[1][i]).epsilon(1e-6));

    // Stable 8-bit display-space framehash for the second frame. Quantizing
    // through the canonical linear-to-sRGB conversion keeps this an image
    // canary rather than a hash of backend-specific float representation.
    std::uint64_t framehash = 14695981039346656037ULL;
    for (std::size_t pixel = 0; pixel < second.size() / 4; ++pixel) {
        const auto base = pixel * 4;
        const std::uint8_t rgba[] = {
            Color::linear_to_srgb8(second[base]),
            Color::linear_to_srgb8(second[base + 1]),
            Color::linear_to_srgb8(second[base + 2]),
            static_cast<std::uint8_t>(std::clamp(second[base + 3], 0.0f, 1.0f) * 255.0f + 0.5f)};
        for (const auto byte : rgba) {
            framehash ^= byte;
            framehash *= 1099511628211ULL;
        }
    }
    CHECK(framehash == 0xf6cf6fc8e59fbd8bULL);
}

TEST_CASE("AdjustmentNode preserves ordered color, Echo and chroma-key stages") {
    using namespace chronon3d;
    using namespace chronon3d::graph;
    using namespace chronon3d::runtime;

    auto runtime_result = RuntimeContext::create(
        RuntimeConfig{.config = Config{}, .assets_root = std::nullopt});
    REQUIRE(runtime_result);
    auto& runtime = *runtime_result.value();
    backends::vulkan::VulkanBackend backend;
    RenderSurfaceRegistry surfaces;
    JobSession job(runtime);
    RenderGraphContext ctx;
    ctx.frame_input.width = 4;
    ctx.frame_input.height = 2;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;

    EffectStack effects;
    effects.emplace_back(BrightnessParams{.value = 0.1f});
    effects.emplace_back(EchoParams{
        .echoes = 1, .decay = 0.5f, .time_offset_frames = 1});
    effects.emplace_back(KeyingParams{
        .mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 1.0f, 0.0f, 1.0f},
        .threshold = 0.12f,
        .softness = 0.52f,
        .spill = 0.2f});
    AdjustmentNode node(std::move(effects));
    node.set_temporal_history_key("echo-integration/color-echo-chroma");
    CHECK(node.temporal_requirements().history_frames == 1);

    const SurfaceDesc desc{
        4, 2, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 0};
    std::array<std::vector<float>, 2> native_frames;
    for (std::int64_t frame_number = 0; frame_number < 2; ++frame_number) {
        const auto source_handle = surfaces.create(desc);
        REQUIRE(source_handle != kInvalidRenderSurfaceHandle);
        REQUIRE(backend.create_surface(source_handle, desc).ok());
        std::vector<float> pixels(4 * 2 * 4, 0.0f);
        const auto base = static_cast<std::size_t>(frame_number) * 4;
        pixels[base + 1] = 1.0f;
        pixels[base + 3] = 1.0f;
        REQUIRE(backend.upload_surface(source_handle, desc, pixels).ok());
        auto source = std::make_shared<Framebuffer>(4, 2, static_cast<Color*>(nullptr));
        source->set_surface_handle(source_handle);
        ctx.frame_input.frame = Frame{frame_number};
        ctx.frame_input.sample_time = SampleTime::from_frame_int(
            Frame{frame_number}, FrameRate{});
        auto* slot = job.slots().try_acquire(FrameSlotState::Evaluating);
        REQUIRE(slot != nullptr);
        auto& payload = job.slots().payload(slot->slot_id);
        FrameExecution execution(
            job, Frame{frame_number}, ctx.frame_input.sample_time, *slot, payload);
        ctx.execution = &execution;
        const std::array<FramebufferRef, 1> inputs{source.get()};
        const std::array<std::optional<raster::BBox>, 1> bboxes{
            raster::BBox{0, 0, 4, 2}};
        auto result = node.execute(ctx, inputs, bboxes);
        REQUIRE(result.ok());
        const auto output_handle = result.value()->surface_handle();
        REQUIRE(output_handle != kInvalidRenderSurfaceHandle);
        auto& output = native_frames[static_cast<std::size_t>(frame_number)];
        output.resize(4 * 2 * 4);
        REQUIRE(backend.download_surface(output_handle, output).ok());
        execution.complete();
        ctx.execution = nullptr;
        job.slots().release(*slot);
        REQUIRE(backend.release_surface(output_handle).ok());
        CHECK(surfaces.release(output_handle));
        REQUIRE(backend.release_surface(source_handle).ok());
        CHECK(surfaces.release(source_handle));
    }

    renderer::SoftwareRegistry software_registry;
    renderer::register_builtin_processors(software_registry);
    EffectScratchResources software_scratch;
    EffectStack software_stack;
    software_stack.emplace_back(BrightnessParams{.value = 0.1f});
    software_stack.emplace_back(EchoParams{
        .echoes = 1, .decay = 0.5f, .time_offset_frames = 1});
    software_stack.emplace_back(KeyingParams{
        .mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 1.0f, 0.0f, 1.0f},
        .threshold = 0.12f,
        .softness = 0.52f,
        .spill = 0.2f});
    for (std::int64_t frame_number = 0; frame_number < 2; ++frame_number) {
        Framebuffer input(4, 2);
        input.clear(Color::transparent());
        input.set_pixel(static_cast<int>(frame_number), 0, Color{0, 1, 0, 1});
        effects::EffectExecutionContext effect_context;
        effect_context.frame = Frame{frame_number};
        effect_context.temporal_history_key = "echo-integration/color-echo-chroma";
        effect_context.effect_scratch = &software_scratch;
        renderer::apply_effect_stack(
            input, software_stack, effect_context, software_registry);
        const auto& actual = native_frames[static_cast<std::size_t>(frame_number)];
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) {
                const Color expected = input.get_pixel(x, y);
                const float channels[]{expected.r, expected.g, expected.b, expected.a};
                const auto pixel = static_cast<std::size_t>((y * 4 + x) * 4);
                for (int channel = 0; channel < 4; ++channel)
                    CHECK(actual[pixel + static_cast<std::size_t>(channel)] ==
                          doctest::Approx(channels[channel]).epsilon(1e-6));
            }
    }
}

TEST_CASE("Vulkan lighting preserves alpha and matches the CPU multiplier oracle") {
    using namespace chronon3d;

    backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t width = 2;
    constexpr std::uint32_t height = 2;
    const runtime::SurfaceDesc desc{
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const std::vector<float> source{
        0.2f, 0.4f, 0.6f, 1.0f,
        0.8f, 0.1f, 0.3f, 0.5f,
        0.9f, 0.9f, 0.9f, 0.0f,
        0.0f, 0.2f, 1.0f, 1.0f};
    const Color multiplier{0.25f, 0.5f, 0.75f, 1.0f};
    std::vector<float> actual(source.size(), 0.0f);

    REQUIRE(backend.create_surface(461, desc).ok());
    REQUIRE(backend.create_surface(462, desc).ok());
    REQUIRE(backend.upload_surface(462, desc, source).ok());
    REQUIRE(backend.lighting_surface(461, 462, multiplier).ok());
    REQUIRE(backend.download_surface(461, actual).ok());

    for (std::size_t pixel = 0; pixel < source.size() / 4; ++pixel) {
        const auto base = pixel * 4;
        if (source[base + 3] == 0.0f) {
            CHECK(actual[base + 0] == doctest::Approx(source[base + 0]));
            CHECK(actual[base + 1] == doctest::Approx(source[base + 1]));
            CHECK(actual[base + 2] == doctest::Approx(source[base + 2]));
        } else {
            CHECK(actual[base + 0] == doctest::Approx(source[base + 0] * multiplier.r));
            CHECK(actual[base + 1] == doctest::Approx(source[base + 1] * multiplier.g));
            CHECK(actual[base + 2] == doctest::Approx(source[base + 2] * multiplier.b));
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
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(8, 8);
    source->clear(Color::transparent());
    source->set_pixel(4, 4, Color{1.0f, 0.5f, 0.25f, 1.0f});

    GlowParams params;
    params.radius = 2.0f;
    params.intensity = 0.5f;
    params.color = Color{0.25f, 0.5f, 1.0f, 1.0f};
    // The native glow path uses the same single-pass contract as software.
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
    CHECK(output[center + 0] == doctest::Approx(1.0f));
    CHECK(output[neighbor + 0] > 0.0f);

    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
}

TEST_CASE("EffectStackNode glow reads an available native surface when CPU mirror is stale") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    const runtime::SurfaceDesc desc{
        2, 2, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const auto source_handle = surfaces.create(desc);
    REQUIRE(source_handle != runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source_handle, desc).ok());
    std::vector<float> pixels(2 * 2 * 4, 0.0f);
    const auto center = static_cast<std::size_t>((1 * 2 + 1) * 4);
    pixels[center + 0] = 1.0f;
    pixels[center + 3] = 1.0f;
    REQUIRE(backend.upload_surface(source_handle, desc, pixels).ok());

    auto source = std::make_shared<Framebuffer>(2, 2);
    source->set_origin(3, 3);
    source->clear(Color::transparent()); // deliberately stale CPU mirror
    source->set_surface_handle(source_handle);
    GlowParams params{.radius = 1.5f, .intensity = 0.5f};
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{3, 3, 5, 5}};

    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    const auto output_handle = result.value()->surface_handle();
    REQUIRE(output_handle != runtime::kInvalidRenderSurfaceHandle);
    CHECK(result.value()->width() > source->width());
    CHECK(result.value()->height() > source->height());
    const auto output_x = 4 - result.value()->origin_x();
    const auto output_y = 4 - result.value()->origin_y();
    const auto output_center = static_cast<std::size_t>(
        (output_y * result.value()->width() + output_x) * 4);
    std::vector<float> output(static_cast<std::size_t>(
        result.value()->width() * result.value()->height() * 4), 0.0f);
    REQUIRE(backend.download_surface(output_handle, output).ok());
    CHECK(output[output_center] == doctest::Approx(1.0f));
    CHECK(output[output_center + 3] > 0.0f);
    const auto output_neighbor = output_center + 4;
    REQUIRE(output_neighbor + 3 < output.size());
    CHECK(output[output_neighbor] > 0.0f);

    REQUIRE(backend.release_surface(output_handle).ok());
    CHECK(surfaces.release(output_handle));
    REQUIRE(backend.release_surface(source_handle).ok());
    CHECK(surfaces.release(source_handle));
}

TEST_CASE("EffectStackNode blurs a GPU-only input without uploading a CPU framebuffer") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;
    ctx.policy.require_native_gpu = true;

    const runtime::SurfaceDesc desc{
        8, 8, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient, 0};
    const auto source_handle = surfaces.create(desc);
    REQUIRE(source_handle != runtime::kInvalidRenderSurfaceHandle);
    REQUIRE(backend.create_surface(source_handle, desc).ok());
    std::vector<float> pixels(8 * 8 * 4, 0.0f);
    const auto center = static_cast<std::size_t>((4 * 8 + 4) * 4);
    pixels[center + 0] = 1.0f;
    pixels[center + 1] = 0.5f;
    pixels[center + 2] = 0.25f;
    pixels[center + 3] = 1.0f;
    REQUIRE(backend.upload_surface(source_handle, desc, pixels).ok());

    auto source = std::make_shared<Framebuffer>(8, 8, static_cast<Color*>(nullptr));
    source->set_surface_handle(source_handle);
    BlurParams params{.radius = 2.0f};
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));
    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 8, 8}};

    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto output_handle = result.value()->surface_handle();
    REQUIRE(output_handle != runtime::kInvalidRenderSurfaceHandle);
    CHECK(output_handle != source_handle);
    CHECK(surfaces.size() == 2); // borrowed source + blurred output; H/V scratch released

    std::vector<float> output(8 * 8 * 4, 0.0f);
    REQUIRE(backend.download_surface(output_handle, output).ok());
    CHECK(output[center + 0] > 0.0f);
    CHECK(output[center + 3] > 0.0f);

    REQUIRE(backend.release_surface(output_handle).ok());
    CHECK(surfaces.release(output_handle));
    REQUIRE(backend.release_surface(source_handle).ok());
    CHECK(surfaces.release(source_handle));
}

TEST_CASE("EffectStackNode dispatches the simple glow contract through Vulkan surfaces") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    backends::vulkan::VulkanBackend backend;
    runtime::RenderSurfaceRegistry surfaces;
    RenderGraphContext ctx;
    ctx.frame_input.width = 8;
    ctx.frame_input.height = 8;
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

    auto source = std::make_shared<Framebuffer>(8, 8);
    source->clear(Color::transparent());
    source->set_pixel(4, 4, Color{1.0f, 0.5f, 0.25f, 1.0f});

    GlowParams params;
    params.radius = 2.0f;
    params.intensity = 0.5f;
    EffectStack effects;
    effects.push_back(effects::EffectInstance{params});
    EffectStackNode node(std::move(effects));

    const std::array<FramebufferRef, 1> inputs{source.get()};
    const std::array<std::optional<raster::BBox>, 1> bboxes{
        raster::BBox{0, 0, 8, 8}};
    const auto result = node.execute(ctx, inputs, bboxes);
    REQUIRE(result.ok());
    REQUIRE(result.value() != nullptr);
    const auto handle = result.value()->surface_handle();
    REQUIRE(handle != runtime::kInvalidRenderSurfaceHandle);
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
    ctx.backend = &backend;
    ctx.surface_registry = &surfaces;

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

TEST_CASE("Vulkan backend composite_layer is fail-closed without native surfaces") {
    // P0.1 regression lock: the CPU pixel-blit fallback inside the Vulkan
    // backend was demolished. A GPU backend must never blend pixels on the
    // CPU; callers must materialize native surfaces first (CPU-origin assets
    // are legitimate, silent CPU fallbacks are not).
    chronon3d::backends::vulkan::VulkanBackend backend;
    chronon3d::Framebuffer destination(2, 2);
    chronon3d::Framebuffer source(2, 2);
    destination.clear(chronon3d::Color{0.1f, 0.2f, 0.3f, 0.8f});
    source.clear(chronon3d::Color{0.5f, 0.25f, 0.125f, 0.5f});

    bool threw = false;
    try {
        backend.composite_layer(destination, source, chronon3d::BlendMode::Normal,
                                std::nullopt, chronon3d::CompositeOperator::SourceOver);
    } catch (const std::invalid_argument& e) {
        threw = true;
        const std::string message = e.what();
        CHECK(message.find("missing native surface handle") != std::string::npos);
    }
    CHECK(threw);

    // Source-over premultiplied semantics remain covered by the native-surface
    // composite test above and by SoftwareCompositor parity tests.
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

TEST_CASE("Vulkan weighted source-over scales premultiplied RGBA without readback") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    const runtime::SurfaceDesc desc{
        2, 1, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> destination{
        0.10f, 0.05f, 0.0f, 0.25f,
        0.0f, 0.10f, 0.05f, 0.20f};
    const std::vector<float> source{
        0.40f, 0.20f, 0.10f, 0.50f,
        0.30f, 0.10f, 0.20f, 0.40f};
    std::vector<float> actual(8, 0.0f);
    REQUIRE(backend.create_surface(201, desc).ok());
    REQUIRE(backend.create_surface(202, desc).ok());
    REQUIRE(backend.upload_surface(201, desc, destination).ok());
    REQUIRE(backend.upload_surface(202, desc, source).ok());
    REQUIRE(backend.composite_surface_weighted(201, 202, 0.25f).ok());
    REQUIRE(backend.download_surface(201, actual).ok());
    for (std::size_t i = 0; i < actual.size(); i += 4) {
        const float weighted_alpha = source[i + 3] * 0.25f;
        const float remain = 1.0f - weighted_alpha;
        for (std::size_t channel = 0; channel < 3; ++channel)
            CHECK(actual[i + channel] == doctest::Approx(
                source[i + channel] * 0.25f + destination[i + channel] * remain));
        CHECK(actual[i + 3] == doctest::Approx(
            weighted_alpha + destination[i + 3] * remain));
    }
    const auto invalid = backend.composite_surface_weighted(201, 202, 1.25f);
    CHECK_FALSE(invalid.ok());
    CHECK(invalid.error().code == graph::RenderBackendErrorCode::InvalidInput);
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

TEST_CASE("Vulkan Multiply, Screen, and Overlay composite match premultiplied software equations") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> destination{0.4f, 0.6f, 0.2f, 1.0f};
    const std::vector<float> source{0.8f, 0.3f, 0.4f, 1.0f};
    const std::array<std::pair<chronon3d::BlendMode, std::array<float, 3>>, 3> cases{{
        {chronon3d::BlendMode::Multiply, {0.32f, 0.18f, 0.08f}},
        {chronon3d::BlendMode::Screen, {0.88f, 0.72f, 0.52f}},
        {chronon3d::BlendMode::Overlay, {0.64f, 0.44f, 0.16f}},
    }};
    REQUIRE(backend.create_surface(901, desc).ok());
    REQUIRE(backend.create_surface(902, desc).ok());
    for (const auto& [mode, expected] : cases) {
        REQUIRE(backend.upload_surface(901, desc, destination).ok());
        REQUIRE(backend.upload_surface(902, desc, source).ok());
        REQUIRE(backend.composite_surfaces(
            901, 902, mode, chronon3d::CompositeOperator::SourceOver).ok());
        std::vector<float> output(4);
        REQUIRE(backend.download_surface(901, output).ok());
        CHECK(output[0] == doctest::Approx(expected[0]));
        CHECK(output[1] == doctest::Approx(expected[1]));
        CHECK(output[2] == doctest::Approx(expected[2]));
        CHECK(output[3] == doctest::Approx(1.0f));
    }
    REQUIRE(backend.release_surface(901).ok());
    REQUIRE(backend.release_surface(902).ok());
}

TEST_CASE("Vulkan bloom bright-pass preserves source and adds only thresholded light") {
    chronon3d::backends::vulkan::VulkanBackend backend;
    const chronon3d::runtime::SurfaceDesc desc{
        1, 1, chronon3d::runtime::PixelFormat::Rgba32Float,
        chronon3d::runtime::ResourceUsage::Storage,
        chronon3d::runtime::LifetimeClass::JobPersistent, 0};
    const std::vector<float> source{1.0f, 0.4f, 0.2f, 1.0f};
    const std::vector<float> empty(4, 0.0f);
    constexpr float threshold = 0.5f;
    constexpr float intensity = 1.0f;
    constexpr float luminance = 0.2126f + 0.4f * 0.7152f + 0.2f * 0.0722f;
    constexpr float contribution = (luminance - threshold) / (1.0f - threshold);
    const std::array<float, 3> expected{
        1.0f + contribution,
        0.4f + 0.4f * contribution,
        0.2f + 0.2f * contribution};
    for (const auto handle : {911U, 912U, 913U, 914U})
        REQUIRE(backend.create_surface(handle, desc).ok());
    REQUIRE(backend.upload_surface(911, desc, source).ok());
    REQUIRE(backend.upload_surface(912, desc, source).ok());
    REQUIRE(backend.upload_surface(913, desc, empty).ok());
    REQUIRE(backend.upload_surface(914, desc, empty).ok());
    REQUIRE(backend.bloom_surfaces(911, 912, 913, 914,
                                    threshold, 0.0f, intensity).ok());
    std::vector<float> output(4);
    REQUIRE(backend.download_surface(911, output).ok());
    CHECK(output[0] == doctest::Approx(expected[0]).epsilon(1e-5));
    CHECK(output[1] == doctest::Approx(expected[1]).epsilon(1e-5));
    CHECK(output[2] == doctest::Approx(expected[2]).epsilon(1e-5));
    CHECK(output[3] == doctest::Approx(1.0f));
    for (const auto handle : {911U, 912U, 913U, 914U})
        REQUIRE(backend.release_surface(handle).ok());
}

TEST_CASE("Vulkan bloom threshold boundary and HDR match the scalar reference") {
    using namespace chronon3d;
    backends::vulkan::VulkanBackend backend;
    constexpr std::uint32_t width = 5;
    const runtime::SurfaceDesc desc{
        width, 1, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    const std::array<float, width> values{0.699f, 0.700f, 0.701f, 1.0f, 4.0f};
    std::vector<float> source;
    source.reserve(width * 4);
    for (const float value : values)
        source.insert(source.end(), {value, value, value, 1.0f});
    const std::vector<float> empty(source.size(), 0.0f);
    for (const auto handle : {921U, 922U, 923U, 924U})
        REQUIRE(backend.create_surface(handle, desc).ok());
    REQUIRE(backend.upload_surface(921, desc, source).ok());
    REQUIRE(backend.upload_surface(922, desc, source).ok());
    REQUIRE(backend.upload_surface(923, desc, empty).ok());
    REQUIRE(backend.upload_surface(924, desc, empty).ok());
    REQUIRE(backend.bloom_surfaces(921, 922, 923, 924, 0.7f, 0.0f, 0.5f).ok());
    std::vector<float> actual(source.size(), 0.0f);
    REQUIRE(backend.download_surface(921, actual).ok());
    Framebuffer cpu{static_cast<int>(width), 1};
    for (std::size_t i = 0; i < values.size(); ++i)
        cpu.pixels_row(0)[i] = Color{values[i], values[i], values[i], 1.0f};
    renderer::run_bloom_effect(cpu, BloomParams{
        .threshold = 0.7f, .radius = 0.0f, .intensity = 0.5f}, std::nullopt);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float contribution = std::clamp((values[i] - 0.7f) / 0.3f, 0.0f, 1.0f);
        const float expected = values[i] + values[i] * contribution * 0.5f;
        CHECK(actual[i * 4] == doctest::Approx(expected).epsilon(1e-5));
        CHECK(actual[i * 4 + 1] == doctest::Approx(expected).epsilon(1e-5));
        CHECK(actual[i * 4 + 2] == doctest::Approx(expected).epsilon(1e-5));
        CHECK(actual[i * 4 + 3] == doctest::Approx(1.0f));
        CHECK(actual[i * 4] == doctest::Approx(cpu.pixels_row(0)[i].r).epsilon(1e-5));
        CHECK(actual[i * 4 + 1] == doctest::Approx(cpu.pixels_row(0)[i].g).epsilon(1e-5));
        CHECK(actual[i * 4 + 2] == doctest::Approx(cpu.pixels_row(0)[i].b).epsilon(1e-5));
        CHECK(actual[i * 4 + 3] == doctest::Approx(cpu.pixels_row(0)[i].a).epsilon(1e-5));
    }
    for (const auto handle : {921U, 922U, 923U, 924U})
        REQUIRE(backend.release_surface(handle).ok());
}

TEST_CASE("Vulkan RGB channel transform matches software for offsets, affine channels, and alpha") {
    using namespace chronon3d;
    constexpr std::uint32_t width = 9;
    constexpr std::uint32_t height = 5;
    const runtime::SurfaceDesc desc{
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent, 0};
    Framebuffer original{static_cast<int>(width), static_cast<int>(height)};
    std::vector<float> packed;
    packed.reserve(width * height * 4);
    for (int y = 0; y < static_cast<int>(height); ++y)
        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float alpha = static_cast<float>((x + 2 * y) % 5 + 1) / 5.0f;
            const Color color{alpha * static_cast<float>(x + 1) / width,
                              alpha * static_cast<float>(y + 1) / height,
                              alpha * static_cast<float>((x + y) % 7) / 7.0f, alpha};
            original.pixels_row(y)[x] = color;
            packed.insert(packed.end(), {color.r, color.g, color.b, color.a});
        }

    backends::vulkan::VulkanBackend backend;
    REQUIRE(backend.create_surface(931, desc).ok());
    REQUIRE(backend.create_surface(932, desc).ok());
    const std::array<ChromaticAberrationParams, 3> cases{{
        ChromaticAberrationParams{
            .red_offset = 2.0f, .blue_offset = -1.0f,
            .red = RgbChannelTransform{Vec2{1.0f, 0.0f}, Vec2{1.0f, 1.0f}, -5.0f, 1.0f},
            .green = RgbChannelTransform{},
            .blue = RgbChannelTransform{Vec2{-1.0f, 0.0f}, Vec2{1.0f, 1.0f}, 5.0f, 1.0f}},
        ChromaticAberrationParams{
            .red_offset = 1.5f, .blue_offset = -2.0f,
            .offset_mode = ChromaticOffsetMode::Radial, .center = Vec2{0.35f, 0.6f},
            .red = RgbChannelTransform{Vec2{0.25f, -0.5f}, Vec2{1.0f, 1.0f}, 0.0f, 0.75f},
            .green = RgbChannelTransform{},
            .blue = RgbChannelTransform{Vec2{-0.5f, 0.0f}, Vec2{1.0f, 1.0f}, 0.0f, 0.5f}},
        ChromaticAberrationParams{
            .red_offset = -2.0f, .blue_offset = 2.0f,
            .offset_mode = ChromaticOffsetMode::Velocity, .velocity = Vec2{3.0f, 4.0f},
            .red = RgbChannelTransform{}, .green = RgbChannelTransform{},
            .blue = RgbChannelTransform{Vec2{0.0f, 0.0f}, Vec2{1.4f, 0.8f}, 12.0f, 0.6f}}
    }};
    renderer::SoftwareRegistry registry;
    renderer::register_builtin_processors(registry);
    for (const auto& params : cases) {
        REQUIRE(backend.upload_surface(932, desc, packed).ok());
        REQUIRE(backend.chromatic_transform_surface(931, 932, params).ok());
        std::vector<float> actual(packed.size(), 0.0f);
        REQUIRE(backend.download_surface(931, actual).ok());

        Framebuffer expected{original};
        EffectStack stack;
        stack.emplace_back(params);
        effects::EffectExecutionContext context;
        renderer::apply_effect_stack(expected, stack, context, registry);
        for (std::size_t index = 0; index < actual.size() / 4; ++index) {
            const int x = static_cast<int>(index % width);
            const int y = static_cast<int>(index / width);
            const Color reference = expected.pixels_row(y)[x];
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const float value = channel == 0 ? reference.r : channel == 1 ? reference.g :
                                    channel == 2 ? reference.b : reference.a;
                CHECK(actual[index * 4 + channel] == doctest::Approx(value).epsilon(1e-5));
            }
        }
    }
    REQUIRE(backend.release_surface(931).ok());
    REQUIRE(backend.release_surface(932).ok());
}
#endif
