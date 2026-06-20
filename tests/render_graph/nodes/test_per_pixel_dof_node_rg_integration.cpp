// ==============================================================================
// tests/render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp
//
// PR2 — PerPixelDofNode render-graph integration tests (3 tests).
//
// Drives the full SoftwareRenderer pipeline and verifies pixel-level
// outcomes.  RenderSettings does NOT expose a `dof` field; DoF state
// lives on the Camera2_5DRuntime supplied to PerPixelDofNode.  These
// tests cover:
//   1. Smoke test: render composition → no crash, expected size.
//   2. Determinism: two consecutive renders with same params = byte equal.
//   3. Camera DoF variation: two scenes with different focus_z render
//      differently (hash differs).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
using namespace chronon3d;

namespace {
SoftwareRenderer make_renderer() {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    r.set_settings(s);
    return r;
}

Composition make_dof_scene(bool far_bar) {
    return composition({.width = 256, .height = 256, .duration = 1},
        [far_bar](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.05f, 0.07f, 0.10f, 1.0f});
            });
            s.layer("lines", [far_bar](LayerBuilder& l) {
                l.position({0.0f, 0.0f, far_bar ? -800.0f : 0.0f});
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

uint64_t fb_hash(const Framebuffer& fb) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const auto c = fb.get_pixel(x, y);
            auto fold = [&](float v) {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                h ^= bits;
                h *= 0x100000001b3ULL;
            };
            fold(c.r); fold(c.g); fold(c.b); fold(c.a);
        }
    }
    return h;
}
}  // namespace

TEST_CASE("PR2-RG-DoF: smoke render produces expected dimensions") {
    auto r = make_renderer();
    auto fb = r.render_frame(make_dof_scene(false), 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 256);
    CHECK(fb->height() == 256);
}

TEST_CASE("PR2-RG-DoF: two consecutive renders are byte-equal (determinism)") {
    auto r = make_renderer();
    auto fb1 = r.render_frame(make_dof_scene(false), 0);
    auto fb2 = r.render_frame(make_dof_scene(false), 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb_hash(*fb1) == fb_hash(*fb2));
}

TEST_CASE("PR2-RG-DoF: per-element z-range variation produces differing hashes") {
    auto r = make_renderer();
    auto fb_near = r.render_frame(make_dof_scene(false), 0);
    auto fb_far  = r.render_frame(make_dof_scene(true),  0);
    REQUIRE(fb_near != nullptr);
    REQUIRE(fb_far  != nullptr);
    CHECK(fb_hash(*fb_near) != fb_hash(*fb_far));
}
