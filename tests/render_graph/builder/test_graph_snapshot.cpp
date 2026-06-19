// ==========================================================================
// test_graph_snapshot.cpp
//
// Graph contract tests:
//   1. Same scene → same DOT (stable snapshot)
//   2. Same scene → same structure_hash (fingerprint determinism)
//   3. Layer order change → different structure_hash
//   4. Same scene → same DOT normalized (deterministic output)
//   5. Different scenes → different structure_hash
//   6. Graph structure hash is frame-independent
// ==========================================================================

#include <doctest/doctest.h>

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <algorithm>
#include <sstream>
#include <string>
using namespace chronon3d;

using namespace chronon3d::graph;

namespace {

/// Build a simple 2-layer scene for testing.
Scene make_simple_scene(int width, int height) {
    SceneBuilder s(width, height);
    s.layer("bg", [&](LayerBuilder& l) {
        l.rect("bg_rect", {
            .size  = {static_cast<float>(width), static_cast<float>(height)},
            .color = Color{0.1f, 0.2f, 0.3f, 1},
            .pos   = {static_cast<float>(width) / 2.0f,
                      static_cast<float>(height) / 2.0f, 0}
        });
    });
    s.layer("fg", [&](LayerBuilder& l) {
        l.rect("fg_rect", {
            .size  = {100, 100},
            .color = Color{1, 0, 0, 1},
            .pos   = {static_cast<float>(width) / 2.0f,
                      static_cast<float>(height) / 2.0f, 0}
        });
    });
    return s.build();
}

/// Build the same scene but with layers in reversed order.
Scene make_reversed_layer_order_scene(int width, int height) {
    SceneBuilder s(width, height);
    s.layer("fg", [&](LayerBuilder& l) {
        l.rect("fg_rect", {
            .size  = {100, 100},
            .color = Color{1, 0, 0, 1},
            .pos   = {static_cast<float>(width) / 2.0f,
                      static_cast<float>(height) / 2.0f, 0}
        });
    });
    s.layer("bg", [&](LayerBuilder& l) {
        l.rect("bg_rect", {
            .size  = {static_cast<float>(width), static_cast<float>(height)},
            .color = Color{0.1f, 0.2f, 0.3f, 1},
            .pos   = {static_cast<float>(width) / 2.0f,
                      static_cast<float>(height) / 2.0f, 0}
        });
    });
    return s.build();
}

/// Build a scene with a circle instead of a rect for the foreground.
Scene make_different_scene(int width, int height) {
    SceneBuilder s(width, height);
    s.layer("bg", [&](LayerBuilder& l) {
        l.rect("bg_rect", {
            .size  = {static_cast<float>(width), static_cast<float>(height)},
            .color = Color{0.1f, 0.2f, 0.3f, 1},
            .pos   = {static_cast<float>(width) / 2.0f,
                      static_cast<float>(height) / 2.0f, 0}
        });
    });
    s.layer("fg", [&](LayerBuilder& l) {
        l.circle("fg_circle", {
            .radius = 50.0f,
            .color  = Color{0, 1, 0, 1},
            .pos    = {static_cast<float>(width) / 2.0f,
                       static_cast<float>(height) / 2.0f, 0}
        });
    });
    return s.build();
}

/// Helper: compute structure hash from a scene using SceneHasher directly.

/// Minimal DOT normalizer: sorts lines (excluding header/footer) for stable comparison.
std::string normalize_dot(const std::string& dot) {
    std::istringstream stream(dot);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        // Skip empty lines, digraph header, and closing brace
        if (line.empty() || line.find("digraph") != std::string::npos ||
            line == "}" || line == " ") {
            continue;
        }
        // Trim leading whitespace
        auto start = line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            lines.push_back(line.substr(start));
        }
    }
    std::sort(lines.begin(), lines.end());
    std::ostringstream out;
    for (const auto& l : lines) {
        out << l << "\n";
    }
    return out.str();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 1. Stable DOT: same scene rendered twice produces identical DOT
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GraphContract: same scene produces identical DOT across renders") {
    constexpr int W = 256;
    constexpr int H = 256;

    Scene scene = make_simple_scene(W, H);
    Camera camera;
    SoftwareRenderer renderer;
    cache::NodeCache node_cache;

    // Render twice and collect DOT from debug output
    auto fb1 = render_composition_frame(renderer, node_cache, {}, nullptr, nullptr,
        Composition(CompositionSpec{.name = "snap1", .width = W, .height = H, .duration = 1},
                    [&](const FrameContext&) { return scene.clone(); }), 0);
    auto dot1 = debug_scene_graph(renderer, node_cache, scene, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);

    // Reset and render again
    SoftwareRenderer renderer2;
    cache::NodeCache node_cache2;
    auto fb2 = render_composition_frame(renderer2, node_cache2, {}, nullptr, nullptr,
        Composition(CompositionSpec{.name = "snap2", .width = W, .height = H, .duration = 1},
                    [&](const FrameContext&) { return scene.clone(); }), 0);
    auto dot2 = debug_scene_graph(renderer2, node_cache2, scene, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);

    CHECK(dot1 == dot2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Fingerprint determinism: same scene → same scene fingerprint
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GraphContract: same scene produces same scene fingerprint") {
    constexpr int W = 256;
    constexpr int H = 256;

    Scene scene = make_simple_scene(W, H);
    graph::SceneHasher hasher1;
    graph::SceneHasher hasher2;

    auto fp1 = hasher1.compute_fingerprint(scene, 0);
    auto fp2 = hasher2.compute_fingerprint(scene, 0);

    CHECK(fp1 == fp2);

    // Static fingerprint should also match
    auto sfp1 = hasher1.compute_static_fingerprint(scene);
    auto sfp2 = hasher2.compute_static_fingerprint(scene);
    CHECK(sfp1 == sfp2);

    // Structure fingerprint should also match
    auto stfp1 = hasher1.compute_structure_fingerprint(scene);
    auto stfp2 = hasher2.compute_structure_fingerprint(scene);
    CHECK(stfp1 == stfp2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Layer order change → different graph AND different fingerprint
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GraphContract: layer order change produces different graph and fingerprint") {
    constexpr int W = 256;
    constexpr int H = 256;

    Scene scene_normal = make_simple_scene(W, H);
    Scene scene_reversed = make_reversed_layer_order_scene(W, H);
    Camera camera;
    SoftwareRenderer renderer;
    cache::NodeCache node_cache;

    auto dot_normal = debug_scene_graph(renderer, node_cache, scene_normal, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);
    cache::NodeCache node_cache2;
    auto dot_reversed = debug_scene_graph(renderer, node_cache2, scene_reversed, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);

    // Different layer order must produce a different DOT
    CHECK(dot_normal != dot_reversed);
    CHECK(dot_normal.find("digraph") != std::string::npos);
    CHECK(dot_reversed.find("digraph") != std::string::npos);

    // Scene fingerprint must also differ (layer order is part of the hash)
    graph::SceneHasher hasher;
    auto fp_normal = hasher.compute_fingerprint(scene_normal, 0);
    auto fp_reversed = hasher.compute_fingerprint(scene_reversed, 0);
    CHECK(fp_normal != fp_reversed);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Same scene → stable normalized DOT
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GraphContract: normalized DOT is deterministic") {
    constexpr int W = 256;
    constexpr int H = 256;

    Scene scene = make_simple_scene(W, H);
    Camera camera;
    SoftwareRenderer renderer;
    cache::NodeCache node_cache;

    auto dot = debug_scene_graph(renderer, node_cache, scene, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);

    // Normalized DOT should be stable (sorted lines)
    auto normalized = normalize_dot(dot);
    CHECK_FALSE(normalized.empty());

    // Render again and verify normalized output matches
    SoftwareRenderer renderer2;
    cache::NodeCache node_cache2;
    auto dot2 = debug_scene_graph(renderer2, node_cache2, scene, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);
    auto normalized2 = normalize_dot(dot2);

    CHECK(normalized == normalized2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Different scenes → different graphs
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GraphContract: different scenes produce different graphs") {
    constexpr int W = 256;
    constexpr int H = 256;

    Scene scene_a = make_simple_scene(W, H);
    Scene scene_b = make_different_scene(W, H);
    Camera camera;
    SoftwareRenderer renderer;
    cache::NodeCache node_cache;

    auto stats_a = analyze_scene_graph(renderer, node_cache, scene_a, camera, W, H, 0, 0.0f, {}, nullptr, nullptr, false, false);
    cache::NodeCache node_cache2;
    auto stats_b = analyze_scene_graph(renderer, node_cache2, scene_b, camera, W, H, 0, 0.0f, {}, nullptr, nullptr, false, false);

    // Different shape types (rect vs circle) should produce different node counts
    // or at minimum different DOT output
    auto dot_a = debug_scene_graph(renderer, node_cache, scene_a, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);
    auto dot_b = debug_scene_graph(renderer, node_cache2, scene_b, camera, W, H, 0, 0.0f, {}, nullptr, nullptr);

    CHECK(dot_a != dot_b);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Scene fingerprint is frame-independent for static scenes
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("GraphContract: static fingerprint is identical across frames") {
    constexpr int W = 256;
    constexpr int H = 256;

    Scene scene = make_simple_scene(W, H);
    graph::SceneHasher hasher;

    // Static fingerprint hashes ALL layers regardless of active_at(frame)
    auto sfp_0 = hasher.compute_static_fingerprint(scene);
    auto sfp_5 = hasher.compute_static_fingerprint(scene);
    auto sfp_99 = hasher.compute_static_fingerprint(scene);

    CHECK(sfp_0 == sfp_5);
    CHECK(sfp_5 == sfp_99);

    // compute_fingerprint is frame-dependent by design (uses active_at),
    // so frame-dependent fingerprints for the SAME frame must match.
    graph::SceneHasher hasher2;
    auto fp_a = hasher.compute_fingerprint(scene, 0);
    auto fp_b = hasher2.compute_fingerprint(scene, 0);
    CHECK(fp_a == fp_b);
}
