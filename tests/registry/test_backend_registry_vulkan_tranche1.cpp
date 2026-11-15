#include "test_backend_registry_vulkan_effect_parity_test_support.hpp"

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/nodes/lighting_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/particles.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN

namespace {

std::vector<float> download_and_release(
    chronon3d::backends::vulkan::VulkanBackend& backend,
    chronon3d::runtime::RenderSurfaceRegistry& surfaces,
    chronon3d::runtime::RenderSurfaceHandle handle,
    int width,
    int height) {
    REQUIRE(handle != chronon3d::runtime::kInvalidRenderSurfaceHandle);
    std::vector<float> pixels(static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) * 4U,
                              0.0f);
    REQUIRE(backend.download_surface(handle, pixels).ok());
    REQUIRE(backend.release_surface(handle).ok());
    CHECK(surfaces.release(handle));
    return pixels;
}

void check_pixels_close(const std::vector<float>& actual,
                        const std::vector<float>& expected,
                        float tolerance,
                        const char* label) {
    REQUIRE(actual.size() == expected.size());
    float max_diff = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const float diff = std::abs(actual[i] - expected[i]);
        max_diff = std::max(max_diff, diff);
        if (diff > tolerance) {
            INFO(label << " idx=" << i << " expected=" << expected[i]
                       << " actual=" << actual[i]);
            CHECK(diff <= tolerance);
        }
    }
    MESSAGE(label << " max_diff=" << max_diff);
}

} // namespace

TEST_CASE("Strict Vulkan TrackMatteNode matches software for Alpha, AlphaInverted, Luma, and LumaInverted") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    auto& backend = *shared_vulkan_test_backend();
    constexpr int w = 4;
    constexpr int h = 4;

    auto target = std::make_shared<Framebuffer>(w, h);
    auto matte = std::make_shared<Framebuffer>(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float ta = (x == 0 && y == 0) ? 0.0f : (0.25f + 0.25f * static_cast<float>(y));
            const float ma = (x == 3 && y == 3) ? 0.0f : (0.2f * static_cast<float>(x + 1));
            target->set_pixel(x, y, Color{0.9f * ta, 0.4f * ta, 0.2f * ta, ta});
            matte->set_pixel(x, y, Color{
                (0.1f + 0.2f * static_cast<float>(x)) * ma,
                (0.2f + 0.15f * static_cast<float>(y)) * ma,
                (0.3f + 0.1f * static_cast<float>(x + y)) * ma,
                ma});
        }
    }

    const std::array<TrackMatteType, 4> modes{{
        TrackMatteType::Alpha,
        TrackMatteType::AlphaInverted,
        TrackMatteType::Luma,
        TrackMatteType::LumaInverted,
    }};

    for (const auto mode : modes) {
        CAPTURE(static_cast<int>(mode));
        TrackMatteNode cpu_node(mode, "cpu-matte", {});
        RenderGraphContext cpu_ctx;
        cpu_ctx.frame_input.width = w;
        cpu_ctx.frame_input.height = h;
        const std::array<FramebufferRef, 2> inputs{target.get(), matte.get()};
        const std::array<std::optional<raster::BBox>, 2> bboxes{
            raster::BBox{0, 0, w, h}, raster::BBox{0, 0, w, h}};
        const auto cpu_res = cpu_node.execute(cpu_ctx, inputs, bboxes);
        REQUIRE(cpu_res.ok());
        std::vector<float> expected;
        expected.reserve(static_cast<std::size_t>(w * h * 4));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Color c = cpu_res.value()->get_pixel(x, y);
                expected.insert(expected.end(), {c.r, c.g, c.b, c.a});
            }
        }

        runtime::RenderSurfaceRegistry surfaces;
        RenderGraphContext gpu_ctx;
        gpu_ctx.frame_input.width = w;
        gpu_ctx.frame_input.height = h;
        gpu_ctx.backend = &backend;
        gpu_ctx.surface_registry = &surfaces;
        gpu_ctx.policy.require_native_gpu = true;

        TrackMatteNode gpu_node(mode, "gpu-matte", {});
        const auto gpu_res = gpu_node.execute(gpu_ctx, inputs, bboxes);
        INFO("strict Vulkan error: " << (gpu_res.ok() ? "" : gpu_res.error().message));
        REQUIRE(gpu_res.ok());
        const auto actual = download_and_release(
            backend, surfaces, gpu_res.value()->surface_handle(), w, h);
        check_pixels_close(actual, expected, 1.0e-4f, "TrackMatte parity");
    }
}

