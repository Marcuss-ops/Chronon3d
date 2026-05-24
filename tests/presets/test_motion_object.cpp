#include <doctest/doctest.h>
#include <chronon3d/presets/motion_object.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

TEST_CASE("MotionObject creates text objects with expected defaults") {
    auto obj = MotionObject::text("title", "TEST TITLE");

    CHECK(obj.id == "title");
    CHECK(obj.type == MotionObjectType::Text);
    CHECK(obj.text_value == "TEST TITLE");
    CHECK(obj.size_value.x == doctest::Approx(900.0f));
    CHECK(obj.size_value.y == doctest::Approx(160.0f));
    CHECK(obj.text_style.font_size == doctest::Approx(72.0f));
}

TEST_CASE("MotionObject fluent API updates transform and timing") {
    auto obj = MotionObject::rect("box")
        .at({10.0f, 20.0f, 30.0f})
        .size({400.0f, 120.0f})
        .preset(MotionPreset::SlideUp)
        .time(5, 25)
        .opacity(0.5f);

    CHECK(obj.position_value.x == doctest::Approx(10.0f));
    CHECK(obj.position_value.y == doctest::Approx(20.0f));
    CHECK(obj.position_value.z == doctest::Approx(30.0f));
    CHECK(obj.size_value.x == doctest::Approx(400.0f));
    CHECK(obj.size_value.y == doctest::Approx(120.0f));
    CHECK(obj.preset_value == MotionPreset::SlideUp);
    CHECK(obj.time_value.start == 5);
    CHECK(obj.time_value.end == 25);
    CHECK(obj.opacity_value == doctest::Approx(0.5f));
}

TEST_CASE("MotionObject enables 3D space") {
    auto obj = MotionObject::image("person", "assets/images/person.png")
        .enable_3d(-180.0f)
        .rotate_3d({-8.0f, 12.0f, 0.0f})
        .parallax(1.5f);

    CHECK(obj.motion3d.enabled);
    CHECK(obj.motion3d.z == doctest::Approx(-180.0f));
    CHECK(obj.motion3d.rotation.x == doctest::Approx(-8.0f));
    CHECK(obj.motion3d.rotation.y == doctest::Approx(12.0f));
    CHECK(obj.motion3d.parallax == doctest::Approx(1.5f));
}
