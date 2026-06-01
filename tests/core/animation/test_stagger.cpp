#include <doctest/doctest.h>
#include <chronon3d/animation/stagger.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/presets/motion_object.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

TEST_CASE("compute_stagger_delay basic linear") {
    StaggerConfig config{.delay_per_unit = Frame{4}, .easing = EasingCurve{Easing::Linear}};
    CHECK(compute_stagger_delay(0, 5, config) == Frame{0});
    CHECK(compute_stagger_delay(1, 5, config) == Frame{4});
    CHECK(compute_stagger_delay(4, 5, config) == Frame{16});
}

TEST_CASE("compute_stagger_delay with OutCubic easing") {
    StaggerConfig config{.delay_per_unit = Frame{4}, .easing = EasingCurve{Easing::OutCubic}};
    // OutCubic pushes delays earlier (eased_t > t for small t)
    Frame d1 = compute_stagger_delay(1, 5, config);
    Frame d2 = compute_stagger_delay(2, 5, config);
    CHECK(d1 >= Frame{0});
    CHECK(d2 >= d1);
}

TEST_CASE("compute_stagger_delay single element") {
    StaggerConfig config{.delay_per_unit = Frame{10}};
    CHECK(compute_stagger_delay(0, 1, config) == Frame{0});
}

TEST_CASE("stagger_layers LeftToRight ordering") {
    std::pmr::vector<Layer> layers;
    layers.reserve(3);

    Layer l1;
    l1.name = "left";
    l1.transform.position = Vec3{-100.0f, 0.0f, 0.0f};
    l1.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l1));

    Layer l2;
    l2.name = "center";
    l2.transform.position = Vec3{0.0f, 0.0f, 0.0f};
    l2.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l2));

    Layer l3;
    l3.name = "right";
    l3.transform.position = Vec3{100.0f, 0.0f, 0.0f};
    l3.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l3));

    StaggerConfig config{.delay_per_unit = Frame{8}, .easing = EasingCurve{Easing::Linear}};
    stagger_layers(layers, config, StaggerOrder::LeftToRight);

    // left layer should have rank 0 → no delay
    CHECK(layers[0].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    // center layer should have rank 1 → delay 8
    CHECK(layers[1].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    CHECK(layers[1].anim_transform.position.evaluate(Frame{8}).x == doctest::Approx(0.0f));
    // right layer should have rank 2 → delay 16
    CHECK(layers[2].anim_transform.position.evaluate(Frame{16}).x == doctest::Approx(0.0f));
}

TEST_CASE("stagger_layers RightToLeft ordering") {
    std::pmr::vector<Layer> layers;
    layers.reserve(2);

    Layer l1;
    l1.name = "left";
    l1.transform.position = Vec3{-50.0f, 0.0f, 0.0f};
    l1.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l1));

    Layer l2;
    l2.name = "right";
    l2.transform.position = Vec3{50.0f, 0.0f, 0.0f};
    l2.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l2));

    StaggerConfig config{.delay_per_unit = Frame{10}, .easing = EasingCurve{Easing::Linear}};
    stagger_layers(layers, config, StaggerOrder::RightToLeft);

    // Right layer (index 1) should be rank 0 → no shift
    CHECK(layers[1].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    // Left layer (index 0) should be rank 1 → delay 10
    CHECK(layers[0].anim_transform.position.evaluate(Frame{10}).x == doctest::Approx(0.0f));
}

TEST_CASE("stagger_layers DepthNearToFar ordering") {
    std::pmr::vector<Layer> layers;
    layers.reserve(2);

    Layer l1;
    l1.name = "near";
    l1.transform.position = Vec3{0.0f, 0.0f, 10.0f};
    l1.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l1));

    Layer l2;
    l2.name = "far";
    l2.transform.position = Vec3{0.0f, 0.0f, 100.0f};
    l2.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l2));

    StaggerConfig config{.delay_per_unit = Frame{6}, .easing = EasingCurve{Easing::Linear}};
    stagger_layers(layers, config, StaggerOrder::DepthNearToFar);

    // Near layer (index 0) should be rank 0 → no shift
    CHECK(layers[0].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    // Far layer (index 1) should be rank 1 → delay 6
    CHECK(layers[1].anim_transform.position.evaluate(Frame{6}).x == doctest::Approx(0.0f));
}

