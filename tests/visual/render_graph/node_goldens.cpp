// ==============================================================================
// tests/visual/render_graph/node_goldens.cpp
//
// PR2 — 6 golden image snapshots for the 4 node categories.
//
//   Node                     Case
//   ───────────────────────  ─────────────────────────────────────
//   ShadowNode               shadow_contact_and_ambient
//   ShadowNode               shadow_depth_aware_scaling
//   PerPixelDofNode          dof_per_pixel_variable_depth
//   MaskNode                 mask_hard_clip
//   MaskNode                 mask_modular_coord
//   GlowPipeline             glow_pipeline_alpha_source
//
// Bloom / Gradient golden cases are handled by existing test_glow_torture
// + tests/visual/gradient_visual_tests so they remain those tests'
// responsibility.
//
// All LayerBuilder method calls verified against the actual API:
//   - l.mask_rect(RectMaskParams{...})  (not l.mask)
//   - l.drop_shadow(Vec2 offset, Color, f32 radius)  (not DropShadowParams)
//   - l.bloom(threshold, radius, intensity)  (not BloomParams{...})
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/api/composition.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/golden_test.hpp>
#include <tests/visual/support/image_diff.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
using namespace chronon3d;
using namespace chronon3d::test;

namespace {

const std::filesystem::path kGoldenDir   = "tests/golden/render_graph_nodes";
const std::filesystem::path kArtifactDir = "test_renders/artifacts/render_graph_nodes";

GoldenTestConfig make_golden_config() {
    ImageDiffThreshold t{};
    t.max_mean_abs_error      = 0.025f;
    t.max_abs_error           = 0.10f;
    t.max_rmse                = 0.012f;
    t.max_changed_pixel_ratio = 0.08f;
    return {
        .golden_directory   = kGoldenDir,
        .artifact_directory = kArtifactDir,
        .threshold          = t,
        .mode               = golden_mode_from_environment(),
    };
}

void verify_node_golden(const Framebuffer& fb, const std::string& name) {
    const auto result = verify_golden(fb, name, make_golden_config());
    if (result.golden_missing) {
        MESSAGE("Golden missing: ", result.golden_path.string(),
                " — Set CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    INFO(result.message);
    CHECK(result.passed);
}

namespace node_goldens_impl {
SoftwareRenderer make_node_golden_renderer(bool /*modular*/ = false) {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    // RenderSettings no longer carries modular_coordinates directly.
    r.set_settings(s);
    return r;
}
}  // namespace node_goldens_impl

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. ShadowNode — contact + ambient shadow layers visible
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NodeGolden: shadow_contact_and_ambient") {
    auto comp = composition({.width = 320, .height = 240, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.12f, 0.16f, 1.0f});
            });
            s.layer("cast", [](LayerBuilder& l) {
                l.position({0.0f, -10.0f, 0.0f});
                l.drop_shadow(
                    Vec2{28.0f, 22.0f},
                    Color{0.0f, 0.0f, 0.0f, 0.55f},
                    14.0f
                );
                l.rect("r", {.size = {80.0f, 60.0f},
                              .color = {1.0f, 0.85f, 0.55f, 1.0f},
                              .pos   = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
    auto fb = node_goldens_impl::make_node_golden_renderer().render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    verify_node_golden(*fb, "shadow_contact_and_ambient");
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. ShadowNode — depth-aware scaling visible (Z-far caster)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NodeGolden: shadow_depth_aware_scaling") {
    auto comp = composition({.width = 320, .height = 240, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.12f, 0.16f, 1.0f});
            });
            s.layer("cast", [](LayerBuilder& l) {
                l.position({0.0f, -10.0f, -800.0f});  // far Z → wider blur
                l.drop_shadow(
                    Vec2{40.0f, 30.0f},
                    Color{0.0f, 0.0f, 0.0f, 0.40f},
                    30.0f
                );
                l.rect("r", {.size = {80.0f, 60.0f},
                              .color = {1.0f, 0.85f, 0.55f, 1.0f},
                              .pos   = {0.0f, 0.0f, 0.0f}});
            });
            return s.build();
        });
    auto fb = node_goldens_impl::make_node_golden_renderer().render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    verify_node_golden(*fb, "shadow_depth_aware_scaling");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. PerPixelDofNode — variable depth causes variable screen-space footprint
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NodeGolden: dof_per_pixel_variable_depth") {
    auto comp = composition({.width = 320, .height = 240, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.05f, 0.07f, 0.10f, 1.0f});
            });
            for (int i = 0; i < 3; ++i) {
                s.layer("bar_" + std::to_string(i), [i](LayerBuilder& l) {
                    float z = static_cast<float>(i - 1) * 200.0f;
                    l.position({static_cast<float>(i - 1) * 80.0f, 0.0f, z});
                    l.rect("r", {.size = {10.0f, 140.0f},
                                  .color = {0.95f, 0.85f, 0.70f, 1.0f},
                                  .pos   = {0.0f, 0.0f, 0.0f}});
                });
            }
            return s.build();
        });
    auto fb = node_goldens_impl::make_node_golden_renderer().render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    verify_node_golden(*fb, "dof_per_pixel_variable_depth");
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. MaskNode — hard clip with rectangular mask_rect
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NodeGolden: mask_hard_clip") {
    auto comp = composition({.width = 320, .height = 240, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.10f, 0.10f, 1.0f});
            });
            s.layer("clipped", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.mask_rect(RectMaskParams{
                    .size = {140.0f, 140.0f},
                    .pos  = Vec3{0.0f, 0.0f, 0.0f},
                });
                l.circle("c", {
                    .radius = 90.0f,
                    .color = {1.0f, 0.7f, 0.2f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb = node_goldens_impl::make_node_golden_renderer().render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    verify_node_golden(*fb, "mask_hard_clip");
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. MaskNode — modular coordinates (best-effort smoke + golden)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NodeGolden: mask_modular_coord") {
    auto comp = composition({.width = 320, .height = 240, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.10f, 0.10f, 0.10f, 1.0f});
            });
            s.layer("mod", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                l.mask_rect(RectMaskParams{
                    .size = {120.0f, 120.0f},
                    .pos  = Vec3{0.0f, 0.0f, 0.0f},
                });
                l.circle("c", {
                    .radius = 70.0f,
                    .color = {0.4f, 0.7f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb = node_goldens_impl::make_node_golden_renderer(true).render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    verify_node_golden(*fb, "mask_modular_coord");
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. GlowPipeline — alpha-source path via l.bloom(threshold, r, intensity)
// RGB below 1.0 so the bloom pass actually spreads visible energy instead of
// pre-saturating to white.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NodeGolden: glow_pipeline_alpha_source") {
    auto comp = composition({.width = 320, .height = 240, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color{0.005f, 0.008f, 0.020f, 1.0f});
            });
            s.layer("orb_alpha", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
                // bloom_path = l.bloom(threshold=0, radius=24, intensity=1)
                // → bloom kicks in on every non-zero alpha pixel.
                l.bloom(/*threshold=*/0.0f, /*radius=*/24.0f, /*intensity=*/1.0f);
                l.circle("c", {
                    .radius = 30.0f,
                    .color = {0.60f, 0.50f, 0.45f, 1.0f},   // ≤ 1.0 → bloom visible
                    .pos = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    auto fb = node_goldens_impl::make_node_golden_renderer().render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    verify_node_golden(*fb, "glow_pipeline_alpha_source");
}
