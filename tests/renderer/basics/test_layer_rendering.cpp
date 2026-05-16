#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

namespace {

static SoftwareRenderer g_renderer;

static std::unique_ptr<Framebuffer> render_layer(
    i32 w, i32 h, std::function<void(SceneBuilder&)> build)
{
    Composition comp = composition({.name="T",.width=w,.height=h,.duration=1},
                                   [&](const FrameContext& ctx) {
                                       SceneBuilder s(ctx);
                                       build(s);
                                       return s.build();
                                   });
    return g_renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Layer transform translates child nodes") {
    auto fb = render_layer(200, 200, [](SceneBuilder& s) {
        s.layer("layer", [](LayerBuilder& l) {
            l.position({100, 100, 0})
             .rect("box", {.size={40,40}, .color=Color{1,0,0,1}, .pos={}});
        });
    });
    auto p = fb->get_pixel(100, 100);
    CHECK(p.r > 0.5f);
    CHECK(p.g == 0.0f);
    CHECK(p.b == 0.0f);
}

TEST_CASE("Layer opacity affects child nodes") {
    auto fb = render_layer(100, 100, [](SceneBuilder& s) {
        s.layer("layer", [](LayerBuilder& l) {
            l.position({50, 50, 0}).opacity(0.5f)
             .rect("box", {.size={100,100}, .color=Color{1,1,1,1}, .pos={}});
        });
    });
    auto p = fb->get_pixel(50, 50);
    CHECK(p.r == doctest::Approx(0.5f));
    CHECK(p.g == doctest::Approx(0.5f));
    CHECK(p.b == doctest::Approx(0.5f));
}

TEST_CASE("Layer draw order is insertion order") {
    auto fb = render_layer(100, 100, [](SceneBuilder& s) {
        s.layer("red",  [](LayerBuilder& l) {
            l.position({50,50,0}).rect("r",{.size={60,60},.color=Color{1,0,0,1},.pos={}});
        });
        s.layer("blue", [](LayerBuilder& l) {
            l.position({50,50,0}).rect("r",{.size={60,60},.color=Color{0,0,1,1},.pos={}});
        });
    });
    auto p = fb->get_pixel(50, 50);
    CHECK(p.b > 0.5f);
    CHECK(p.r < 0.1f);
}

TEST_CASE("Child draw order inside layer is insertion order") {
    auto fb = render_layer(100, 100, [](SceneBuilder& s) {
        s.layer("group", [](LayerBuilder& l) {
            l.position({50,50,0})
             .rect("red",  {.size={60,60}, .color=Color{1,0,0,1}, .pos={}})
             .rect("green",{.size={40,40}, .color=Color{0,1,0,1}, .pos={}});
        });
    });
    auto p = fb->get_pixel(50, 50);
    CHECK(p.g > 0.5f);
    CHECK(p.r < 0.1f);
}

TEST_CASE("Root nodes render before layers") {
    auto fb = render_layer(100, 100, [](SceneBuilder& s) {
        s.rect("root-red", {.size={80,80}, .color=Color{1,0,0,1}, .pos={50,50,0}});
        s.layer("blue-layer", [](LayerBuilder& l) {
            l.position({50,50,0}).rect("blue",{.size={60,60},.color=Color{0,0,1,1},.pos={}});
        });
    });
    auto p = fb->get_pixel(50, 50);
    CHECK(p.b > 0.5f);
    CHECK(p.r < 0.1f);
}