TEST_CASE("stagger_named_layers filters by name") {
    std::pmr::vector<Layer> layers;
    layers.reserve(3);

    Layer l1;
    l1.name = "a";
    l1.transform.position = Vec3{0.0f, 0.0f, 0.0f};
    l1.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l1));

    Layer l2;
    l2.name = "b";
    l2.transform.position = Vec3{10.0f, 0.0f, 0.0f};
    l2.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l2));

    Layer l3;
    l3.name = "c";
    l3.transform.position = Vec3{20.0f, 0.0f, 0.0f};
    l3.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l3));

    StaggerConfig config{.delay_per_unit = Frame{5}, .easing = EasingCurve{Easing::Linear}};
    stagger_named_layers(layers, {"a", "c"}, config, StaggerOrder::LeftToRight);

    // a should be rank 0 → no shift
    CHECK(layers[0].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    // b was not selected → no shift
    CHECK(layers[1].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    // c should be rank 1 → delay 5
    CHECK(layers[2].anim_transform.position.evaluate(Frame{5}).x == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedTransform::shift offsets keyframes") {
    AnimatedTransform anim;
    anim.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    anim.position.key(Frame{30}, Vec3{100.0f, 0.0f, 0.0f});
    anim.opacity.key(Frame{0}, 0.0f);
    anim.opacity.key(Frame{30}, 1.0f);

    anim.shift(Frame{10});

    CHECK(anim.position.evaluate(Frame{10}).x == doctest::Approx(0.0f));
    CHECK(anim.position.evaluate(Frame{40}).x == doctest::Approx(100.0f));
    CHECK(anim.opacity.evaluate(Frame{10}) == doctest::Approx(0.0f));
    CHECK(anim.opacity.evaluate(Frame{40}) == doctest::Approx(1.0f));
}

TEST_CASE("MotionObject::shift offsets animations and time window") {
    MotionObject obj = MotionObject::rect("test")
        .at({0.0f, 0.0f, 0.0f})
        .time(Frame{0}, Frame{60});

    obj.animate(MotionAnimation(Frame{0}, Frame{30}, EasingCurve{Easing::Linear},
        [](MotionState& st, f32 progress) { st.position.x = progress * 100.0f; }));

    obj.shift(Frame{12});

    CHECK(obj.time_value.start == Frame{12});
    CHECK(obj.time_value.end == Frame{72});

    // Animation should only start after frame 12
    CHECK(obj.get_animations()[0].start() == Frame{12});
    CHECK(obj.get_animations()[0].end() == Frame{42});
}

TEST_CASE("stagger_layers CenterOut ordering") {
    std::pmr::vector<Layer> layers;
    layers.reserve(4);

    Layer l1;
    l1.name = "left";
    l1.transform.position = Vec3{-100.0f, 0.0f, 0.0f};
    l1.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l1));

    Layer l2;
    l2.name = "center";
    l2.transform.position = Vec3{0.0f, 0.0f, 0.0f};
    l2.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l2));

    Layer l3;
    l3.name = "right";
    l3.transform.position = Vec3{100.0f, 0.0f, 0.0f};
    l3.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l3));

    Layer l4;
    l4.name = "far_left";
    l4.transform.position = Vec3{-200.0f, 0.0f, 0.0f};
    l4.anim_transform.position.key(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
    layers.push_back(std::move(l4));

    StaggerConfig config{.delay_per_unit = Frame{4}, .easing = EasingCurve{Easing::Linear}};
    stagger_layers(layers, config, StaggerOrder::CenterOut);

    // Center layer (index 1) should be rank 0 → no delay
    CHECK(layers[1].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f));
    // left (index 0) and right (index 2) are at same distance from center → order between them is unspecified
    // but both must have greater delay than center
    Frame d_left = layers[0].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f) ? Frame{0} : Frame{4};
    Frame d_right = layers[2].anim_transform.position.evaluate(Frame{0}).x == doctest::Approx(0.0f) ? Frame{0} : Frame{4};
    CHECK(d_left >= Frame{0});
    CHECK(d_right >= Frame{0});
    // far_left (index 3) should be rank 3 → delay 12
    CHECK(layers[3].anim_transform.position.evaluate(Frame{12}).x == doctest::Approx(0.0f));
}

