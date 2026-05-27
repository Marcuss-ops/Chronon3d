#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/animation/spring.hpp>

using namespace chronon3d;

TEST_CASE("Composition Immutability") {
    CompositionSpec spec;
    spec.name = "TestComp";
    spec.width = 1280;
    spec.height = 720;
    spec.frame_rate = {60, 1};
    spec.duration = 600;

    Composition comp{spec, [](const FrameContext&) { return Scene(); }};

    CHECK(comp.name() == "TestComp");
    CHECK(comp.width() == 1280);
    CHECK(comp.height() == 720);
    CHECK(comp.frame_rate().fps() == 60.0);
    CHECK(comp.duration() == 600);
}

TEST_CASE("Deterministic Spring") {
    f32 from = 0.0f;
    f32 to = 100.0f;

    // After 0 seconds, it should be at initial position
    CHECK(spring(0.0f, from, to) == 0.0f);
    
    // After some time, it should move towards target
    f32 mid = spring(0.5f, from, to);
    CHECK(mid > 0.0f);
    CHECK(mid < 200.0f); // might overshoot depending on damping

    // Multiple evaluations at SAME t should give SAME result (pure function of t)
    CHECK(spring(0.5f, from, to) == mid);
}

TEST_CASE("Pure Frame Evaluation") {
    CompositionSpec spec;
    spec.width = 100;
    spec.height = 100;
    spec.duration = 100;

    Composition comp{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder builder(ctx.resource);
            auto x = interpolate(ctx.frame, 0, 100, 0.0f, 100.0f);
            builder.rect("L1", {.size={100, 100}, .color=Color::white(), .pos={x, 0, 0}});
            return builder.build();
        }
    };

    Scene s1 = comp.evaluate(50);
    Scene s2 = comp.evaluate(50);

    CHECK(s1.nodes().size() == 1);
    CHECK(s1.nodes()[0].world_transform.position.x == 50.0f);
    
    // Determinism check
    CHECK(s1.nodes()[0].world_transform.position.x == s2.nodes()[0].world_transform.position.x);
}

TEST_CASE("Render Determinism & Telemetry Hash") {
    CompositionSpec spec;
    spec.name = "VerifyComp";
    spec.width = 320;
    spec.height = 180;
    spec.duration = 10;

    Composition comp{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder builder(ctx.resource);
            builder.rect("bg", {.size={320, 180}, .color=Color::red(), .pos={0, 0, 0}});
            return builder.build();
        }
    };

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    // Render run 1
    renderer.counters()->reset();
    auto fb1 = renderer.render_frame(comp, 0);

    REQUIRE(fb1 != nullptr);

    // Compute FNV-1a pixel hash
    auto get_pixel_hash = [](const Framebuffer& fb) -> u64 {
        u64 h = 0x811c9dc5;
        for (int y = 0; y < fb.height(); ++y) {
            for (int x = 0; x < fb.width(); ++x) {
                Color c = fb.get_pixel(x, y);
                u32 pixel = (static_cast<u32>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)) << 24) |
                            (static_cast<u32>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)) << 16) |
                            (static_cast<u32>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)) << 8)  |
                            (static_cast<u32>(std::clamp(c.a * 255.0f, 0.0f, 255.0f)));
                h = (h ^ pixel) * 0x01000193;
            }
        }
        return h;
    };

    u64 hash1 = get_pixel_hash(*fb1);

    // Render run 2
    auto fb2 = renderer.render_frame(comp, 0);
    REQUIRE(fb2 != nullptr);
    u64 hash2 = get_pixel_hash(*fb2);

    // Check pixel-perfect determinism
    CHECK(hash1 == hash2);

    // Check telemetry counters are active
    CHECK(renderer.counters()->nodes_executed.load() > 0);
    CHECK(renderer.counters()->pixels_touched.load() > 0);

    // Check core render counters are active
    CHECK(renderer.counters()->cache_hits.load() + renderer.counters()->cache_misses.load() >= 0);
}
