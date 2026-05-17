#include <doctest/doctest.h>

#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("Cache Key Hashing: Text Shape properties") {
    Shape s1;
    s1.type = ShapeType::Text;
    s1.text.text = "Hello World";
    s1.text.style.font_path = "assets/fonts/Inter-Bold.ttf";
    s1.text.style.size = 24.0f;
    s1.text.style.color = Color{1, 0, 0, 1};
    s1.text.style.align = TextAlign::Left;
    s1.text.style.line_height = 1.2f;
    s1.text.style.tracking = 1.0f;
    s1.text.style.max_lines = 2;
    s1.text.style.auto_scale = false;
    s1.text.style.min_size = 10.0f;
    s1.text.style.max_size = 100.0f;
    s1.text.box.enabled = true;
    s1.text.box.size = Vec2{200, 50};

    u64 base_hash = hash_shape(s1);

    // Mutate text content
    {
        Shape s2 = s1;
        s2.text.text = "Hello Universe";
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate font path
    {
        Shape s2 = s1;
        s2.text.style.font_path = "assets/fonts/Roboto.ttf";
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate size
    {
        Shape s2 = s1;
        s2.text.style.size = 28.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate color
    {
        Shape s2 = s1;
        s2.text.style.color = Color{0, 1, 0, 1};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate align
    {
        Shape s2 = s1;
        s2.text.style.align = TextAlign::Center;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate line height
    {
        Shape s2 = s1;
        s2.text.style.line_height = 1.5f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate tracking
    {
        Shape s2 = s1;
        s2.text.style.tracking = 2.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate max lines
    {
        Shape s2 = s1;
        s2.text.style.max_lines = 4;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate auto scale
    {
        Shape s2 = s1;
        s2.text.style.auto_scale = true;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate min size
    {
        Shape s2 = s1;
        s2.text.style.min_size = 8.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate max size
    {
        Shape s2 = s1;
        s2.text.style.max_size = 120.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate box enabled
    {
        Shape s2 = s1;
        s2.text.box.enabled = false;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate box size
    {
        Shape s2 = s1;
        s2.text.box.size = Vec2{300, 80};
        CHECK(hash_shape(s2) != base_hash);
    }
}

TEST_CASE("Cache Key Hashing: Image Shape properties") {
    Shape s1;
    s1.type = ShapeType::Image;
    s1.image.path = "assets/images/logo.png";
    s1.image.size = Vec2{128, 128};
    s1.image.opacity = 0.85f;

    u64 base_hash = hash_shape(s1);

    // Mutate path
    {
        Shape s2 = s1;
        s2.image.path = "assets/images/banner.png";
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate size
    {
        Shape s2 = s1;
        s2.image.size = Vec2{256, 128};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate opacity
    {
        Shape s2 = s1;
        s2.image.opacity = 1.0f;
        CHECK(hash_shape(s2) != base_hash);
    }
}

TEST_CASE("Cache Key Hashing: FakeBox3D properties") {
    Shape s1;
    s1.type = ShapeType::FakeBox3D;
    s1.fake_box3d.world_pos = Vec3{10, 20, 30};
    s1.fake_box3d.size = Vec2{100, 150};
    s1.fake_box3d.depth = 40.0f;
    s1.fake_box3d.color = Color{1, 1, 0, 1};
    s1.fake_box3d.top_tint = 0.2f;
    s1.fake_box3d.side_tint = 0.3f;

    u64 base_hash = hash_shape(s1);

    // Mutate world pos
    {
        Shape s2 = s1;
        s2.fake_box3d.world_pos = Vec3{11, 20, 30};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate size
    {
        Shape s2 = s1;
        s2.fake_box3d.size = Vec2{101, 150};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate depth
    {
        Shape s2 = s1;
        s2.fake_box3d.depth = 45.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate color
    {
        Shape s2 = s1;
        s2.fake_box3d.color = Color{1, 0.5f, 0, 1};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate top tint
    {
        Shape s2 = s1;
        s2.fake_box3d.top_tint = 0.25f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate side tint
    {
        Shape s2 = s1;
        s2.fake_box3d.side_tint = 0.35f;
        CHECK(hash_shape(s2) != base_hash);
    }
}

TEST_CASE("Cache Key Hashing: GridPlane properties") {
    Shape s1;
    s1.type = ShapeType::GridPlane;
    s1.grid_plane.world_pos = Vec3{0, 0, 0};
    s1.grid_plane.axis = PlaneAxis::XZ;
    s1.grid_plane.extent = 1000.0f;
    s1.grid_plane.spacing = 100.0f;
    s1.grid_plane.color = Color{0.5f, 0.5f, 0.5f, 0.5f};
    s1.grid_plane.fade_distance = 800.0f;
    s1.grid_plane.fade_min_alpha = 0.1f;

    u64 base_hash = hash_shape(s1);

    // Mutate world pos
    {
        Shape s2 = s1;
        s2.grid_plane.world_pos = Vec3{0, 1, 0};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate axis
    {
        Shape s2 = s1;
        s2.grid_plane.axis = PlaneAxis::XY;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate extent
    {
        Shape s2 = s1;
        s2.grid_plane.extent = 1200.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate spacing
    {
        Shape s2 = s1;
        s2.grid_plane.spacing = 50.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate color
    {
        Shape s2 = s1;
        s2.grid_plane.color = Color{0.6f, 0.6f, 0.6f, 0.5f};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate fade distance
    {
        Shape s2 = s1;
        s2.grid_plane.fade_distance = 900.0f;
        CHECK(hash_shape(s2) != base_hash);
    }
}

TEST_CASE("Cache Key Hashing: FakeExtrudedText properties") {
    Shape s1;
    s1.type = ShapeType::FakeExtrudedText;
    s1.fake_extruded_text.text = "Chronon";
    s1.fake_extruded_text.font_path = "assets/fonts/Inter-Bold.ttf";
    s1.fake_extruded_text.font_size = 72.0f;
    s1.fake_extruded_text.align = TextAlign::Center;
    s1.fake_extruded_text.world_pos = Vec3{0, 50, 10};
    s1.fake_extruded_text.depth = 16;
    s1.fake_extruded_text.extrude_dir = Vec2{0.5f, 0.5f};
    s1.fake_extruded_text.extrude_z_step = 1.0f;
    s1.fake_extruded_text.front_color = Color{0.9f, 0.9f, 0.9f, 1.0f};
    s1.fake_extruded_text.side_color = Color{0.4f, 0.4f, 0.4f, 1.0f};
    s1.fake_extruded_text.side_fade = 0.2f;
    s1.fake_extruded_text.highlight_opacity = 0.1f;
    s1.fake_extruded_text.bevel_size = 1.0f;
    s1.fake_extruded_text.light_dir = Vec3{0, 1, 0};

    u64 base_hash = hash_shape(s1);

    // Mutate text
    {
        Shape s2 = s1;
        s2.fake_extruded_text.text = "Chronon3D";
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate font path
    {
        Shape s2 = s1;
        s2.fake_extruded_text.font_path = "assets/fonts/Inter.ttf";
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate font size
    {
        Shape s2 = s1;
        s2.fake_extruded_text.font_size = 64.0f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate align
    {
        Shape s2 = s1;
        s2.fake_extruded_text.align = TextAlign::Left;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate world pos
    {
        Shape s2 = s1;
        s2.fake_extruded_text.world_pos = Vec3{0, 51, 10};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate depth
    {
        Shape s2 = s1;
        s2.fake_extruded_text.depth = 20;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate extrude dir
    {
        Shape s2 = s1;
        s2.fake_extruded_text.extrude_dir = Vec2{0.6f, 0.5f};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate extrude z step
    {
        Shape s2 = s1;
        s2.fake_extruded_text.extrude_z_step = 1.1f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate front color
    {
        Shape s2 = s1;
        s2.fake_extruded_text.front_color = Color{1.0f, 1.0f, 1.0f, 1.0f};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate side color
    {
        Shape s2 = s1;
        s2.fake_extruded_text.side_color = Color{0.3f, 0.3f, 0.3f, 1.0f};
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate side fade
    {
        Shape s2 = s1;
        s2.fake_extruded_text.side_fade = 0.3f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate highlight opacity
    {
        Shape s2 = s1;
        s2.fake_extruded_text.highlight_opacity = 0.15f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate bevel size
    {
        Shape s2 = s1;
        s2.fake_extruded_text.bevel_size = 1.2f;
        CHECK(hash_shape(s2) != base_hash);
    }

    // Mutate light dir
    {
        Shape s2 = s1;
        s2.fake_extruded_text.light_dir = Vec3{1, 0, 0};
        CHECK(hash_shape(s2) != base_hash);
    }
}

TEST_CASE("Cache Key Hashing: BloomParams") {
    EffectParams p1 = BloomParams{.threshold = 0.8f, .radius = 10.0f, .intensity = 1.5f};
    u64 base_hash = hash_effect_params(p1);

    // Mutate threshold
    {
        EffectParams p2 = BloomParams{.threshold = 0.9f, .radius = 10.0f, .intensity = 1.5f};
        CHECK(hash_effect_params(p2) != base_hash);
    }

    // Mutate radius
    {
        EffectParams p2 = BloomParams{.threshold = 0.8f, .radius = 12.0f, .intensity = 1.5f};
        CHECK(hash_effect_params(p2) != base_hash);
    }

    // Mutate intensity
    {
        EffectParams p2 = BloomParams{.threshold = 0.8f, .radius = 10.0f, .intensity = 2.0f};
        CHECK(hash_effect_params(p2) != base_hash);
    }
}
