#include <doctest/doctest.h>

#include <chronon3d/render_plan/temporal_rate_analysis.hpp>

#include <vector>

using namespace chronon3d;
using namespace chronon3d::render_plan;

namespace {
RenderPlan five_rates() {
    RenderPlan plan;
    plan.canvas.duration = Frame{12};
    plan.layers.resize(5);
    plan.layers[0].id = "static";
    plan.layers[0].type = LayerType::Image;
    plan.layers[1].id = "cue";
    plan.layers[1].type = LayerType::Text;
    plan.layers[1].start_frame = Frame{3};
    plan.layers[1].duration_frames = Frame{4};
    plan.layers[2].id = "keyframes";
    plan.layers[2].type = LayerType::Shape;
    AnimationTrackPlan sparse;
    sparse.property = "position_x";
    sparse.keyframes = {{Frame{1}, {0.0f}}, {Frame{5}, {5.0f}}, {Frame{10}, {2.0f}}};
    plan.layers[2].animation = AnimationPlan{{sparse}};
    plan.layers[3].id = "per_frame";
    plan.layers[3].type = LayerType::Shape;
    AnimationTrackPlan dense;
    dense.property = "opacity";
    dense.keyframes = {{Frame{0}, {0.0f}}, {Frame{1}, {1.0f}}, {Frame{2}, {0.0f}}};
    plan.layers[3].animation = AnimationPlan{{dense}};
    plan.layers[4].id = "subframe";
    plan.layers[4].type = LayerType::Image;
    plan.layers[4].motion_blur = MotionBlurPlan{180.0f, 4};
    return plan;
}
}

TEST_CASE("Temporal rate analysis classifies all five required rates") {
    const auto report = analyze_temporal_rates(five_rates());
    REQUIRE(report.layers.size() == 5);
    CHECK(report.layers[0].rate == TemporalRateClass::Static);
    CHECK(report.layers[0].eval_frames.empty());
    CHECK(report.layers[1].rate == TemporalRateClass::Cue);
    CHECK(report.layers[1].eval_frames == std::vector<Frame>{
        Frame{3}, Frame{4}, Frame{5}, Frame{6}});
    CHECK(report.layers[2].rate == TemporalRateClass::KeyframeInterval);
    CHECK(report.layers[2].eval_frames ==
          std::vector<Frame>{Frame{1}, Frame{5}, Frame{10}});
    CHECK(report.layers[3].rate == TemporalRateClass::PerFrame);
    CHECK(report.layers[3].eval_frames.size() == 12);
    CHECK(report.layers[3].eval_frames.front() == Frame{0});
    CHECK(report.layers[3].eval_frames.back() == Frame{11});
    CHECK(report.layers[4].rate == TemporalRateClass::Subframe);
    CHECK(report.layers[4].eval_frames.size() == 12);
    CHECK(report.skippable_node_fraction == doctest::Approx(0.4));
}

TEST_CASE("Temporal rate precedence selects per-frame and subframe work") {
    auto plan = five_rates();
    plan.layers[0].type = LayerType::Video;
    plan.layers[0].motion_blur = MotionBlurPlan{180.0f, 1};
    auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::PerFrame);
    plan.temporal_motion_blur = MotionBlurPlan{180.0f, 8};
    report = analyze_temporal_rates(plan);
    for (const auto& layer : report.layers) {
        CHECK(layer.rate == TemporalRateClass::Subframe);
        CHECK(layer.eval_frames.size() == 12);
    }
    CHECK(report.skippable_node_fraction == 0.0);
    plan.temporal_motion_blur = MotionBlurPlan{0.0f, 8};
    CHECK(analyze_temporal_rates(plan).layers[0].rate == TemporalRateClass::PerFrame);
}

