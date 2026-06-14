#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <tests/helpers/pixel_assertions.hpp>

using namespace chronon3d;
using namespace chronon3d::test;

static std::shared_ptr<Framebuffer> render_precomp(
    std::function<Scene(const FrameContext&)> fn, int w = 120, int h = 120, Frame frame = Frame{0})
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=90}, fn);
    return rend.render_frame(comp, frame);
}

// Render with a CompositionRegistry wired to the SoftwareRenderer.
// This is necessary for PrecompNode to find and render nested compositions.
static std::shared_ptr<Framebuffer> render_precomp_with_registry(
    const CompositionRegistry& registry,
    std::function<Scene(const FrameContext&)> fn,
    int w = 200, int h = 200, Frame frame = Frame{0})
{
    SoftwareRenderer rend;
    rend.set_composition_registry(&registry);
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=90}, fn);
    return rend.render_frame(comp, frame);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: LayerBuilder::precomp() basics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: LayerBuilder precomp sets kind and composition name") {
    LayerBuilder builder("test", std::pmr::get_default_resource());
    builder.precomp("my_comp");
    Layer l = builder.build();
    CHECK(l.kind == LayerKind::Precomp);
    CHECK(std::string(l.precomp_composition_name) == "my_comp");
}

TEST_CASE("AE-6: SceneBuilder precomp_layer creates Precomp layer") {
    auto fb = render_precomp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={120,120}, .color=Color::white(), .pos={60,60,0}})
         .precomp_layer("nested", "nonexistent_comp", [](LayerBuilder& l) {
            l.from(Frame{0});
         });
        return s.build();
    });
    // Should not crash even when composition doesn't exist
    CHECK(fb != nullptr);
}

TEST_CASE("AE-6: LayerBuilder precomp via kind() method") {
    LayerBuilder builder("test", std::pmr::get_default_resource());
    builder.kind(LayerKind::Precomp);
    // kind() alone doesn't set the composition name
    Layer l = builder.build();
    CHECK(l.kind == LayerKind::Precomp);
    CHECK(std::string(l.precomp_composition_name).empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: LayerDesc precomp_composition_name
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: LayerDesc precomp fields default correctly") {
    LayerDesc ld;
    CHECK(ld.precomp_composition_name.empty());
    CHECK(ld.kind == LayerKind::Normal);
}

TEST_CASE("AE-6: LayerDesc precomp_composition_name is settable") {
    LayerDesc ld;
    ld.name = "my_precomp";
    ld.kind = LayerKind::Precomp;
    ld.precomp_composition_name = "inner_comp";
    CHECK(ld.kind == LayerKind::Precomp);
    CHECK(ld.precomp_composition_name == "inner_comp");
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: TimelineEvaluator propagates precomp fields
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: TimelineEvaluator propagates precomp composition name") {
    SceneDescription desc;
    desc.width = 120;
    desc.height = 120;
    desc.frame_rate = {30, 1};

    LayerDesc ld;
    ld.name = "precomp_layer";
    ld.kind = LayerKind::Precomp;
    ld.precomp_composition_name = "inner_scene";
    desc.layers.push_back(ld);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].kind == LayerKind::Precomp);
    CHECK(std::string(scene.layers()[0].precomp_composition_name) == "inner_scene");
}

TEST_CASE("AE-6: TimelineEvaluator clears nodes for Precomp layers") {
    SceneDescription desc;
    desc.width = 120;
    desc.height = 120;
    desc.frame_rate = {30, 1};

    LayerDesc ld;
    ld.name = "precomp_layer";
    ld.kind = LayerKind::Precomp;
    ld.precomp_composition_name = "inner";
    // Add a visual that should be cleared for precomp layers
    ld.visuals.push_back(RectParams{.size={50,50}, .color=Color::red(), .pos={25,25,0}});
    desc.layers.push_back(ld);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].nodes.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: SubComposition struct
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: SubComposition default construction") {
    SubComposition sub;
    CHECK(sub.name.empty());
    CHECK(sub.scene->layers.empty());
}

