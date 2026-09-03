// ==============================================================================
// tests/render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp
//
// PR2 — PerPixelDofNode render-graph integration tests.
//
// Drives the full SoftwareRenderer pipeline and verifies pixel-level and
// depth-provenance outcomes. RenderSettings does NOT expose a `dof` field;
// DoF state lives on the Camera2_5DRuntime supplied to PerPixelDofNode.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>
#include <tests/helpers/test_utils.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
using namespace chronon3d;
namespace ctt = chronon3d::test;

Composition make_dof_scene(bool far_bar) {
    return composition({.width = 256, .height = 256, .duration = 1},
        [far_bar](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                .enabled = true,
                .focus_z = 0.0f,
                .aperture = 0.05f,
                .max_blur = 24.0f
            });
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.05f, 0.07f, 0.10f, 1.0f});
            });
            s.layer("lines", [far_bar](LayerBuilder& l) {
                l.enable_3d(true).position({0.0f, 0.0f, far_bar ? -800.0f : 0.0f});
                for (int i = 0; i < 8; ++i) {
                    l.rect("r" + std::to_string(i), {
                        .size = {200.0f, 1.5f},
                        .color = {1.0f, 1.0f, 1.0f, 1.0f},
                        .pos = {0.0f, -50.0f + static_cast<float>(i) * 14.0f, 0.0f}
                    });
                }
            });
            return s.build();
        });
}

