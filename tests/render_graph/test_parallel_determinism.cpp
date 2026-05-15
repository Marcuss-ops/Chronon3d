#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <xxhash.h>

using namespace chronon3d;

namespace {

u64 fb_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

Composition make_test_comp() {
    CompositionSpec spec{.name="Det", .width=160, .height=90, .duration=10};
    return Composition(spec, [](const FrameContext& ctx) {
        SceneBuilder b(ctx.resource);
        b.rect("bg",  Vec3{80, 45, 0}, Color{0.2f, 0.3f, 0.8f, 1.0f}, Vec2{160, 90});
        b.rect("box", Vec3{40, 30, 0}, Color::white(), Vec2{30, 20});
        b.circle("dot", Vec3{120, 60, 0}, 15.0f, Color{1,0.5f,0,1});
        return b.build();
    });
}

} // namespace

TEST_CASE("render deterministico: stesso frame produce pixel identici") {
    auto comp = make_test_comp();
    SoftwareRenderer renderer;

    auto fb1 = renderer.render_frame(comp, 0);
    auto fb2 = renderer.render_frame(comp, 0);

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb_hash(*fb1) == fb_hash(*fb2));
}

TEST_CASE("render deterministico: 10 render consecutivi dello stesso frame") {
    auto comp = make_test_comp();
    SoftwareRenderer renderer;

    auto ref = renderer.render_frame(comp, 5);
    REQUIRE(ref != nullptr);
    const u64 ref_hash = fb_hash(*ref);

    for (int i = 0; i < 9; ++i) {
        auto fb = renderer.render_frame(comp, 5);
        REQUIRE(fb != nullptr);
        CHECK(fb_hash(*fb) == ref_hash);
    }
}

TEST_CASE("render deterministico: frame diversi producono output diversi") {
    auto comp = make_test_comp();
    SoftwareRenderer renderer;

    // Frame 0 e frame 5 hanno la stessa scena statica → devono essere uguali
    auto fb0 = renderer.render_frame(comp, 0);
    auto fb5 = renderer.render_frame(comp, 5);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb5 != nullptr);
    // Scena statica: i due frame devono essere identici
    CHECK(fb_hash(*fb0) == fb_hash(*fb5));
}

TEST_CASE("render deterministico: istanze renderer indipendenti") {
    auto comp = make_test_comp();

    SoftwareRenderer r1, r2;
    auto fb1 = r1.render_frame(comp, 3);
    auto fb2 = r2.render_frame(comp, 3);

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb_hash(*fb1) == fb_hash(*fb2));
}