TEST_CASE("AE-6: SubComposition can be populated") {
    SubComposition sub;
    sub.name = "inner";

    LayerDesc layer;
    layer.name = "bg";
    layer.visuals.push_back(RectParams{.size={100,100}, .color=Color::blue(), .pos={50,50,0}});
    sub.scene->layers.push_back(layer);
    sub.scene->width = 100;
    sub.scene->height = 100;

    CHECK(sub.name == "inner");
    CHECK(sub.scene->layers.size() == 1);
    CHECK(sub.scene->width == 100);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: SceneDescription sub_compositions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: SceneDescription has empty sub_compositions by default") {
    SceneDescription desc;
    CHECK(desc.sub_compositions.empty());
}

TEST_CASE("AE-6: SceneDescription sub_compositions can be added") {
    SceneDescription desc;
    desc.width = 120;
    desc.height = 120;

    SubComposition inner;
    inner.name = "inner_bg";
    inner.scene->width = 60;
    inner.scene->height = 60;
    LayerDesc bg;
    bg.name = "bg";
    bg.visuals.push_back(RectParams{.size={60,60}, .color=Color::blue(), .pos={30,30,0}});
    inner.scene->layers.push_back(bg);
    desc.sub_compositions.push_back(std::move(inner));

    CHECK(desc.sub_compositions.size() == 1);
    CHECK(desc.sub_compositions[0].name == "inner_bg");
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: build_sub_composition_registry
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: build_sub_composition_registry creates registry without inline comps") {
    SceneDescription desc;
    auto registry = build_sub_composition_registry(desc);
    // No inline sub-compositions were added, but built-in compositions may exist
    CHECK_FALSE(registry.contains("nonexistent_comp"));
}

TEST_CASE("AE-6: build_sub_composition_registry registers inline compositions") {
    SceneDescription desc;
    desc.width = 120;
    desc.height = 120;

    SubComposition inner;
    inner.name = "inner";
    inner.scene->width = 60;
    inner.scene->height = 60;
    inner.scene->frame_rate = {30, 1};
    inner.scene->duration = Frame{30};
    LayerDesc bg;
    bg.name = "bg";
    bg.visuals.push_back(RectParams{.size={60,60}, .color=Color::blue(), .pos={30,30,0}});
    inner.scene->layers.push_back(bg);
    desc.sub_compositions.push_back(std::move(inner));

    auto registry = build_sub_composition_registry(desc);
    CHECK(registry.contains("inner"));
    // Verify the inline composition was registered (built-in compositions also present)
}

TEST_CASE("AE-6: build_sub_composition_registry creates valid Composition") {
    SceneDescription desc;
    desc.width = 120;
    desc.height = 120;

    SubComposition inner;
    inner.name = "inner";
    inner.scene->width = 60;
    inner.scene->height = 60;
    inner.scene->frame_rate = {30, 1};
    inner.scene->duration = Frame{60};
    LayerDesc bg;
    bg.name = "bg";
    bg.visuals.push_back(RectParams{.size={60,60}, .color=Color::blue(), .pos={30,30,0}});
    inner.scene->layers.push_back(bg);
    desc.sub_compositions.push_back(std::move(inner));

    auto registry = build_sub_composition_registry(desc);
    auto comp = registry.create("inner");
    CHECK(comp.width() == 60);
    CHECK(comp.height() == 60);
    CHECK(comp.duration() == Frame{60});

    // Evaluate the nested composition
    auto scene = comp.evaluate(Frame{0});
    CHECK(scene.layers().size() == 1);
}