Composition make_sparse_dof_scene() {
    return composition({.width = 128, .height = 128, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                .enabled = true,
                .focus_z = 0.0f,
                .aperture = 0.05f,
                .max_blur = 24.0f
            });
            s.layer("sparse", [](LayerBuilder& l) {
                l.enable_3d(true).position({0.0f, 0.0f, -800.0f});
                l.rect("box", {
                    .size = {8.0f, 8.0f},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition make_focused_dof_scene() {
    return composition({.width = 128, .height = 128, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                    .enabled = true,
                    .focus_z = 0.0f,
                    .aperture = 0.05f,
                    .max_blur = 24.0f
                });
            s.layer("focused", [](LayerBuilder& l) {
                l.enable_3d(true).position({0.0f, 0.0f, 0.0f});
                l.rect("box", {
                    .size = {16.0f, 16.0f},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
}

Composition make_two_source_dof_scene(bool distant) {
    return composition({.width = 128, .height = 128, .duration = 1},
        [distant](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                    .enabled = true, .focus_z = 0.0f,
                    .aperture = 0.05f, .max_blur = 24.0f});
            s.layer("source_a", [distant](LayerBuilder& l) {
                l.enable_3d(true).position(distant
                    ? Vec3{-20.0f, -20.0f, -600.0f}
                    : Vec3{-12.0f, 0.0f, -600.0f});
                l.rect("a", {.size = {12.0f, 12.0f},
                              .color = Color::red(), .pos = {0.0f, 0.0f, 0.0f}});
            });
            s.layer("source_b", [distant](LayerBuilder& l) {
                l.enable_3d(true).position(distant
                    ? Vec3{20.0f, 20.0f, -600.0f}
                    : Vec3{12.0f, 0.0f, 0.0f});
                l.rect("b", {.size = {12.0f, 12.0f},
                              .color = Color::blue(), .pos = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
}

Composition make_alpha_dof_scene(float opacity) {
    return composition({.width = 64, .height = 64, .duration = 1},
        [opacity](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f})
                .zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                    .enabled = true, .focus_z = 0.0f,
                    .aperture = 0.05f, .max_blur = 16.0f});
            s.layer("alpha_source", [opacity](LayerBuilder& l) {
                l.enable_3d(true).position({0.0f, 0.0f, -800.0f}).opacity(opacity);
                l.rect("box", {.size = {12.0f, 12.0f},
                                .color = Color::white(), .pos = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
}

Composition make_position_animation_dof_scene() {
    return composition({.width = 128, .height = 128, .duration = 300},
        [](const FrameContext& ctx) {
            const float frame = static_cast<float>(ctx.frame());
            const float x = -10.0f + 20.0f * (frame / 299.0f);
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                    .enabled = true, .focus_z = 0.0f,
                    .aperture = 0.05f, .max_blur = 24.0f});
            s.layer("moving", [x](LayerBuilder& l) {
                l.enable_3d(true).position({x, 0.0f, -800.0f});
                l.rect("box", {.size = {12.0f, 12.0f},
                                .color = Color::white(), .pos = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
}

Composition make_z_animation_dof_scene() {
    return composition({.width = 64, .height = 64, .duration = 300},
        [](const FrameContext& ctx) {
            const float frame = static_cast<float>(ctx.frame());
            const float z = -800.0f * (frame / 299.0f);
            SceneBuilder s(ctx);
            s.camera().enable(true)
                .position({0.0f, 0.0f, -1000.0f}).zoom(1000.0f)
                .look_at({0.0f, 0.0f, 0.0f})
                .dof(DepthOfFieldSettings{
                    .enabled = true, .focus_z = 0.0f,
                    .aperture = 0.05f, .max_blur = 24.0f});
            s.layer("z_moving", [z](LayerBuilder& l) {
                l.enable_3d(true).position({0.0f, 0.0f, z});
                l.rect("box", {.size = {8.0f, 8.0f},
                                .color = Color::white(), .pos = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
}

TEST_CASE("PR2-RG-DoF: smoke render produces expected dimensions") {
    auto r = ctt::make_renderer();
    auto fb = r.render(make_dof_scene(false), 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 256);
    CHECK(fb->height() == 256);
}

TEST_CASE("PR2-RG-DoF: two consecutive renders are byte-equal (determinism)") {
    auto r = ctt::make_renderer();
    auto fb1 = r.render(make_dof_scene(false), 0);
    auto fb2 = r.render(make_dof_scene(false), 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(ctt::framebuffer_hash(*fb1) == ctt::framebuffer_hash(*fb2));
}

TEST_CASE("PR2-RG-DoF: per-element z-range variation produces differing hashes") {
    auto r = ctt::make_renderer();
    auto fb_near = r.render(make_dof_scene(false), 0);
    auto fb_far  = r.render(make_dof_scene(true),  0);
    REQUIRE(fb_near != nullptr);
    REQUIRE(fb_far  != nullptr);
    CHECK(ctt::framebuffer_hash(*fb_near) != ctt::framebuffer_hash(*fb_far));
}

TEST_CASE("PR2-RG-DoF: untouched pixels are not classified as blur sources") {
    constexpr uint64_t kPixels = 128ULL * 128ULL;

    auto r = ctt::make_renderer();
    r.reset_counters();
    auto fb = r.render(make_sparse_dof_scene(), 0);
    REQUIRE(fb != nullptr);

    const uint64_t blur_sources = r.counters()->dof_blur_source_pixels.load(
        std::memory_order_relaxed);
    const uint64_t roi_pixels = r.counters()->dof_roi_pixels.load(
        std::memory_order_relaxed);
    const uint64_t scratch_bytes = r.counters()->dof_scratch_bytes.load(
        std::memory_order_relaxed);

    // Regression for the depth-provenance bug where OutputPass initialized
    // every untouched pixel to z=0.  Zero is a valid world depth, so the DOF
    // analyzer classified essentially the complete canvas as defocused.  A
    // sparse scene must leave the vast majority of the depth plane unset.
    CHECK(blur_sources > 0);
    CHECK(blur_sources < kPixels / 4);
    CHECK(roi_pixels > 0);
    // The projected 8×8 authoring box expands to a larger screen-space box
    // under this camera; the invariant is that the processing region remains
    // bounded away from a full-frame fallback.
    CHECK(roi_pixels < (kPixels * 3) / 4);
    CHECK(scratch_bytes > 0);
    CHECK(scratch_bytes < kPixels * sizeof(Color) * 2);
}

TEST_CASE("PR2-RG-DoF: fully focused scene skips blur and scratch") {
    auto r = ctt::make_renderer();
    r.reset_counters();
    auto fb = r.render(make_focused_dof_scene(), 0);
    REQUIRE(fb != nullptr);

    const auto* counters = r.counters();
    CHECK(counters->dof_blur_source_pixels.load(std::memory_order_relaxed) == 0);
    CHECK(counters->dof_roi_pixels.load(std::memory_order_relaxed) == 0);
    CHECK(counters->dof_scratch_bytes.load(std::memory_order_relaxed) == 0);
    CHECK(counters->dof_horizontal_pass_wall_us.load(std::memory_order_relaxed) == 0);
    CHECK(counters->dof_vertical_pass_wall_us.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("PR2-RG-DoF: focused and blurred sources share correct ROI") {
    auto r = ctt::make_renderer();
    r.reset_counters();
    auto fb = r.render(make_two_source_dof_scene(false), 0);
    REQUIRE(fb != nullptr);

    CHECK(r.counters()->dof_blur_source_pixels.load(std::memory_order_relaxed) > 0);
    CHECK(r.counters()->dof_roi_pixels.load(std::memory_order_relaxed) > 0);
    CHECK(r.counters()->dof_scratch_bytes.load(std::memory_order_relaxed) > 0);
}

TEST_CASE("PR2-RG-DoF: distant sources keep source count below ROI area") {
    auto r = ctt::make_renderer();
    r.reset_counters();
    auto fb = r.render(make_two_source_dof_scene(true), 0);
    REQUIRE(fb != nullptr);

    const auto sources = r.counters()->dof_blur_source_pixels.load(std::memory_order_relaxed);
    const auto roi = r.counters()->dof_roi_pixels.load(std::memory_order_relaxed);
    CHECK(sources > 0);
    CHECK(roi > sources);
}

TEST_CASE("PR2-RG-DoF: alpha threshold controls depth provenance") {
    for (const float opacity : {0.005f, 0.009f, 0.010f, 0.011f, 0.020f}) {
        auto r = ctt::make_renderer();
        r.reset_counters();
        auto fb = r.render(make_alpha_dof_scene(opacity), 0);
        REQUIRE(fb != nullptr);
        const auto sources = r.counters()->dof_blur_source_pixels.load(
            std::memory_order_relaxed);
        if (opacity <= 0.010f) {
            CHECK(sources == 0);
        } else {
            CHECK(sources > 0);
        }
    }
}

TEST_CASE("PR2-RG-DoF: moving source keeps ROI bounded across 300 frames") {
    auto r = ctt::make_renderer();
    const auto scene = make_position_animation_dof_scene();
    constexpr uint64_t full_pixels = 128ULL * 128ULL;
    uint64_t first_hash = 0;
    uint64_t last_hash = 0;
    for (int frame = 0; frame < 300; ++frame) {
        r.reset_counters();
        auto fb = r.render(scene, frame);
        REQUIRE(fb != nullptr);
        const auto sources = r.counters()->dof_blur_source_pixels.load(
            std::memory_order_relaxed);
        const auto roi = r.counters()->dof_roi_pixels.load(std::memory_order_relaxed);
        CHECK(sources > 0);
        CHECK(roi < full_pixels);
        if (frame == 0) first_hash = ctt::framebuffer_hash(*fb);
        if (frame == 299) last_hash = ctt::framebuffer_hash(*fb);
    }
    CHECK(first_hash != last_hash);
}

TEST_CASE("PR2-RG-DoF: cache invalidates when animated position changes") {
    auto r = ctt::make_renderer();
    r.runtime().node_cache().set_capacity(128 * 1024 * 1024);
    const auto scene = make_position_animation_dof_scene();
    auto first = r.render(scene, 0);
    REQUIRE(first != nullptr);
    const auto first_hash = ctt::framebuffer_hash(*first);
    const auto hits_before = r.counters()->cache_hits.load(std::memory_order_relaxed);

    auto changed = r.render(scene, 150);
    REQUIRE(changed != nullptr);
    CHECK(ctt::framebuffer_hash(*changed) != first_hash);
    // Frame-dependent transforms are intentionally cache-bypassed rather than
    // recorded as cache misses; unrelated static nodes can still hit.
    CHECK(r.counters()->cache_hits.load(std::memory_order_relaxed) > hits_before);
    CHECK(r.counters()->dof_blur_source_pixels.load(std::memory_order_relaxed) > 0);
}

TEST_CASE("PR2-RG-DoF: z animation transitions from focus to blur") {
    auto r = ctt::make_renderer();
    const auto scene = make_z_animation_dof_scene();
    r.reset_counters();
    auto focused = r.render(scene, 0);
    REQUIRE(focused != nullptr);
    const auto focused_sources = r.counters()->dof_blur_source_pixels.load(
        std::memory_order_relaxed);

    r.reset_counters();
    auto defocused = r.render(scene, 299);
    REQUIRE(defocused != nullptr);
    const auto defocused_sources = r.counters()->dof_blur_source_pixels.load(
        std::memory_order_relaxed);

    CHECK(focused_sources == 0);
    CHECK(defocused_sources > 0);
}

TEST_CASE("PR2-RG-DoF: cache warm hit preserves DOF output and coverage") {
    auto cached = ctt::make_renderer();
    cached.runtime().node_cache().set_capacity(128 * 1024 * 1024);
    cached.reset_counters();
    auto cold = cached.render(make_sparse_dof_scene(), 0);
    REQUIRE(cold != nullptr);
    const auto cold_hash = ctt::framebuffer_hash(*cold);

    cached.reset_counters();
    auto warm = cached.render(make_sparse_dof_scene(), 0);
    REQUIRE(warm != nullptr);
    const auto* warm_counters = cached.counters();

    CHECK(ctt::framebuffer_hash(*warm) == cold_hash);
    CHECK(warm_counters->dof_blur_source_pixels.load(std::memory_order_relaxed) > 0);
    CHECK(warm_counters->dof_roi_pixels.load(std::memory_order_relaxed) > 0);

    auto uncached = ctt::make_renderer();
    uncached.runtime().node_cache().set_capacity(0);
    auto uncached_frame = uncached.render(make_sparse_dof_scene(), 0);
    REQUIRE(uncached_frame != nullptr);
    CHECK(ctt::framebuffer_hash(*uncached_frame) == cold_hash);
}