TEST_CASE("Temporal cue interval uses clipped half-open bounds") {
    RenderPlan plan;
    plan.canvas.duration = Frame{8};
    LayerPlan cue;
    cue.id = "cue";
    cue.type = LayerType::Text;
    cue.start_frame = Frame{-3};
    cue.duration_frames = Frame{20};
    plan.layers.push_back(cue);
    auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::Cue);
    CHECK(report.layers[0].eval_frames == std::vector<Frame>{
        Frame{0}, Frame{1}, Frame{2}, Frame{3}, Frame{4}, Frame{5}, Frame{6}, Frame{7}});
    plan.layers[0].start_frame = Frame{8};
    report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::Cue);
    CHECK(report.layers[0].eval_frames.empty());
    plan.layers[0].start_frame.reset();
    plan.layers[0].duration_frames.reset();
    report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::Static);
    CHECK(report.skippable_node_fraction == 1.0);
}

TEST_CASE("Temporal rate analysis clips keyed eval frames to layer visibility") {
    auto plan = five_rates();
    plan.layers[2].start_frame = Frame{4};
    plan.layers[2].duration_frames = Frame{5};
    const auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[2].eval_frames == std::vector<Frame>{Frame{5}});
    CHECK(report.layers[2].eval_frame_count == 1);
}

TEST_CASE("Temporal rate analysis bounds large event lists without losing exact counts") {
    RenderPlan plan;
    plan.canvas.duration = Frame{1'000'001};
    LayerPlan video;
    video.id = "video";
    video.type = LayerType::Video;
    plan.layers.push_back(video);
    const auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::PerFrame);
    CHECK(report.layers[0].eval_frames.empty());
    CHECK(report.layers[0].eval_frame_count == 1'000'001);
    CHECK_FALSE(report.layers[0].eval_frames_complete);
}

TEST_CASE("Temporal rate analysis recognizes driven tracks as per-frame") {
    RenderPlan plan;
    plan.canvas.duration = Frame{6};
    LayerPlan layer;
    layer.id = "oscillator";
    layer.type = LayerType::Text;
    AnimationTrackPlan track;
    track.property = "position_x";
    track.oscillate = AnimationTrackPlan::OscillateDriverPlan{};
    layer.animation = AnimationPlan{{track}};
    plan.layers.push_back(layer);
    const auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::PerFrame);
    CHECK(report.layers[0].eval_frames.size() == 6);
}

TEST_CASE("Temporal rate analysis recognizes procedural effect and camera time") {
    RenderPlan plan;
    plan.canvas.duration = Frame{4};
    LayerPlan layer;
    layer.id = "evolving";
    layer.type = LayerType::Shape;
    LayerPlan::EffectPlan effect;
    effect.evolution = 0.25f;
    layer.effects.push_back(effect);
    plan.layers.push_back(layer);
    CHECK(analyze_temporal_rates(plan).layers[0].rate == TemporalRateClass::PerFrame);

    plan.layers[0].effects.clear();
    plan.camera_animation = std::vector<CameraTrackPlan>{
        CameraTrackPlan{CameraPropertyPlan::PositionX,
            {{Frame{0}, {0.0f}}, {Frame{3}, {1.0f}}}}};
    plan.layers[0].enable_3d = true;
    CHECK(analyze_temporal_rates(plan).layers[0].rate == TemporalRateClass::PerFrame);
}

TEST_CASE("Temporal rate report owns layer identifiers independently of plan lifetime") {
    const auto make_report = [] {
        RenderPlan plan;
        plan.canvas.duration = Frame{2};
        LayerPlan layer;
        layer.id = "owned-layer-id";
        plan.layers.push_back(layer);
        return analyze_temporal_rates(plan);
    };
    const auto report = make_report();
    REQUIRE(report.layers.size() == 1);
    CHECK(report.layers[0].layer_id == "owned-layer-id");
}