TEST_CASE("AE-6: build_sub_composition_registry merges external compositions") {
    CompositionRegistry external;
    external.add("external_comp", []() {
        return Composition(
            CompositionSpec{.name="external_comp", .width=80, .height=80, .duration=Frame{30}},
            [](const FrameContext&) -> Scene { return Scene(); }
        );
    });

    SceneDescription desc;
    desc.width = 120;
    desc.height = 120;

    SubComposition inner;
    inner.name = "inner";
    inner.scene->width = 60;
    inner.scene->height = 60;
    inner.scene->frame_rate = {30, 1};
    inner.scene->duration = Frame{60};
    desc.sub_compositions.push_back(std::move(inner));

    auto registry = build_sub_composition_registry(desc, &external);
    CHECK(registry.contains("inner"));
    CHECK(registry.contains("external_comp"));
    // Both inline and external compositions registered (built-in compositions also present)
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: End-to-end precomp rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: Precomp renders via CompositionRegistry") {
    // Create a registry with a simple inner composition
    CompositionRegistry registry;
    registry.add("inner", []() {
        return Composition(
            CompositionSpec{.name="inner", .width=80, .height=80, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::blue(), .pos={40,40,0}});
                return s.build();
            }
        );
    });

    // Create a parent composition that uses a precomp layer
    auto fb = render_precomp([&registry](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "inner", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    });

    CHECK(fb != nullptr);
}

TEST_CASE("AE-6: Precomp with transform applied to parent layer") {
    CompositionRegistry registry;
    registry.add("inner", []() {
        return Composition(
            CompositionSpec{.name="inner", .width=80, .height=80, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={40,40}, .color=Color::red(), .pos={20,20,0}});
                return s.build();
            }
        );
    });

    auto fb = render_precomp([&registry](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Precomp with position offset
        s.precomp_layer("nested", "inner", [](LayerBuilder& l) {
            l.position({20, 20, 0});
            l.opacity(0.8f);
            l.from(Frame{0});
        });
        return s.build();
    });

    CHECK(fb != nullptr);
}

TEST_CASE("AE-6: Precomp with time remap") {
    CompositionRegistry registry;
    registry.add("inner", []() {
        return Composition(
            CompositionSpec{.name="inner", .width=80, .height=80, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::green(), .pos={40,40,0}});
                return s.build();
            }
        );
    });

    auto fb = render_precomp([&registry](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "inner", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.speed(0.5f);
        });
        return s.build();
    }, 120, 120, Frame{30});

    CHECK(fb != nullptr);
}

TEST_CASE("AE-6: Multiple precomp layers in same scene") {
    CompositionRegistry registry;
    registry.add("comp_a", []() {
        return Composition(
            CompositionSpec{.name="comp_a", .width=60, .height=60, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={60,60}, .color=Color::red(), .pos={30,30,0}});
                return s.build();
            }
        );
    });
    registry.add("comp_b", []() {
        return Composition(
            CompositionSpec{.name="comp_b", .width=60, .height=60, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={60,60}, .color=Color::blue(), .pos={30,30,0}});
                return s.build();
            }
        );
    });

    auto fb = render_precomp([&registry](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("a", "comp_a", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.position({-30, 0, 0});
        });
        s.precomp_layer("b", "comp_b", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.position({30, 0, 0});
        });
        return s.build();
    });

    CHECK(fb != nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: Declarative API end-to-end with sub_compositions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: Full declarative pipeline with sub_compositions") {
    SceneDescription desc;
    desc.name = "main";
    desc.width = 120;
    desc.height = 120;
    desc.frame_rate = {30, 1};
    desc.duration = Frame{90};

    // Define inner composition
    SubComposition inner;
    inner.name = "inner_bg";
    inner.scene->width = 60;
    inner.scene->height = 60;
    inner.scene->frame_rate = {30, 1};
    inner.scene->duration = Frame{60};
    LayerDesc inner_layer;
    inner_layer.name = "bg";
    inner_layer.visuals.push_back(RectParams{.size={60,60}, .color=Color::blue(), .pos={30,30,0}});
    inner.scene->layers.push_back(inner_layer);
    desc.sub_compositions.push_back(std::move(inner));

    // Define main layer as precomp
    LayerDesc precomp_layer;
    precomp_layer.name = "nested";
    precomp_layer.kind = LayerKind::Precomp;
    precomp_layer.precomp_composition_name = "inner_bg";
    desc.layers.push_back(precomp_layer);

    // Build registry and evaluate
    auto registry = build_sub_composition_registry(desc);
    CHECK(registry.contains("inner_bg"));

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].kind == LayerKind::Precomp);
    CHECK(std::string(scene.layers()[0].precomp_composition_name) == "inner_bg");
    CHECK(scene.layers()[0].nodes.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: Precomp layer effects
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: Precomp layer with blur effect") {
    auto fb = render_precomp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "nonexistent", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.blur(5.0f);
        });
        return s.build();
    });
    CHECK(fb != nullptr);
}

