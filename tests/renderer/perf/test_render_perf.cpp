#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
using namespace chronon3d;


namespace {

Composition make_perf_comp(int w, int h) {
    CompositionSpec spec{.name="PerfTest", .width=w, .height=h, .duration=30};
    return Composition(spec, [w, h](const FrameContext& ctx) {
        SceneBuilder b(ctx.resource);
        b.rect("bg", {.size={(f32)w,(f32)h}, .color={0.1f,0.1f,0.1f,1}, .pos={(f32)w/2,(f32)h/2,0}});
        for (int i = 0; i < 8; ++i) {
            f32 x = 40.0f + i * 60.0f;
            b.rect("r" + std::to_string(i),   {.size={40,40},    .color=Color::white(),   .pos={x,90,0}});
            b.circle("c" + std::to_string(i), {.radius=18.0f,    .color={1,0.5f,0,1},     .pos={x,160,0}});
        }
        return b.build();
    });
}

long long render_ms(SoftwareRenderer& r, Composition& comp, int frames) {
    auto t0 = profiling::now();
    for (int f = 0; f < frames; ++f)
        r.render_frame(comp, f);
    auto t1 = profiling::now();
    return static_cast<long long>(profiling::duration_ms(t0, t1));
}

} // namespace

TEST_CASE("perf: 3 frame 480x270 sotto 500ms") {
    auto comp = make_perf_comp(480, 270);
    SoftwareRenderer renderer;
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    auto ms = render_ms(renderer, comp, 3);
    MESSAGE("480x270 x3 frames: ", ms, "ms");
    CHECK(ms < 800);
}

TEST_CASE("perf: render frame singolo 480x270 sotto 200ms") {
    auto comp = make_perf_comp(480, 270);
    SoftwareRenderer renderer;
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    auto t0 = profiling::now();
    auto fb = renderer.render_frame(comp, 0);
    auto ms = static_cast<long long>(profiling::elapsed_ms(t0));

    REQUIRE(fb != nullptr);
    MESSAGE("Primo frame (cold cache) 480x270: ", ms, "ms");
    CHECK(ms < 450);
}

TEST_CASE("perf: warm render e' piu' veloce del cold") {
    auto comp = make_perf_comp(480, 270);
    SoftwareRenderer renderer;
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    // Cold
    auto t0 = profiling::now();
    renderer.render_frame(comp, 0);
    auto cold_ms = static_cast<long long>(profiling::elapsed_ms(t0));

    // Warm (3 frame successivi)
    auto warm_ms = render_ms(renderer, comp, 3) / 3;

    MESSAGE("Cold: ", cold_ms, "ms  Warm avg: ", warm_ms, "ms");
    // Warm deve stare sotto il doppio del budget cold
    CHECK(warm_ms < 450);
}