TEST_CASE("Temporal interval merge is bounded and preserves exact per-frame count") {
    RenderPlan plan;
    plan.canvas.duration = Frame{1'000'001};
    LayerPlan layer;
    layer.id = "cued";
    layer.type = LayerType::Image;
    layer.start_frame = Frame{0};
    layer.duration_frames = Frame{1'000'001};
    AnimationTrackPlan track;
    track.keyframes = {{Frame{0}, {0.0f}}, {Frame{10}, {1.0f}}};
    layer.animation = AnimationPlan{{track}};
    plan.layers.push_back(layer);
    const auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::KeyframeInterval);
    CHECK(report.layers[0].eval_frames.size() == 1'000'000);
    CHECK(report.layers[0].eval_frame_count == 1'000'000);
    CHECK_FALSE(report.layers[0].eval_frames_complete);
}

TEST_CASE("Temporal rate analysis covers all layer-local animated sources") {
    const auto classify = [](LayerPlan layer) {
        RenderPlan plan;
        plan.canvas.duration = Frame{8};
        plan.layers.push_back(std::move(layer));
        return analyze_temporal_rates(plan).layers[0].rate;
    };
    LayerPlan transition;
    transition.type = LayerType::Image;
    transition.transition_in = TransitionPlan{};
    CHECK(classify(transition) == TemporalRateClass::PerFrame);

    LayerPlan counter;
    counter.type = LayerType::Text;
    counter.text_counter = TextCounterPlan{};
    CHECK(classify(counter) == TemporalRateClass::PerFrame);

    LayerPlan shape;
    shape.type = LayerType::Shape;
    shape.shape = ShapeContentPlan{};
    AnimationTrackPlan paint;
    paint.keyframes = {{Frame{0}, {0.0f}}, {Frame{4}, {1.0f}}};
    shape.shape->fill_color_animation = paint;
    CHECK(classify(shape) == TemporalRateClass::KeyframeInterval);

    LayerPlan subpath;
    subpath.type = LayerType::Shape;
    subpath.shape = ShapeContentPlan{};
    ImportedSubpathPlan imported;
    imported.stroke_width_animation = paint;
    subpath.shape->imported_subpaths.push_back(imported);
    CHECK(classify(subpath) == TemporalRateClass::KeyframeInterval);
}

TEST_CASE("Temporal rate analysis ignores camera time on flat layers and clips 3D keys") {
    RenderPlan plan;
    plan.canvas.duration = Frame{12};
    LayerPlan flat;
    flat.id = "flat";
    flat.type = LayerType::Image;
    LayerPlan three_d;
    three_d.id = "3d";
    three_d.type = LayerType::Mesh;
    three_d.start_frame = Frame{4};
    three_d.duration_frames = Frame{4};
    plan.layers = {flat, three_d};
    plan.camera_animation = std::vector<CameraTrackPlan>{
        CameraTrackPlan{CameraPropertyPlan::PositionX,
            {{Frame{0}, {0.0f}}, {Frame{8}, {1.0f}}}}};
    auto report = analyze_temporal_rates(plan);
    CHECK(report.layers[0].rate == TemporalRateClass::Static);
    CHECK(report.layers[1].rate == TemporalRateClass::KeyframeInterval);
    CHECK(report.layers[1].eval_frames == std::vector<Frame>{Frame{4}, Frame{5}, Frame{6}, Frame{7}});
}

TEST_CASE("Temporal rate analysis is deterministic and does not mutate RenderPlan") {
    const auto plan = five_rates();
    const auto before = plan.layers[2].animation->tracks[0].keyframes;
    const auto first = analyze_temporal_rates(plan);
    const auto second = analyze_temporal_rates(plan);
    REQUIRE(first.layers.size() == second.layers.size());
    CHECK(first.skippable_node_fraction == second.skippable_node_fraction);
    for (std::size_t i = 0; i < first.layers.size(); ++i) {
        CHECK(first.layers[i].rate == second.layers[i].rate);
        CHECK(first.layers[i].eval_frames == second.layers[i].eval_frames);
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(plan.layers[2].animation->tracks[0].keyframes[i].frame == before[i].frame);
        CHECK(plan.layers[2].animation->tracks[0].keyframes[i].value == before[i].value);
    }
}