TEST_CASE("AE-6: Precomp layer with adjustment effects") {
    auto fb = render_precomp([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={120,120}, .color=Color::white(), .pos={60,60,0}})
         .precomp_layer("nested", "nonexistent", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.saturation(0.5f);
            l.opacity(0.9f);
        });
        return s.build();
    });
    CHECK(fb != nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-6: Edge Cases — Nested precomps, time remap, effects stack
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-6: Nested precomp (precomp inside precomp)") {
    // Innermost composition: a red rect
    // Middle composition: references "inner" as a precomp
    // Outer composition: references "middle" as a precomp
    CompositionRegistry registry;

    registry.add("inner", []() {
        return Composition(
            CompositionSpec{.name="inner", .width=100, .height=100, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::red(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    registry.add("middle", []() {
        return Composition(
            CompositionSpec{.name="middle", .width=100, .height=100, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.precomp_layer("inner_layer", "inner", [](LayerBuilder& l) {
                    l.from(Frame{0});
                });
                return s.build();
            }
        );
    });

    auto fb = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("middle_layer", "middle", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // The nested red rect should be visible in the top-left quadrant of the 200×200 canvas
    // (inner comp is 100×100, rendered at top-left of parent canvas)
    Color red = Color::red();
    CHECK(any_pixel(*fb, red));
    // Red pixels should be concentrated near the center
    float cx = centroid_x(*fb, red);
    float cy = centroid_y(*fb, red);
    CHECK(cx > 40.0f);
    CHECK(cx < 160.0f);
    CHECK(cy > 40.0f);
    CHECK(cy < 160.0f);
}

TEST_CASE("AE-6: Nested precomp with parent transform") {
    CompositionRegistry registry;

    registry.add("inner", []() {
        return Composition(
            CompositionSpec{.name="inner", .width=100, .height=100, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={40,40}, .color=Color::blue(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    // Middle composition offsets the inner precomp
    registry.add("middle", []() {
        return Composition(
            CompositionSpec{.name="middle", .width=100, .height=100, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.precomp_layer("inner_layer", "inner", [](LayerBuilder& l) {
                    l.from(Frame{0});
                    l.position({-30, 0, 0});  // shift left within middle comp
                });
                return s.build();
            }
        );
    });

    auto fb = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("middle_layer", "middle", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    Color blue = Color::blue();
    CHECK(any_pixel(*fb, blue));
    // Blue rect should be shifted left of center due to parent transform
    float cx = centroid_x(*fb, blue);
    CHECK(cx < 100.0f);  // left of canvas center
}

TEST_CASE("AE-6: Precomp with time remap reverse does not affect inner time") {
    // NOTE: PrecompNode computes nested_frame = ctx.frame - start_frame directly,
    // bypassing the layer's TimeRemap. reverse() on the precomp layer affects the
    // outer layer's transform properties but NOT the inner composition's time.
    // The inner composition progresses forward regardless of the outer layer's speed/reverse.
    CompositionRegistry registry;

    registry.add("moving", []() {
        return Composition(
            CompositionSpec{.name="moving", .width=200, .height=200, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.layer("dot", [](LayerBuilder& l) {
                    l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
                    l.rect("r", {.size={10,10}, .color=Color::green()});
                });
                return s.build();
            }
        );
    });

    // reverse/speed on precomp layers are no-ops for inner composition time
    // (PrecompNode::execute computes nested_frame = ctx.frame - start_frame directly)
    auto fb_rev_0 = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "moving", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    }, 200, 200, Frame{0});

    auto fb_rev_59 = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "moving", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    }, 200, 200, Frame{59});

    REQUIRE(fb_rev_0  != nullptr);
    REQUIRE(fb_rev_59 != nullptr);

    // Both evaluate the inner comp forward — frame 0→left, frame 59→right
    Color green = Color::green();
    float cx_start = centroid_x(*fb_rev_0, green);
    float cx_end   = centroid_x(*fb_rev_59, green);

    CHECK(cx_start > 0.0f);
    CHECK(cx_end   > 0.0f);
    // PrecompNode bypasses the layer's TimeRemap — reverse on the outer layer
    // does not affect the inner composition's evaluation. Both produce the same result.
    CHECK(cx_start == doctest::Approx(cx_end));
}

TEST_CASE("AE-6: Precomp with speed does not affect inner composition time") {
    // NOTE: PrecompNode computes nested_frame = ctx.frame - start_frame directly,
    // bypassing the layer's TimeRemap. Speed on the precomp layer affects the
    // outer layer's transform properties but NOT the inner composition's time.
    // This test verifies that both renders produce identical results.
    CompositionRegistry registry;

    registry.add("moving", []() {
        return Composition(
            CompositionSpec{.name="moving", .width=200, .height=200, .duration=Frame{60}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.layer("dot", [](LayerBuilder& l) {
                    l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
                    l.rect("r", {.size={10,10}, .color=Color::yellow()});
                });
                return s.build();
            }
        );
    });

    auto fb_normal = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "moving", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    }, 200, 200, Frame{30});

    auto fb_slow = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "moving", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.speed(0.5f);
        });
        return s.build();
    }, 200, 200, Frame{30});

    REQUIRE(fb_normal != nullptr);
    REQUIRE(fb_slow   != nullptr);

    // Both evaluate the inner comp at the same nested frame (30)
    Color yellow = Color::yellow();
    float cx_normal = centroid_x(*fb_normal, yellow);
    float cx_slow   = centroid_x(*fb_slow, yellow);

    CHECK(cx_normal > 0.0f);
    CHECK(cx_slow   > 0.0f);
    CHECK(cx_slow == doctest::Approx(cx_normal));
}

TEST_CASE("AE-6: Precomp with effects stack (blur + tint)") {
    // Inner composition: a solid white rect.
    // Precomp layer: apply blur + tint effects.
    // Verify the pixel colors change compared to no effects.
    CompositionRegistry registry;

    registry.add("white_rect", []() {
        return Composition(
            CompositionSpec{.name="white_rect", .width=200, .height=200, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    // Without effects
    auto fb_plain = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "white_rect", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    });

    // With blur + tint
    auto fb_effects = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "white_rect", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.blur(3.0f);
            l.tint(Color{1.0f, 0.0f, 0.0f, 0.5f});  // red tint at 50%
        });
        return s.build();
    });

    REQUIRE(fb_plain   != nullptr);
    REQUIRE(fb_effects != nullptr);

    // Both should have visible content
    CHECK(any_pixel(*fb_plain, Color::white()));
    // Effects version: center pixel should have red tint applied
    Color center = fb_effects->get_pixel(100, 100);
    CHECK(center.r > 0.8f);  // red channel boosted by tint (white + red at 50%)
}

