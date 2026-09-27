#include <doctest/doctest.h>

#include <chronon3d/animation/motion/idle_presets.hpp>

using namespace chronon3d;

TEST_CASE("idle motion presets bake deterministic cyclic transform tracks") {
    const FrameRate fps{30, 1};
    const auto sample = [fps](const AnimatedValue<Vec3>& value, std::int64_t frame) {
        return value.evaluate(SampleTime::from_frame_int(Frame{frame}, fps));
    };

    Layer breathing;
    breathing.transform.scale = Vec3{2.0f, 2.0f, 2.0f};
    motion_presets::breathe(breathing, Frame{10}, Frame{240}, fps, 0.02f);
    CHECK(breathing.anim_transform.scale.loop_mode() == LoopMode::Hold);
    CHECK(sample(breathing.anim_transform.scale, 10).x == doctest::Approx(2.0f));
    CHECK(sample(breathing.anim_transform.scale, 40).x == doctest::Approx(2.04f));
    CHECK(sample(breathing.anim_transform.scale, 160).x == doctest::Approx(2.04f));
    CHECK(sample(breathing.anim_transform.scale, 130).x == doctest::Approx(2.0f));

    Layer bobbing;
    bobbing.transform.position = Vec3{3.0f, 4.0f, 5.0f};
    motion_presets::bob(bobbing, Frame{0}, Frame{150}, fps, 12.0f);
    CHECK(sample(bobbing.anim_transform.position, 19).y == doctest::Approx(16.0f));
    CHECK(sample(bobbing.anim_transform.position, 38).y == doctest::Approx(4.0f));

    Layer pulsing;
    pulsing.transform.scale = Vec3{1.0f, 1.0f, 1.0f};
    motion_presets::pulse(pulsing, Frame{0}, Frame{120}, fps, 0.04f);
    CHECK(sample(pulsing.anim_transform.scale, 30).x == doctest::Approx(1.04f));
    CHECK(sample(pulsing.anim_transform.scale, 15).x == doctest::Approx(1.02f));

    Layer drifting;
    drifting.transform.position = Vec3{5.0f, 7.0f, 0.0f};
    drifting.transform.rotation = glm::quat(glm::radians(Vec3{0.0f, 0.0f, 1.0f}));
    motion_presets::floatDrift(drifting, Frame{0}, Frame{360}, fps, 8.0f, 0.5f);
    const auto drift_quarter = sample(drifting.anim_transform.position, 45);
    CHECK(drift_quarter.x == doctest::Approx(13.0f));
    CHECK(drift_quarter.y == doctest::Approx(7.0f));
    CHECK(sample(drifting.anim_transform.rotation_euler, 45).z == doctest::Approx(1.5f));
    CHECK(sample(drifting.anim_transform.position, 180).x == doctest::Approx(5.0f));
}

TEST_CASE("idle motion presets reject invalid timing or amplitudes") {
    Layer layer;
    CHECK_THROWS_AS(motion_presets::breathe(layer, Frame{0}, Frame{30}, FrameRate{30, 1}, -0.1f),
                    std::invalid_argument);
    CHECK_THROWS_AS(motion_presets::bob(layer, Frame{0}, Frame{0}, FrameRate{30, 1}),
                    std::invalid_argument);
    CHECK_THROWS_AS(motion_presets::pulse(layer, Frame{0}, Frame{30}, FrameRate{0, 1}),
                    std::invalid_argument);
}