TEST_CASE("stagger_layers Random ordering with fixed seed") {
    // Each layer starts with a non-zero default position value, and has two keyframes:
    //   Frame{0}:  {start_x, 0, 0}   (same as default — no shift visible here)
    //   Frame{30}: {start_x + 100, 0, 0}  (the actual animation target)
    // After stagger, the first keyframe shifts to Frame{delay}, so evaluate(Frame{0})
    // returns the default value, and evaluate(Frame{delay}) returns the first keyframe value.
    // We detect the shift by finding the first frame where evaluate != default.

    std::pmr::vector<Layer> layers;
    layers.reserve(10);

    for (int i = 0; i < 10; ++i) {
        Layer l;
        l.name = std::to_string(i);
        l.transform.position = Vec3{static_cast<f32>(100.0f + i), 0.0f, 0.0f};
        l.anim_transform.position = AnimatedValue<Vec3>(Vec3{static_cast<f32>(100.0f + i), 0.0f, 0.0f});
        l.anim_transform.position.key(Frame{0},  Vec3{static_cast<f32>(100.0f + i), 0.0f, 0.0f});
        l.anim_transform.position.key(Frame{30}, Vec3{static_cast<f32>(200.0f + i), 0.0f, 0.0f});
        layers.push_back(std::move(l));
    }

    StaggerConfig config{.delay_per_unit = Frame{5}, .easing = EasingCurve{Easing::Linear}, .random_seed = 42};
    stagger_layers(layers, config, StaggerOrder::Random);

    // Find the first frame where evaluate(f).x differs from the default value
    // (meaning we're at or past the shifted first keyframe)
    auto find_first_non_default = [](const AnimatedValue<Vec3>& anim, f32 default_x) -> Frame {
        for (Frame f{0}; f <= Frame{60}; ++f) {
            if (anim.evaluate(f).x != doctest::Approx(default_x)) return f;
        }
        return Frame{-1};
    };

    std::vector<Frame> start_frames;
    for (size_t i = 0; i < layers.size(); ++i) {
        start_frames.push_back(find_first_non_default(
            layers[i].anim_transform.position,
            100.0f + static_cast<f32>(i)
        ));
    }

    REQUIRE(start_frames.size() == 10);
    for (auto f : start_frames) {
        REQUIRE(f >= Frame{0});
    }

    // With 10 elements and seed 42, the shuffle should produce a non-identity permutation.
    bool all_same = true;
    for (size_t i = 1; i < start_frames.size(); ++i) {
        if (start_frames[i] != start_frames[0]) { all_same = false; break; }
    }
    CHECK(!all_same);

    // Verify that the delays correspond to the correct rank-based spacing.
    // find_first_non_default returns (delay + 1) due to interpolation starting just
    // after the first keyframe. Sorted delay+1 values should be {1,6,11,...,46}.
    std::vector<Frame> expected;
    for (int r = 0; r < 10; ++r) expected.push_back(Frame{r * 5 + 1});
    auto sorted = start_frames;
    std::sort(sorted.begin(), sorted.end());
    CHECK(sorted == expected);
}

