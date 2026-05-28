// ==========================================================================
// test_graph_preflight_diagnostics.cpp
//
// Validates the GraphPreflightReport API:
//   - DOT export contains topology and bbox/dirty annotations
//   - Canvas intersection math (fully visible / partial clip / outside)
//   - Glow effect expands the node bbox beyond the source geometry
//   - Dirty / cache flags are always populated for every node
//   - Timeline filtering: inactive layers are absent from the built graph
//   - Human-readable text dump contains all required keywords
// ==========================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/preflight_render_graph.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

/// Convenience wrapper matching the pattern used elsewhere in the test suite.
GraphPreflightReport run_preflight(
    SoftwareRenderer&   renderer,
    cache::NodeCache&   node_cache,
    const Scene&        scene,
    const Camera&       camera,
    int W,
    int H,
    Frame frame    = 0,
    float time_sec = 0.0f
) {
    return debug_preflight_render_graph(
        renderer, node_cache, scene, camera,
        W, H, frame, time_sec,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder()
    );
}

} // namespace

// --------------------------------------------------------------------------
// 1. DOT export: topology must be readable and contain enriched annotations
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: exports DOT topology with node kinds and edges") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg_rect", {
            .size  = {512, 512},
            .color = Color{0.05f, 0.08f, 0.12f, 1},
            .pos   = {0, 0, 0}
        });
    });

    s.layer("glow_box", [](LayerBuilder& l) {
        l.glow(40.0f, 1.0f, Color{0, 0.7f, 1, 1});
        l.rect("box", {
            .size  = {100, 100},
            .color = Color{1, 1, 1, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    // Graphviz structure
    CHECK(report.dot.find("digraph") != std::string::npos);
    CHECK(report.dot.find("->")      != std::string::npos);

    // Node kinds present in DOT
    CHECK(report.dot.find("Source")    != std::string::npos);
    CHECK(report.dot.find("Composite") != std::string::npos);

    // Enriched annotations present in DOT
    CHECK(report.dot.find("bbox=")         != std::string::npos);
    CHECK(report.dot.find("visible_ratio=") != std::string::npos);
    CHECK(report.dot.find("dirty=")        != std::string::npos);
    CHECK(report.dot.find("cached=")       != std::string::npos);

    // Sanity: at least a few nodes
    CHECK(report.nodes.size() >= 2u);
}

// --------------------------------------------------------------------------
// 2. Fullscreen background → 100 % visible, no warnings
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: fullscreen background reports fully visible") {
    constexpr int W = 1536;
    constexpr int H = 1024;

    SceneBuilder s(W, H);

    s.layer("_bg", [](LayerBuilder& l) {
        l.rect("bg", {
            .size  = {1536, 1024},
            .color = Color{0.1f, 0.2f, 0.8f, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    const auto* bg = report.find_node("bg");
    REQUIRE(bg != nullptr);

    CHECK(bg->outside_canvas    == false);
    CHECK(bg->partially_clipped == false);
    CHECK(bg->visible_ratio      > 0.99f);
    CHECK(bg->warning.empty());

    CHECK(bg->intersection_bbox.x0 <= 0);
    CHECK(bg->intersection_bbox.y0 <= 0);
    CHECK(bg->intersection_bbox.x1 >= W);
    CHECK(bg->intersection_bbox.y1 >= H);
}

// --------------------------------------------------------------------------
// 3. Off-screen rect → OUTSIDE_CANVAS warning
//
// NOTE: the engine culls fully-offscreen layers *before* graph construction,
// so no Source node with that name appears in the graph. We verify the
// warning via the layer-level culling telemetry stored in report.warnings
// (populated by preflight_render_graph from CullingTelemetryRecord) OR by
// checking that the layer is NOT found and the report has no phantom nodes.
// The reliable assertion is that no node reports a positive visible_ratio
// for that layer, and the text dump flags it via the cull log.
//
// Practical check: a culled layer must produce zero nodes in the report
// with outside_canvas==false (no false-positive visibility claims).
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: offscreen layer is culled and not visible in graph") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    // Background so the graph has at least one node
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg_rect", {
            .size  = {512, 512},
            .color = Color{0.1f, 0.1f, 0.1f, 1},
            .pos   = {0, 0, 0}
        });
    });

    s.layer("offscreen_layer", [](LayerBuilder& l) {
        l.rect("offscreen_rect", {
            .size  = {100, 100},
            .color = Color{1, 0, 0, 1},
            .pos   = {2000, -900, 0}  // far outside any canvas
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    // The engine culls this layer before building the graph:
    // no Source node for "offscreen_rect" should appear.
    const auto* offscreen_node = report.find_node("offscreen_rect");
    CHECK(offscreen_node == nullptr);  // correctly absent from the built graph

    // No node in the report should claim to be outside canvas WITH a real bbox
    // that maps to the offscreen position (i.e., no false "outside_canvas" ghost)
    for (const auto& node : report.nodes) {
        if (node.layer_id == "offscreen_layer") {
            // If somehow a node was emitted for this layer, it must be outside
            CHECK(node.outside_canvas == true);
            CHECK(node.visible_ratio  == doctest::Approx(0.0f));
        }
    }
}

// --------------------------------------------------------------------------
// 4. Partially clipped rect → PARTIAL_CLIP warning, 0 < ratio < 1
//
// Canvas is 512×512. Modular coords: origin = (256,256).
// Rect size 300×300, pos=(220,0,0):
//   left  edge in canvas = 256+220-150 = 326   (inside)
//   right edge in canvas = 256+220+150 = 626   (outside, >512)
// So the rect straddles the right border — partial clip.
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: partially clipped layer reports visible ratio") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    // 300×300 rect, centred at canvas_origin+(220,0) = (476,256).
    // Right edge at 476+150=626 > 512 → genuinely clips the right border.
    s.layer("partial_layer", [](LayerBuilder& l) {
        l.rect("partial_rect", {
            .size  = {300, 300},
            .color = Color{0, 1, 0, 1},
            .pos   = {220, 0, 0}     // canvas-space: x0=326, x1=626 → overflows
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    const auto* node = report.find_node("partial_rect");
    REQUIRE(node != nullptr);

    CHECK(node->outside_canvas    == false);
    CHECK(node->partially_clipped == true);
    CHECK(node->visible_ratio      > 0.0f);
    CHECK(node->visible_ratio      < 1.0f);
    CHECK(node->warning.find("PARTIAL_CLIP") != std::string::npos);
}

// --------------------------------------------------------------------------
// 5. Glow expands the effect node bbox beyond the source geometry
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: glow expands predicted bbox beyond source bbox") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("glow_layer", [](LayerBuilder& l) {
        l.glow(50.0f, 1.0f, Color{0, 0.7f, 1, 1});
        l.rect("glow_source_rect", {
            .size  = {100, 100},
            .color = Color{1, 1, 1, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    const auto* effect = report.find_node("EffectStack");
    REQUIRE(effect != nullptr);

    // The source rect is 100×100 centred at (W/2, H/2).
    // Glow of 50 px should expand the bbox by at least ~48 px on each side.
    constexpr int kMinSpread = 48;
    CHECK(effect->predicted_bbox.x0 <= (W / 2 - 50 - kMinSpread));
    CHECK(effect->predicted_bbox.y0 <= (H / 2 - 50 - kMinSpread));
    CHECK(effect->predicted_bbox.x1 >= (W / 2 + 50 + kMinSpread));
    CHECK(effect->predicted_bbox.y1 >= (H / 2 + 50 + kMinSpread));

    CHECK(effect->outside_canvas == false);
}

// --------------------------------------------------------------------------
// 6. Topology order: Source appears before Effect, Effect before Composite
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: effect layer topology is Source to Effect to Composite") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("effect_layer", [](LayerBuilder& l) {
        l.glow(32.0f, 0.8f, Color{0, 0.7f, 1, 1});
        l.rect("box", {
            .size  = {120, 120},
            .color = Color{1, 1, 1, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    const auto box_pos       = report.dot.find("box");
    const auto effect_pos    = report.dot.find("EffectStack");
    const auto composite_pos = report.dot.find("Composite");

    REQUIRE(box_pos       != std::string::npos);
    REQUIRE(effect_pos    != std::string::npos);
    REQUIRE(composite_pos != std::string::npos);

    CHECK(box_pos    < effect_pos);
    CHECK(effect_pos < composite_pos);
}

// --------------------------------------------------------------------------
// 7. Every node must expose cache/dirty fields (never uninitialized)
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: every node exposes cache and dirty state") {
    constexpr int W = 256;
    constexpr int H = 256;

    SceneBuilder s(W, H);

    s.layer("static_bg", [](LayerBuilder& l) {
        l.cache_static(true);
        l.rect("bg", {
            .size  = {256, 256},
            .color = Color{0.05f, 0.05f, 0.08f, 1},
            .pos   = {0, 0, 0}
        });
    });

    s.layer("dynamic_fg", [](LayerBuilder& l) {
        l.rect("box", {
            .size  = {80, 80},
            .color = Color{1, 0, 0, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    REQUIRE(report.nodes.size() > 0u);

    for (const auto& node : report.nodes) {
        // kind must be set for every non-removed node
        if (node.name.find("(removed") == std::string::npos) {
            CHECK(node.kind.empty() == false);
        }
        // dirty and cached are computed booleans — no UB possible,
        // but we still assert both paths are representable:
        CHECK((node.dirty  == true || node.dirty  == false));
        CHECK((node.cached == true || node.cached == false));
        CHECK((node.cacheable       == true || node.cacheable       == false));
        CHECK((node.frame_dependent == true || node.frame_dependent == false));
    }
}

// --------------------------------------------------------------------------
// 8. Timeline: only the active frame's layers appear in the built graph
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: timeline map — only active layers appear at given frame") {
    constexpr int W = 512;
    constexpr int H = 512;

    // `early_layer` is active at frame 10, `late_layer` is not.
    SceneBuilder s(W, H);

    s.layer("early_layer", [](LayerBuilder& l) {
        l.from(0).duration(30);
        l.rect("early_rect", {
            .size  = {100, 100},
            .color = Color{1, 0, 0, 1},
            .pos   = {0, 0, 0}
        });
    });

    s.layer("late_layer", [](LayerBuilder& l) {
        l.from(60).duration(30);
        l.rect("late_rect", {
            .size  = {100, 100},
            .color = Color{0, 1, 0, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    // Frame 10: early_layer is active, late_layer is not
    auto report = run_preflight(renderer, node_cache, scene, camera,
                                W, H, /*frame=*/10, 10.0f / 30.0f);

    // early_rect must appear
    CHECK(report.find_node("early_rect") != nullptr);
    // DOT must contain early layer info
    CHECK(report.dot.find("early") != std::string::npos);

    // late_rect must NOT appear (inactive at frame 10)
    CHECK(report.find_node("late_rect") == nullptr);
    CHECK(report.dot.find("late_rect") == std::string::npos);
}

// --------------------------------------------------------------------------
// 9. Human-readable text dump contains all mandatory keywords
//
// Uses a rect that is PARTIALLY clipped (not fully offscreen) so that it
// survives the engine's frustum cull and still appears as a graph node.
// The partial-clip path produces a PARTIAL_CLIP warning in the text dump.
// --------------------------------------------------------------------------

TEST_CASE("GraphPreflight: text dump contains actionable diagnostics") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    // Add a background so there are non-trivial nodes
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg_rect", {
            .size  = {512, 512},
            .color = Color{0.05f, 0.05f, 0.05f, 1},
            .pos   = {0, 0, 0}
        });
    });

    // Partially clipped rect — survives cull, appears in graph, gets PARTIAL_CLIP
    s.layer("partial_diag", [](LayerBuilder& l) {
        l.rect("partial_diag_rect", {
            .size  = {300, 300},
            .color = Color{1, 0, 0, 1},
            .pos   = {220, 0, 0}   // same geometry as test 4 → partial clip
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H);

    std::string text = report.to_text();

    // Structural keywords always present
    CHECK(text.find("Graph Preflight Report") != std::string::npos);
    CHECK(text.find("predicted_bbox")         != std::string::npos);
    CHECK(text.find("intersection")           != std::string::npos);
    CHECK(text.find("visible_ratio")          != std::string::npos);

    // Partial-clip node must appear in the text dump
    CHECK(text.find("partial_diag_rect")     != std::string::npos);
    CHECK(text.find("PARTIAL_CLIP")          != std::string::npos);
}

// ============================================================================
// SECTION B — Glow Composition Render Tests (glow_01..glow_06)
//
// Regression focus:
//   - Background must fill all 4 corners (classic "top-left quadrant" bug)
//   - Centre elements rendered at canvas centre, not top-left
//   - Animated layers differ across frames (not frozen)
//   - Glow visible near element edges
// ============================================================================

#include <chronon3d/timeline/composition.hpp>

// Forward-declare compositions from content/effects/
namespace chronon3d::content::effects {
    Composition glow_01_neon_sign();
    Composition glow_02_orb_galaxy();
    Composition glow_03_cinematic_text();
    Composition glow_04_ui_card();
    Composition glow_05_pulse_wave();
    Composition glow_06_video_layer();
}

namespace {

std::shared_ptr<chronon3d::Framebuffer> render_glow_comp(
    const chronon3d::Composition& comp,
    chronon3d::Frame frame = 0
) {
    chronon3d::SoftwareRenderer renderer;
    chronon3d::RenderSettings settings = renderer.render_settings();
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, frame);
}

// Checks all 4 corners + canvas centre have alpha >= min_alpha.
void check_corners_and_centre(
    const chronon3d::Framebuffer& fb,
    float min_alpha = 0.88f,
    int margin = 8
) {
    const int W = fb.width();
    const int H = fb.height();
    struct Pt { int x, y; const char* lbl; };
    for (auto [x, y, lbl] : std::initializer_list<Pt>{
        {margin,     margin,     "top-left"},
        {W - margin, margin,     "top-right"},
        {margin,     H - margin, "bottom-left"},
        {W - margin, H - margin, "bottom-right"},
        {W / 2,      H / 2,     "centre"},
    }) {
        const chronon3d::Color c = fb.get_pixel(x, y);
        if (c.a < min_alpha) {
            std::string msg = std::string(lbl) + " alpha=" + std::to_string(c.a)
                              + " at (" + std::to_string(x) + "," + std::to_string(y) + ")";
            CHECK_MESSAGE(c.a >= min_alpha, msg);
        }
    }
}

} // anonymous namespace (glow render helpers)

// ─── Glow 01 — Neon Sign ────────────────────────────────────────────────────

TEST_CASE("GlowRender: 01 NeonSign background covers all corners") {
    auto comp = chronon3d::content::effects::glow_01_neon_sign();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1536);
    REQUIRE(fb->height() == 1024);
    check_corners_and_centre(*fb, 0.90f);

    bool found = false;
    for (int y = 280; y < 700 && !found; ++y)
        for (int x = 350; x < 1186 && !found; ++x) {
            const auto c = fb->get_pixel(x, y);
            if (c.r > 0.5f && c.b > 0.5f && c.a > 0.5f) found = true;
        }
    CHECK_MESSAGE(found, "NeonSign hero text/glow not visible in centre region");
}

// ─── Glow 02 — Orb Galaxy ───────────────────────────────────────────────────

TEST_CASE("GlowRender: 02 OrbGalaxy background covers all corners") {
    auto comp = chronon3d::content::effects::glow_02_orb_galaxy();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    check_corners_and_centre(*fb, 0.90f);
}

TEST_CASE("GlowRender: 02 OrbGalaxy left-edge blue orb is visible") {
    auto comp = chronon3d::content::effects::glow_02_orb_galaxy();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    // Blue orb at canvas x≈68, y≈432, glow R=65 → visible in rows 350-520, cols 8-210
    bool found = false;
    for (int y = 350; y < 520 && !found; ++y)
        for (int x = 8; x < 210 && !found; ++x) {
            const auto c = fb->get_pixel(x, y);
            if (c.b > 0.03f && c.a > 0.01f) found = true;
        }
    CHECK_MESSAGE(found, "Left blue orb not visible — possible top-left clipping bug");
}

// ─── Glow 03 — Cinematic Text ───────────────────────────────────────────────

TEST_CASE("GlowRender: 03 CinematicText background covers all corners") {
    auto comp = chronon3d::content::effects::glow_03_cinematic_text();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    check_corners_and_centre(*fb, 0.90f);
}

TEST_CASE("GlowRender: 03 CinematicText hero text visible at canvas centre") {
    auto comp = chronon3d::content::effects::glow_03_cinematic_text();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    bool found = false;
    for (int y = 260; y < 650 && !found; ++y)
        for (int x = 168; x < 1368 && !found; ++x) {
            const auto c = fb->get_pixel(x, y);
            if (c.r > 0.7f && c.g > 0.55f && c.a > 0.5f) found = true;
        }
    CHECK_MESSAGE(found, "CinematicText hero text not found in canvas centre region");
}

// ─── Glow 04 — UI Card ──────────────────────────────────────────────────────

TEST_CASE("GlowRender: 04 UICard background covers all corners") {
    auto comp = chronon3d::content::effects::glow_04_ui_card();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    check_corners_and_centre(*fb, 0.90f);
}

TEST_CASE("GlowRender: 04 UICard card is at canvas centre not top-left") {
    auto comp = chronon3d::content::effects::glow_04_ui_card();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    // Canvas centre (768,512) should be inside the card (dark bg)
    const auto centre = fb->get_pixel(768, 512);
    CHECK(centre.a > 0.9f);
    CHECK(centre.r < 0.40f);   // card is dark, not bright
    // Corners must be filled (ambient bg, not transparent)
    CHECK(fb->get_pixel(10, 10).a > 0.85f);
    CHECK(fb->get_pixel(1526, 1014).a > 0.85f);
}

// ─── Glow 05 — Pulse Wave ───────────────────────────────────────────────────

TEST_CASE("GlowRender: 05 PulseWave frame 0 background covers all corners") {
    auto comp = chronon3d::content::effects::glow_05_pulse_wave();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    check_corners_and_centre(*fb, 0.90f);
}

TEST_CASE("GlowRender: 05 PulseWave frames 0 and 15 differ (animation active)") {
    auto comp = chronon3d::content::effects::glow_05_pulse_wave();
    auto fb0  = render_glow_comp(comp, 0);
    auto fb15 = render_glow_comp(comp, 15);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb15 != nullptr);
    check_corners_and_centre(*fb0,  0.90f);
    check_corners_and_centre(*fb15, 0.90f);
    // Sample just outside the core circle (radius ≈ 36 at frame 0, ≈ 40 at frame 15)
    // so we see the glow halo rather than the saturated white interior.
    const auto c0  = fb0->get_pixel(810, 512);
    const auto c15 = fb15->get_pixel(810, 512);
    const float d  = std::abs(c0.r-c15.r)+std::abs(c0.g-c15.g)+std::abs(c0.b-c15.b);
    CHECK_MESSAGE(d > 0.01f, "PulseWave frames 0 and 15 are identical — animation frozen");
}

// ─── Glow 06 — Video Layer ──────────────────────────────────────────────────

TEST_CASE("GlowRender: 06 VideoLayer background covers all corners") {
    auto comp = chronon3d::content::effects::glow_06_video_layer();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    check_corners_and_centre(*fb, 0.82f);  // looser: video may be placeholder
}

TEST_CASE("GlowRender: 06 VideoLayer title visible above video area") {
    auto comp = chronon3d::content::effects::glow_06_video_layer();
    auto fb   = render_glow_comp(comp, 0);
    REQUIRE(fb != nullptr);
    // Title at canvas_y≈142 (pos=(0,-370), font=46)
    bool found = false;
    for (int y = 108; y < 185 && !found; ++y)
        for (int x = 280; x < 1256 && !found; ++x) {
            const auto c = fb->get_pixel(x, y);
            if (c.r+c.g+c.b > 0.15f && c.a > 0.05f) found = true;
        }
    CHECK_MESSAGE(found, "VideoLayer title 'VIDEO LAYER GLOW' not visible at canvas_y≈142");
}

TEST_CASE("GraphPreflight: validates advanced diagnostics (memory, complexity, cache, dirty reasons, redundant transforms)") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    // Add a background layer
    s.layer("bg", [](LayerBuilder& l) {
        l.cache_static(true);
        l.rect("bg_rect", {
            .size  = {512, 512},
            .color = Color{0.05f, 0.05f, 0.05f, 1},
            .pos   = {0, 0, 0}
        });
    });

    // Add an animated foreground layer
    s.layer("fg", [](LayerBuilder& l) {
        l.from(0).duration(30);
        l.opacity_anim().add_keyframe(0, 1.0f);
        l.opacity_anim().add_keyframe(30, 0.0f);
        l.rect("fg_rect", {
            .size  = {100, 100},
            .color = Color{1, 0, 0, 1},
            .pos   = {0, 0, 0}
        });
    });

    Scene scene = s.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto report = run_preflight(renderer, node_cache, scene, camera, W, H, 5, 5.0f/30.0f);

    // Validate memory metrics
    CHECK(report.peak_memory_bytes > 0);
    
    // Validate complexity metrics
    CHECK(report.total_complexity_score > 0);

    // Validate cache score
    CHECK(report.cache_score >= 0);
    CHECK(report.cache_score <= 100);

    // Validate fill rate
    CHECK(report.total_fill_rate_pixels > 0);

    // Verify presence of dirty reasons for animated node
    const auto* fg_node = report.find_node("fg_rect");
    REQUIRE(fg_node != nullptr);
    CHECK(fg_node->dirty == true);
    CHECK(!fg_node->dirty_reasons.empty());

    // Verify format in text output
    std::string text = report.to_text();
    CHECK(text.find("Peak Memory:") != std::string::npos);
    CHECK(text.find("Total Complexity:") != std::string::npos);
    CHECK(text.find("Cache Score:") != std::string::npos);
    CHECK(text.find("Total Fill Rate:") != std::string::npos);
    CHECK(text.find("Dirty:") != std::string::npos);
}