TEST_CASE("Strict Vulkan LightingNode matches software for SpotLight and AeInverseSquareClamped") {
    using namespace chronon3d;
    using namespace chronon3d::graph;

    auto& backend = *shared_vulkan_test_backend();
    constexpr int w = 16;
    constexpr int h = 16;

    auto make_lit_fb = [&]() {
        auto fb = std::make_shared<Framebuffer>(w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float a = (x == 0 && y == 0) ? 0.0f : 1.0f;
                fb->set_pixel(x, y, Color{0.6f * a, 0.5f * a, 0.4f * a, a});
            }
        }
        return fb;
    };

    const auto run_lighting_parity = [&](const rendering::LightContext& lc, const char* label) {
        auto cpu_src = make_lit_fb();
        RenderGraphContext cpu_ctx;
        cpu_ctx.frame_input.width = w;
        cpu_ctx.frame_input.height = h;
        cpu_ctx.frame_input.light_context = lc;

        Material2_5D mat;
        mat.accepts_lights = true;
        mat.diffuse = 1.0f;
        mat.ambient_multiplier = 1.0f;
        LightingNode cpu_node("lit-layer", Mat4(1.0f), mat);
        const std::array<FramebufferRef, 1> cpu_inputs{cpu_src.get()};
        const std::array<std::optional<raster::BBox>, 1> bboxes{raster::BBox{0, 0, w, h}};
        const auto cpu_res = cpu_node.execute(cpu_ctx, cpu_inputs, bboxes);
        REQUIRE(cpu_res.ok());
        std::vector<float> expected;
        expected.reserve(static_cast<std::size_t>(w * h * 4));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Color c = cpu_res.value()->get_pixel(x, y);
                expected.insert(expected.end(), {c.r, c.g, c.b, c.a});
            }
        }

        runtime::RenderSurfaceRegistry surfaces;
        auto gpu_src = make_lit_fb();
        const auto desc = runtime::SurfaceDesc::make(
            w, h, runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::FrameTransient, runtime::ColorMetadata{});
        const auto src_handle = surfaces.create(desc);
        REQUIRE(src_handle != runtime::kInvalidRenderSurfaceHandle);
        REQUIRE(backend.create_surface(src_handle, desc).ok());
        std::vector<float> src_rgba(static_cast<std::size_t>(w * h * 4), 0.0f);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Color c = gpu_src->get_pixel(x, y);
                const std::size_t idx = static_cast<std::size_t>(y * w + x) * 4U;
                src_rgba[idx + 0] = c.r;
                src_rgba[idx + 1] = c.g;
                src_rgba[idx + 2] = c.b;
                src_rgba[idx + 3] = c.a;
            }
        }
        REQUIRE(backend.upload_surface(src_handle, desc, src_rgba).ok());
        gpu_src->set_surface_handle(src_handle);

        RenderGraphContext gpu_ctx;
        gpu_ctx.frame_input.width = w;
        gpu_ctx.frame_input.height = h;
        gpu_ctx.frame_input.light_context = lc;
        gpu_ctx.backend = &backend;
        gpu_ctx.surface_registry = &surfaces;
        gpu_ctx.policy.require_native_gpu = true;

        LightingNode gpu_node("lit-layer", Mat4(1.0f), mat);
        const std::array<FramebufferRef, 1> gpu_inputs{gpu_src.get()};
        const auto gpu_res = gpu_node.execute(gpu_ctx, gpu_inputs, bboxes);
        INFO("strict Vulkan error: " << (gpu_res.ok() ? "" : gpu_res.error().message));
        REQUIRE(gpu_res.ok());
        const auto actual = download_and_release(
            backend, surfaces, gpu_res.value()->surface_handle(), w, h);
        REQUIRE(backend.release_surface(src_handle).ok());
        CHECK(surfaces.release(src_handle));
        check_pixels_close(actual, expected, 2.0e-4f, label);
    };

    SUBCASE("SpotLight cone and penumbra") {
        rendering::LightContext lc;
        lc.enabled = true;
        lc.ambient_enabled = true;
        lc.ambient = 0.1f;
        rendering::PointLightSource spot;
        spot.kind = rendering::PointLightKind::Spot;
        spot.position = Vec3{8.0f, 8.0f, 10.0f};
        spot.direction = glm::normalize(Vec3{0.25f, -0.15f, -1.0f});
        spot.color = Color{1.0f, 0.85f, 0.65f, 1.0f};
        spot.radius = 12.0f;
        spot.range = 0.0f;
        spot.decay = 1.5f;
        spot.angle = 0.65f;
        spot.penumbra = 0.45f;
        lc.point_lights.push_back(spot);
        run_lighting_parity(lc, "SpotLight parity");
    }

    SUBCASE("PointLightFalloff::AeInverseSquareClamped") {
        rendering::LightContext lc;
        lc.enabled = true;
        lc.ambient_enabled = true;
        lc.ambient = 0.05f;
        rendering::PointLightSource pt;
        pt.kind = rendering::PointLightKind::Point;
        pt.position = Vec3{7.5f, 7.5f, 3.0f};
        pt.color = Color{0.9f, 0.95f, 1.0f, 1.0f};
        pt.radius = 5.0f;
        pt.range = 11.0f;
        pt.falloff = rendering::PointLightFalloff::AeInverseSquareClamped;
        lc.point_lights.push_back(pt);
        run_lighting_parity(lc, "AeInverseSquareClamped parity");
    }
}

