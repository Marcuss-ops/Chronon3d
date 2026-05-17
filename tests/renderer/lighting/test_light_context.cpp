#include <doctest/doctest.h>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <glm/glm.hpp>

using namespace chronon3d;
using namespace chronon3d::rendering;

TEST_CASE("LightContext: default_scene matches legacy FakeBox3D constants") {
    auto light = LightContext::default_scene();
    CHECK(light.ambient  == doctest::Approx(0.20f));
    CHECK(light.diffuse  == doctest::Approx(0.80f));
}

TEST_CASE("LightContext: shade_ndotl on light-facing normal is > ambient") {
    auto light = LightContext::default_scene();
    const f32 r = light.shade_ndotl(light.direction);
    CHECK(r > light.ambient);
    CHECK(r == doctest::Approx(light.ambient + light.diffuse).epsilon(0.01f));
}

TEST_CASE("LightContext: shade_ndotl on back-facing normal equals ambient") {
    auto light = LightContext::default_scene();
    CHECK(light.shade_ndotl(-light.direction) == doctest::Approx(light.ambient).epsilon(0.01f));
}

TEST_CASE("LightContext: shade_ndotl result always in [ambient, ambient+diffuse]") {
    auto light = LightContext::default_scene();
    const Vec3 normals[] = {
        {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}
    };
    for (const auto& n : normals) {
        const f32 r = light.shade_ndotl(n);
        CHECK(r >= light.ambient - 0.001f);
        CHECK(r <= light.ambient + light.diffuse + 0.001f);
    }
}

TEST_CASE("Material2_5D: accepts_lights=false → shade returns 1") {
    Material2_5D mat;
    mat.accepts_lights = false;
    auto light = LightContext::default_scene();
    CHECK(light.shade({0, 0, 1}, mat) == doctest::Approx(1.0f));
}

TEST_CASE("Material2_5D: accepts_lights=true → shade < 1 for back-facing") {
    Material2_5D mat;
    mat.accepts_lights = true;
    auto light = LightContext::default_scene();
    const f32 r = light.shade(-light.direction, mat);
    CHECK(r < 1.0f);
    CHECK(r >= 0.0f);
}

TEST_CASE("Lighting eval: disabled light leaves base color unchanged") {
    LightContext light;
    Material2_5D mat;
    mat.accepts_lights = true;

    const Color base{0.25f, 0.5f, 0.75f, 1.0f};
    const Color out = evaluate_lighting(base, {0, 0, 1}, light, mat);

    CHECK(out.r == doctest::Approx(base.r));
    CHECK(out.g == doctest::Approx(base.g));
    CHECK(out.b == doctest::Approx(base.b));
    CHECK(out.a == doctest::Approx(base.a));
}

TEST_CASE("Lighting eval: directional facing light is brighter than back-facing") {
    LightContext light = LightContext::default_scene();
    Material2_5D mat;
    mat.accepts_lights = true;

    const Color base{1.0f, 1.0f, 1.0f, 1.0f};
    const auto facing = evaluate_lighting(base, light.direction, light, mat);
    const auto away = evaluate_lighting(base, -light.direction, light, mat);

    CHECK(facing.r >= away.r);
    CHECK(facing.g >= away.g);
    CHECK(facing.b >= away.b);
}
