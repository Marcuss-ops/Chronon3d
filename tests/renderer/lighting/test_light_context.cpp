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

namespace {
PointLightSource make_point_source(PointLightKind kind, Vec3 position) {
    PointLightSource s;
    s.kind = kind;
    s.position = position;
    s.direction = Vec3{0.0f, 0.0f, -1.0f};
    s.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    s.radius = 100.0f;
    s.range = 0.0f;
    s.decay = 1.0f;
    s.angle = 0.785398f; // pi/4
    s.penumbra = 0.0f;
    return s;
}

LightContext only_point_lights(PointLightSource source) {
    LightContext light;
    light.enabled = false;
    light.point_lights.push_back(source);
    return light;
}
} // namespace

TEST_CASE("Point light: closed-form irradiance directly below the light") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    // Distance 100 with radius 100 and decay 1 gives attenuation 1/(1+1) = 0.5.
    const auto light = only_point_lights(make_point_source(PointLightKind::Point, Vec3{0.0f, 0.0f, 100.0f}));
    const Color out = evaluate_lighting_at(Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 0.0f}, light, mat);
    CHECK(out.r == doctest::Approx(0.5f).epsilon(0.001f));
    CHECK(out.g == doctest::Approx(0.5f).epsilon(0.001f));
    CHECK(out.a == doctest::Approx(1.0f));
}

TEST_CASE("Point light: range cutoff returns zero beyond range") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    auto source = make_point_source(PointLightKind::Point, Vec3{0.0f, 0.0f, 100.0f});
    source.range = 50.0f;
    const auto light = only_point_lights(source);
    const Color out = evaluate_lighting_at(Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 0.0f}, light, mat);
    CHECK(out.r == doctest::Approx(0.0f));
}

TEST_CASE("Spot light: pixel outside the cone receives no light") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    // Spot points straight down (-z) with a 45 degree half-angle; the pixel
    // sits far off-axis, well outside the cone.
    const auto light = only_point_lights(make_point_source(PointLightKind::Spot, Vec3{0.0f, 0.0f, 100.0f}));
    const Color out = evaluate_lighting_at(Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           Vec3{0.0f, 0.0f, 1.0f}, Vec3{300.0f, 0.0f, 0.0f}, light, mat);
    CHECK(out.r == doctest::Approx(0.0f));
}

TEST_CASE("Spot light: pixel on the axis is lit") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    const auto light = only_point_lights(make_point_source(PointLightKind::Spot, Vec3{0.0f, 0.0f, 100.0f}));
    const Color out = evaluate_lighting_at(Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 0.0f}, light, mat);
    CHECK(out.r == doctest::Approx(0.5f).epsilon(0.001f));
}

TEST_CASE("Directional light: facing normal gets full diffuse irradiance") {
    Material2_5D mat;
    mat.accepts_lights = true;
    mat.diffuse = 1.0f;
    auto source = make_point_source(PointLightKind::Directional, Vec3{0.0f, 0.0f, 0.0f});
    source.direction = Vec3{0.0f, 0.0f, -1.0f};
    const auto light = only_point_lights(source);
    const Color out = evaluate_lighting_at(Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           Vec3{0.0f, 0.0f, 1.0f}, Vec3{10.0f, 20.0f, 0.0f}, light, mat);
    CHECK(out.r == doctest::Approx(1.0f));
}

TEST_CASE("Point light: without point lights evaluate_lighting_at matches evaluate_lighting") {
    LightContext light = LightContext::default_scene();
    Material2_5D mat;
    mat.accepts_lights = true;
    const Color base{0.4f, 0.6f, 0.8f, 1.0f};
    const Vec3 normal{0.0f, 0.0f, 1.0f};
    const Color a = evaluate_lighting(base, normal, light, mat);
    const Color b = evaluate_lighting_at(base, normal, Vec3{5.0f, 5.0f, 0.0f}, light, mat);
    CHECK(b.r == doctest::Approx(a.r));
    CHECK(b.g == doctest::Approx(a.g));
    CHECK(b.b == doctest::Approx(a.b));
}