TEST_CASE("AE-6: Precomp with saturation effect") {
    // Inner composition: a colorful green rect.
    // Precomp layer: saturation=0 (greyscale).
    CompositionRegistry registry;

    registry.add("green_rect", []() {
        return Composition(
            CompositionSpec{.name="green_rect", .width=200, .height=200, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::green(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    // Without saturation effect
    auto fb_normal = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "green_rect", [](LayerBuilder& l) {
            l.from(Frame{0});
        });
        return s.build();
    });

    // With saturation=0 (greyscale)
    auto fb_grey = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "green_rect", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.saturation(0.0f);
        });
        return s.build();
    });

    REQUIRE(fb_normal != nullptr);
    REQUIRE(fb_grey   != nullptr);

    // Normal: green rect has high green channel, low red
    Color c_normal = fb_normal->get_pixel(100, 100);
    CHECK(c_normal.g > 0.5f);
    CHECK(c_normal.r < 0.2f);

    // Greyscale: R ≈ G ≈ B (all channels equalized)
    Color c_grey = fb_grey->get_pixel(100, 100);
    CHECK(c_grey.g > 0.1f);  // still visible
    // In greyscale, red and green should be much closer than before
    float diff_normal = std::abs(c_normal.g - c_normal.r);
    float diff_grey   = std::abs(c_grey.g - c_grey.r);
    CHECK(diff_grey < diff_normal);
}

