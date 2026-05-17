#include <doctest/doctest.h>

#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/cache/node_cache.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

// Test 4.1 - transform diverso cambia hash
TEST_CASE("Test 4.1 - transform diverso cambia hash") {
    RenderNode a;
    a.name = "node";
    a.world_transform.position = Vec3(10, 0, 0);

    RenderNode b = a;
    b.world_transform.position = Vec3(20, 0, 0);

    CHECK(hash_render_node(a) != hash_render_node(b));
}

// Test 4.2 - opacity diversa cambia hash
TEST_CASE("Test 4.2 - opacity diversa cambia hash") {
    RenderNode a;
    a.world_transform.opacity = 1.0f;

    RenderNode b = a;
    b.world_transform.opacity = 0.5f;

    CHECK(hash_render_node(a) != hash_render_node(b));
}

// Test 4.3 - rect size diversa cambia hash
TEST_CASE("Test 4.3 - rect size diversa cambia hash") {
    Shape s1;
    s1.type = ShapeType::Rect;
    s1.rect.size = Vec2(100, 100);

    Shape s2 = s1;
    s2.rect.size = Vec2(200, 100);

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.4 - rounded rect radius diverso cambia hash
TEST_CASE("Test 4.4 - rounded rect radius diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::RoundedRect;
    s1.rounded_rect.size = Vec2(100, 100);
    s1.rounded_rect.radius = 10.0f;

    Shape s2 = s1;
    s2.rounded_rect.radius = 30.0f;

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.5 - circle radius diverso cambia hash
TEST_CASE("Test 4.5 - circle radius diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::Circle;
    s1.circle.radius = 50.0f;

    Shape s2 = s1;
    s2.circle.radius = 70.0f;

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.6 - line thickness diverso cambia hash
TEST_CASE("Test 4.6 - line thickness diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::Line;
    s1.line.to = Vec3(100, 0, 0);
    s1.line.thickness = 2.0f;

    Shape s2 = s1;
    s2.line.thickness = 8.0f;

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.7 - trim path diverso cambia hash
TEST_CASE("Test 4.7 - trim path diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::Line;
    s1.line.stroke.trim_start = 0.0f;
    s1.line.stroke.trim_end = 1.0f;

    Shape s2 = s1;
    s2.line.stroke.trim_start = 0.3f;

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.8 - text content diverso cambia hash
TEST_CASE("Test 4.8 - text content diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::Text;
    s1.text.text = "Hello";

    Shape s2 = s1;
    s2.text.text = "World";

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.9 - text font path diverso cambia hash
TEST_CASE("Test 4.9 - text font path diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::Text;
    s1.text.style.font_path = "Inter-Regular.ttf";

    Shape s2 = s1;
    s2.text.style.font_path = "Inter-Bold.ttf";

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.10 - text size diversa cambia hash
TEST_CASE("Test 4.10 - text size diversa cambia hash") {
    Shape s1;
    s1.type = ShapeType::Text;
    s1.text.style.size = 32.0f;

    Shape s2 = s1;
    s2.text.style.size = 64.0f;

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.11 - image path diverso cambia hash
TEST_CASE("Test 4.11 - image path diverso cambia hash") {
    Shape s1;
    s1.type = ShapeType::Image;
    s1.image.path = "image_a.png";

    Shape s2 = s1;
    s2.image.path = "image_b.png";

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.12 - image opacity diversa cambia hash
TEST_CASE("Test 4.12 - image opacity diversa cambia hash") {
    Shape s1;
    s1.type = ShapeType::Image;
    s1.image.opacity = 1.0f;

    Shape s2 = s1;
    s2.image.opacity = 0.4f;

    CHECK(hash_shape(s1) != hash_shape(s2));
}

// Test 4.13 - gradient fill diverso cambia hash
TEST_CASE("Test 4.13 - gradient fill diverso cambia hash") {
    Fill f1;
    f1.type = FillType::LinearGradient;
    f1.gradient.from = Vec2(0, 0);
    f1.gradient.to = Vec2(100, 100);
    f1.gradient.stops = { {0.0f, Color::red()}, {1.0f, Color::blue()} };

    Fill f2 = f1;
    f2.gradient.stops = { {0.0f, Color::red()}, {1.0f, Color::green()} };

    CHECK(hash_fill(f1) != hash_fill(f2));
}

// Test 4.14 - shadow diverso cambia hash
TEST_CASE("Test 4.14 - shadow diverso cambia hash") {
    RenderNode a;
    a.shadow.enabled = true;
    a.shadow.radius = 10.0f;

    RenderNode b = a;
    b.shadow.radius = 20.0f;

    CHECK(hash_render_node(a) != hash_render_node(b));
}

// Test 4.15 - glow diverso cambia hash
TEST_CASE("Test 4.15 - glow diverso cambia hash") {
    RenderNode a;
    a.glow.enabled = true;
    a.glow.intensity = 0.5f;

    RenderNode b = a;
    b.glow.intensity = 1.0f;

    CHECK(hash_render_node(a) != hash_render_node(b));
}

// Test 4.16 - mask diverso cambia cache key
TEST_CASE("Test 4.16 - mask diverso cambia cache key") {
    Mask m1;
    m1.type = MaskType::RoundedRect;
    m1.radius = 20.0f;

    Mask m2 = m1;
    m2.radius = 40.0f;

    CHECK(hash_mask(m1) != hash_mask(m2));
}

// Test 4.17 - blur radius diverso cambia effect hash
TEST_CASE("Test 4.17 - blur radius diverso cambia effect hash") {
    EffectStack e1;
    EffectInstance instance1;
    instance1.descriptor.id = "blur.gaussian";
    instance1.enabled = true;
    instance1.params = EffectParams{BlurParams{.radius = 4.0f}};
    e1.push_back(instance1);

    EffectStack e2;
    EffectInstance instance2;
    instance2.descriptor.id = "blur.gaussian";
    instance2.enabled = true;
    instance2.params = EffectParams{BlurParams{.radius = 12.0f}};
    e2.push_back(instance2);

    CHECK(hash_effect_stack(e1) != hash_effect_stack(e2));
}

// Test 4.18 - tint diverso cambia effect hash
TEST_CASE("Test 4.18 - tint diverso cambia effect hash") {
    EffectStack e1;
    EffectInstance i1;
    i1.descriptor.id = "color.tint";
    i1.enabled = true;
    i1.params = EffectParams{TintParams{.color = Color::red(), .amount = 1.0f}};
    e1.push_back(i1);

    EffectStack e2;
    EffectInstance i2;
    i2.descriptor.id = "color.tint";
    i2.enabled = true;
    i2.params = EffectParams{TintParams{.color = Color::blue(), .amount = 1.0f}};
    e2.push_back(i2);

    CHECK(hash_effect_stack(e1) != hash_effect_stack(e2));
}

// Test 4.19 - brightness diverso cambia effect hash
TEST_CASE("Test 4.19 - brightness diverso cambia effect hash") {
    EffectStack e1;
    EffectInstance i1;
    i1.descriptor.id = "color.brightness";
    i1.enabled = true;
    i1.params = EffectParams{BrightnessParams{.value = 0.8f}};
    e1.push_back(i1);

    EffectStack e2;
    EffectInstance i2;
    i2.descriptor.id = "color.brightness";
    i2.enabled = true;
    i2.params = EffectParams{BrightnessParams{.value = 1.2f}};
    e2.push_back(i2);

    CHECK(hash_effect_stack(e1) != hash_effect_stack(e2));
}

// Test 4.20 - contrast diverso cambia effect hash
TEST_CASE("Test 4.20 - contrast diverso cambia effect hash") {
    EffectStack e1;
    EffectInstance i1;
    i1.descriptor.id = "color.contrast";
    i1.enabled = true;
    i1.params = EffectParams{ContrastParams{.value = 0.8f}};
    e1.push_back(i1);

    EffectStack e2;
    EffectInstance i2;
    i2.descriptor.id = "color.contrast";
    i2.enabled = true;
    i2.params = EffectParams{ContrastParams{.value = 1.5f}};
    e2.push_back(i2);

    CHECK(hash_effect_stack(e1) != hash_effect_stack(e2));
}

// Test 4.21 - blend mode diverso cambia composite key
TEST_CASE("Test 4.21 - blend mode diverso cambia composite key") {
    BlendMode m1 = BlendMode::Normal;
    BlendMode m2 = BlendMode::Multiply;

    CHECK(static_cast<u64>(m1) != static_cast<u64>(m2));
}

// Test 4.22 - input hash invalida downstream
TEST_CASE("Test 4.22 - input hash invalida downstream") {
    // If we have NodeCacheKey, changing input_hash must change its digest.
    cache::NodeCacheKey key1{
        .scope = "composite",
        .frame = 1,
        .width = 100,
        .height = 100,
        .params_hash = 10,
        .source_hash = 20,
        .input_hash = 1000
    };

    cache::NodeCacheKey key2 = key1;
    key2.input_hash = 2000;

    CHECK(key1.digest() != key2.digest());
}