TEST_CASE("Strict Vulkan GPU particle emitter matches CPU sampler for Ellipse, Edge, and Sprite rotation/scale") {
    using namespace chronon3d;
    using namespace chronon3d::runtime;

    auto& backend = *shared_vulkan_test_backend();
    const auto canvas = SurfaceDesc::make(
        64, 64, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, ColorMetadata{});

    const auto run_shape_parity = [&](EmitterShape shape, const char* label) {
        RenderSurfaceRegistry surfaces;
        const auto gpu_dst = surfaces.create(canvas);
        const auto cpu_dst = surfaces.create(canvas);
        REQUIRE(backend.create_surface(gpu_dst, canvas).ok());
        REQUIRE(backend.create_surface(cpu_dst, canvas).ok());
        REQUIRE(backend.initialize_transparent_surface(gpu_dst).ok());
        REQUIRE(backend.initialize_transparent_surface(cpu_dst).ok());

        ParticleEmitterParams emitter;
        emitter.count = 32;
        emitter.seed = 20261115;
        emitter.shape = shape;
        emitter.emitter_size = {28.0f, 18.0f};
        emitter.velocity_min = {-4.0f, -4.0f};
        emitter.velocity_max = {4.0f, 4.0f};
        emitter.lifetime_min = 2.0f;
        emitter.lifetime_max = 5.0f;
        emitter.size_min = 3.0f;
        emitter.size_max = 6.0f;
        emitter.blend_mode = BlendMode::Normal;

        const auto sampled = sample_particles(emitter, 1.25);
        REQUIRE(sampled.ok());
        REQUIRE(!sampled.particles.empty());

        std::vector<LayerInstance> instances;
        instances.reserve(sampled.particles.size());
        for (const auto& p : sampled.particles) {
            LayerInstance inst;
            inst.kind = PrimitiveKind::Particle;
            inst.dst_x0 = p.position.x - p.size * 0.5f;
            inst.dst_y0 = p.position.y - p.size * 0.5f;
            inst.dst_x1 = p.position.x + p.size * 0.5f;
            inst.dst_y1 = p.position.y + p.size * 0.5f;
            inst.opacity = 1.0f;
            const float a = std::clamp(p.color.a, 0.0f, 1.0f);
            inst.tint = Color{p.color.r * a, p.color.g * a, p.color.b * a, a};
            inst.blend = BlendMode::Normal;
            Vec2 trail = p.velocity * kParticleTrailSeconds;
            const float trail_limit = std::max(
                p.size * kParticleTrailMaxSizeFactor, kParticleTrailMinPixels);
            const float trail_len = std::sqrt(trail.x * trail.x + trail.y * trail.y);
            if (trail_len > trail_limit && trail_len > 0.0f) {
                trail *= trail_limit / trail_len;
            }
            inst.motion_x = trail.x;
            inst.motion_y = trail.y;
            instances.push_back(inst);
        }

        const auto transparent_src = surfaces.create(
            SurfaceDesc::make(1, 1, PixelFormat::Rgba32Float, ResourceUsage::Storage,
                              LifetimeClass::FrameTransient, ColorMetadata{}));
        REQUIRE(backend.create_surface(
            transparent_src,
            SurfaceDesc::make(1, 1, PixelFormat::Rgba32Float, ResourceUsage::Storage,
                              LifetimeClass::FrameTransient, ColorMetadata{})).ok());
        REQUIRE(backend.initialize_transparent_surface(transparent_src).ok());

        const std::array<RenderSurfaceHandle, 1> resources{transparent_src};
        backend.begin_frame_batch();
        REQUIRE(backend.execute_layer_batch(cpu_dst, instances, resources, {}, {}).ok());
        backend.end_frame_batch();

        backend.begin_frame_batch();
        REQUIRE(backend.execute_gpu_particle_batch(
            gpu_dst, transparent_src, emitter, 1.25).ok());
        backend.end_frame_batch();

        const auto expected = download_and_release(backend, surfaces, cpu_dst, 64, 64);
        const auto actual = download_and_release(backend, surfaces, gpu_dst, 64, 64);
        REQUIRE(backend.release_surface(transparent_src).ok());
        CHECK(surfaces.release(transparent_src));
        check_pixels_close(actual, expected, 2.0e-4f, label);
    };

    SUBCASE("EmitterShape::Ellipse") {
        run_shape_parity(EmitterShape::Ellipse, "Particle Ellipse parity");
    }

    SUBCASE("EmitterShape::Edge") {
        run_shape_parity(EmitterShape::Edge, "Particle Edge parity");
    }

    SUBCASE("Sprite with rotation, angular velocity, and non-uniform scale") {
        RenderSurfaceRegistry surfaces;
        const auto gpu_dst = surfaces.create(canvas);
        const auto cpu_dst = surfaces.create(canvas);
        const auto sprite_desc = SurfaceDesc::make(
            2, 2, PixelFormat::Rgba32Float, ResourceUsage::Storage,
            LifetimeClass::FrameTransient, ColorMetadata{});
        const auto sprite_src = surfaces.create(sprite_desc);
        REQUIRE(backend.create_surface(gpu_dst, canvas).ok());
        REQUIRE(backend.create_surface(cpu_dst, canvas).ok());
        REQUIRE(backend.create_surface(sprite_src, sprite_desc).ok());
        REQUIRE(backend.initialize_transparent_surface(gpu_dst).ok());
        REQUIRE(backend.initialize_transparent_surface(cpu_dst).ok());
        const std::array<float, 16> sprite_pixels{
            1.0f, 0.1f, 0.1f, 1.0f,
            0.1f, 1.0f, 0.1f, 1.0f,
            0.1f, 0.1f, 1.0f, 1.0f,
            1.0f, 1.0f, 0.2f, 0.75f};
        REQUIRE(backend.upload_surface(sprite_src, sprite_desc, sprite_pixels).ok());

        ParticleEmitterParams emitter;
        emitter.count = 8;
        emitter.seed = 9001;
        emitter.emitter_size = {24.0f, 24.0f};
        emitter.lifetime_min = 6.0f;
        emitter.lifetime_max = 6.0f;
        emitter.size_min = 10.0f;
        emitter.size_max = 12.0f;
        emitter.shape = EmitterShape::Box;
        emitter.blend_mode = BlendMode::Normal;
        emitter.sprite_asset = "quad_sprite";
        emitter.sprite_scale_min = 0.75f;
        emitter.sprite_scale_max = 1.35f;
        emitter.rotation_min = -1.2f;
        emitter.rotation_max = 1.2f;
        emitter.angular_velocity_min = -0.8f;
        emitter.angular_velocity_max = 0.8f;

        const auto sampled = sample_particles(emitter, 1.5);
        REQUIRE(sampled.ok());
        REQUIRE(sampled.particles.size() == 8);

        std::vector<LayerInstance> instances;
        std::vector<float> transforms(16, 0.0f);
        instances.reserve(sampled.particles.size());
        transforms.reserve((sampled.particles.size() + 1U) * 16U);
        for (std::size_t idx = 0; idx < sampled.particles.size(); ++idx) {
            const auto& p = sampled.particles[idx];
            const float sprite_size = p.size * p.sprite_scale;
            const float half = sprite_size * 0.5f;
            const float c = std::cos(p.rotation);
            const float s = std::sin(p.rotation);
            // Local [0, sprite_size]^2 -> canvas rotated around p.position.
            const float tx = p.position.x - c * half + s * half;
            const float ty = p.position.y - s * half - c * half;
            const auto corner = [&](float lx, float ly) {
                return Vec2{c * lx - s * ly + tx, s * lx + c * ly + ty};
            };
            const std::array<Vec2, 4> corners{
                corner(0.0f, 0.0f), corner(sprite_size, 0.0f),
                corner(0.0f, sprite_size), corner(sprite_size, sprite_size)};
            LayerInstance inst;
            inst.kind = PrimitiveKind::ParticleSprite;
            inst.resource_index = 0;
            inst.transform_index = static_cast<std::int32_t>(idx + 1U);
            inst.dst_x0 = inst.dst_x1 = corners[0].x;
            inst.dst_y0 = inst.dst_y1 = corners[0].y;
            for (const auto& pt : corners) {
                inst.dst_x0 = std::min(inst.dst_x0, pt.x);
                inst.dst_y0 = std::min(inst.dst_y0, pt.y);
                inst.dst_x1 = std::max(inst.dst_x1, pt.x);
                inst.dst_y1 = std::max(inst.dst_y1, pt.y);
            }
            // Inverse affine: canvas -> 2x2 sprite texel space (column-major 4x4).
            const float inv_scale = 2.0f / sprite_size;
            const float inv00 = c * inv_scale;
            const float inv01 = s * inv_scale;
            const float inv10 = -s * inv_scale;
            const float inv11 = c * inv_scale;
            const float inv_tx = -(inv00 * tx + inv01 * ty);
            const float inv_ty = -(inv10 * tx + inv11 * ty);
            std::array<float, 16> m{};
            m[0] = inv00;
            m[4] = inv01;
            m[12] = inv_tx;
            m[1] = inv10;
            m[5] = inv11;
            m[13] = inv_ty;
            transforms.insert(transforms.end(), m.begin(), m.end());
            inst.opacity = p.color.a;
            inst.tint = Color{p.color.r, p.color.g, p.color.b, 1.0f};
            inst.blend = BlendMode::Normal;
            instances.push_back(inst);
        }
        const std::array<RenderSurfaceHandle, 1> resources{sprite_src};

        backend.begin_frame_batch();
        REQUIRE(backend.execute_layer_batch(cpu_dst, instances, resources, transforms, {}).ok());
        backend.end_frame_batch();

        backend.begin_frame_batch();
        REQUIRE(backend.execute_gpu_particle_batch(
            gpu_dst, sprite_src, emitter, 1.5).ok());
        backend.end_frame_batch();

        const auto expected = download_and_release(backend, surfaces, cpu_dst, 64, 64);
        const auto actual = download_and_release(backend, surfaces, gpu_dst, 64, 64);
        REQUIRE(backend.release_surface(sprite_src).ok());
        CHECK(surfaces.release(sprite_src));
        check_pixels_close(actual, expected, 2.0e-4f, "Particle Sprite rotation/scale parity");
    }
}

#endif // CHRONON3D_ENABLE_VULKAN