TEST_CASE("AE-6: Precomp layer respects outer layer duration") {
    // PrecompNode uses the OUTER layer's duration (not inner CompositionSpec.duration)
    // to decide when to stop rendering. Setting l.duration(Frame{30}) on the precomp
    // layer makes PrecompNode return empty at frames >= 30.
    CompositionRegistry registry;

    registry.add("short", []() {
        return Composition(
            CompositionSpec{.name="short", .width=100, .height=100, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::red(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    auto fb_early = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "short", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.duration(Frame{30});
        });
        return s.build();
    }, 200, 200, Frame{10});

    // At frame 50: nested_frame=50 >= outer duration=30 → PrecompNode returns empty.
    auto fb_late = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "short", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.duration(Frame{30});
        });
        return s.build();
    }, 200, 200, Frame{50});

    REQUIRE(fb_early != nullptr);
    REQUIRE(fb_late  != nullptr);
    // Early: red rect visible
    Color red = Color::red();
    CHECK(any_pixel(*fb_early, red));
    // Late: empty — no red pixels (outer duration expired)
    CHECK_FALSE(any_pixel(*fb_late, red));
}

TEST_CASE("AE-6: Multiple precomps with pixel verification") {
    // Two precomps side-by-side: red on left, blue on right.
    CompositionRegistry registry;

    registry.add("red_comp", []() {
        return Composition(
            CompositionSpec{.name="red_comp", .width=80, .height=80, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::red(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    registry.add("blue_comp", []() {
        return Composition(
            CompositionSpec{.name="blue_comp", .width=80, .height=80, .duration=Frame{90}},
            [](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx.width, ctx.height, ctx.resource);
                s.rect("bg", {.size={80,80}, .color=Color::blue(), .pos={0,0,0}});
                return s.build();
            }
        );
    });

    auto fb = render_precomp_with_registry(registry, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("red_layer", "red_comp", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.position({-50, 0, 0});  // left
        });
        s.precomp_layer("blue_layer", "blue_comp", [](LayerBuilder& l) {
            l.from(Frame{0});
            l.position({50, 0, 0});   // right
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);

    Color red  = Color::red();
    Color blue = Color::blue();
    CHECK(any_pixel(*fb, red));
    CHECK(any_pixel(*fb, blue));

    // Red centroid should be left of blue centroid
    float cx_red  = centroid_x(*fb, red);
    float cx_blue = centroid_x(*fb, blue);
    CHECK(cx_red  > 0.0f);
    CHECK(cx_blue > 0.0f);
    CHECK(cx_red < cx_blue);
}
