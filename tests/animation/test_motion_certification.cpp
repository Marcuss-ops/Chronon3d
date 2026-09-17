#include <doctest/doctest.h>

#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <cmath>

namespace {

using namespace chronon3d;

AnimatedTransform youtube_transform() {
    AnimatedTransform transform;
    transform.position
        .key(Frame{0}, Vec3{200.0f, 300.0f, 0.0f})
        .key(Frame{60}, Vec3{1000.0f, 300.0f, 240.0f});
    transform.scale
        .key(Frame{0}, Vec3{0.70f, 0.70f, 1.0f})
        .key(Frame{60}, Vec3{1.00f, 1.00f, 1.0f});
    transform.anchor.set(Vec3{100.0f, 50.0f, 0.0f});
    transform.opacity
        .key(Frame{0}, 0.0f)
        .key(Frame{30}, 1.0f)
        .key(Frame{60}, 0.0f);
    transform.rotation_quat_track
        .key(Frame{0}, quat_from_euler_degrees(Vec3{0.0f, -30.0f, 0.0f}))
        .key(Frame{60}, quat_from_euler_degrees(Vec3{0.0f, 30.0f, 0.0f}));
    return transform;
}

void check_matrix_equal(const Mat4& actual, const Mat4& expected) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(actual[column][row] ==
                  doctest::Approx(expected[column][row]).epsilon(1e-5));
        }
    }
}

} // namespace

TEST_CASE("MotionCertification: text and image share 2.5D transform semantics") {
    Layer text;
    text.name = "title";
    text.kind = LayerKind::Text;
    text.uses_2_5d_projection = true;
    text.anim_transform = youtube_transform();

    Layer image;
    image.name = "product";
    image.kind = LayerKind::Normal;
    image.uses_2_5d_projection = true;
    image.anim_transform = youtube_transform();

    CHECK(text.is_text());
    CHECK(text.content().draws());
    CHECK(image.carries_drawable_content());
    CHECK(text.uses_2_5d_projection);
    CHECK(image.uses_2_5d_projection);

    const auto time = SampleTime::from_frame(30.0, FrameRate{30, 1});
    const Transform text_transform = text.anim_transform.evaluate(time);
    const Transform image_transform = image.anim_transform.evaluate(time);

    CHECK(text_transform.position == image_transform.position);
    CHECK(text_transform.scale == image_transform.scale);
    CHECK(text_transform.anchor == image_transform.anchor);
    CHECK(text_transform.opacity == doctest::Approx(1.0f));
    CHECK(image_transform.opacity == doctest::Approx(1.0f));
    check_matrix_equal(text_transform.to_mat4(), image_transform.to_mat4());
}

TEST_CASE("MotionCertification: canonical YouTube frames evaluate text and image identically") {
    const AnimatedTransform text = youtube_transform();
    const AnimatedTransform image = youtube_transform();

    for (const double frame : {0.0, 15.0, 30.0, 45.0, 60.0}) {
        const auto sample = SampleTime::from_frame(frame, FrameRate{30, 1});
        const auto text_value = text.evaluate(sample);
        const auto image_value = image.evaluate(sample);

        CHECK(text_value.position == image_value.position);
        CHECK(text_value.scale == image_value.scale);
        CHECK(text_value.rotation == image_value.rotation);
        CHECK(text_value.opacity == image_value.opacity);

        if (frame == 15.0) {
            CHECK(text_value.position.x == doctest::Approx(400.0f));
            CHECK(text_value.scale.x == doctest::Approx(0.775f));
            CHECK(text_value.opacity == doctest::Approx(0.5f));
        }
        if (frame == 45.0) {
            CHECK(text_value.position.x == doctest::Approx(800.0f));
            CHECK(text_value.scale.x == doctest::Approx(0.925f));
            CHECK(text_value.opacity == doctest::Approx(0.5f));
        }
    }
}

TEST_CASE("MotionCertification: quaternion rotation uses the shortest 3D path") {
    AnimatedTransform transform;
    transform.rotation_quat_track
        .key(Frame{0}, quat_from_euler_degrees(Vec3{0.0f, 170.0f, 0.0f}))
        .key(Frame{60}, quat_from_euler_degrees(Vec3{0.0f, -170.0f, 0.0f}));

    const auto midpoint = transform.evaluate(
        SampleTime::from_frame(30.0, FrameRate{30, 1})).rotation;
    const auto forward = midpoint * Vec3{0.0f, 0.0f, 1.0f};

    // 170° -> -170° must cross the ±180° boundary, not rotate 340° through 0°.
    CHECK(std::abs(forward.z) > 0.98f);
    CHECK(std::abs(forward.x) < 0.18f);
}
