#include "test_prepared_render_plan_test_support.hpp"

TEST_CASE("render plan compiles semantic word fill-color animator") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "semantic-word-fill";
    plan.canvas = {.width = 320, .height = 120,
                   .fps = FrameRate{30, 1}, .duration = Frame{10}};
    LayerPlan text;
    text.id = "caption";
    text.type = LayerType::Text;
    text.text = "Hi future";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    LayerStylePlan style;
    style.font = text.font;
    style.font_size = 32.0f;
    style.fill = "#FFFFFF";
    text.style = style;
    TextSpanOverride word_span;
    word_span.byte_start = 3;
    word_span.byte_end = 9;
    word_span.semantic_id = "word-0";
    text.text_spans.push_back(word_span);

    TextSelectorPlan selector;
    selector.semantic_id = "word-0";
    selector.shape = "square";
    selector.amount.keyframes = {
        {Frame{0}, {0.0f}}, {Frame{8}, {100.0f}}};
    TextAnimatorPlan animator;
    animator.id = "word-highlight";
    animator.selectors.push_back(selector);
    AnimationTrackPlan fill_color;
    fill_color.property = "fill_color";
    fill_color.keyframes = {{Frame{0}, {1.0f, 0.7f, 0.0f, 1.0f}}};
    animator.properties.push_back(fill_color);
    text.text_animators.push_back(animator);
    plan.layers.push_back(text);

    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(source_root);
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{4});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto shape = evaluated->scene.layers()[0].nodes[0]
        .shape.text_run_shape_handle().value;
    REQUIRE(shape);
    REQUIRE(shape->animators.size() == 1);
    REQUIRE(shape->animators[0].properties.size() == 1);
    const auto* target = std::get_if<FillColorProperty>(
        &shape->animators[0].properties[0]);
    REQUIRE(target);
    const auto evaluated_color = target->color.evaluate(4.0);
    CHECK(evaluated_color.r == doctest::Approx(1.0f));
    CHECK(evaluated_color.g == doctest::Approx(0.7f));
    CHECK(evaluated_color.b == doctest::Approx(0.0f));
}

TEST_CASE("render plan evaluates animated solid fills and typed transitions per frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-solid-transition";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{12}};
    LayerPlan shape;
    shape.id = "paint";
    shape.type = LayerType::Shape;
    shape.size = {24.0f, 24.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->fill_color = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
    AnimationTrackPlan fill;
    fill.property = "fill_color";
    fill.keyframes = {{Frame{0}, {0.0f, 0.0f, 0.0f, 1.0f}},
                      {Frame{10}, {1.0f, 0.0f, 0.0f, 1.0f}}};
    shape.animation = AnimationPlan{.tracks = {fill}};
    TransitionPlan transition;
    transition.id = "slide";
    transition.kind = "slide";
    transition.duration_frames = Frame{15};  // 0.5 s at 30 fps
    transition.direction = "left";
    transition.distance = 0.5f;
    shape.transition_in = transition;
    plan.layers.push_back(shape);

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto frame0 = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{0});
    const auto frame5 = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{5});
    REQUIRE(frame0);
    REQUIRE(frame5);
    REQUIRE(frame0->scene.layers().size() == 1);
    REQUIRE(frame5->scene.layers().size() == 1);
    const auto& layer0 = frame0->scene.layers()[0];
    const auto& layer5 = frame5->scene.layers()[0];
    REQUIRE(layer0.nodes.size() == 1);
    REQUIRE(layer5.nodes.size() == 1);
    CHECK(layer0.nodes[0].fill.solid.r == doctest::Approx(0.0f));
    CHECK(layer5.nodes[0].fill.solid.r == doctest::Approx(0.5f));
    CHECK(layer_transition_kind(layer0.transition_in.transition_id) ==
          LayerTransitionKind::Slide);
    CHECK(std::get<SlideParams>(layer0.transition_in.parameters).distance == doctest::Approx(0.5f));
}

TEST_CASE("render plan evaluates animated path mask geometry per frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-path-mask";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{12}};
    LayerPlan layer;
    layer.id = "masked";
    layer.type = LayerType::Color;
    layer.color = {1.0f, 0.0f, 0.0f, 1.0f};
    LayerPlan::PathMaskPlan mask;
    const auto rect_path = [](float extent) {
        return std::vector<ShapePathCommandPlan>{
            {ShapePathCommandTypePlan::MoveTo, {-extent, -extent}},
            {ShapePathCommandTypePlan::LineTo, {extent, -extent}},
            {ShapePathCommandTypePlan::LineTo, {extent, extent}},
            {ShapePathCommandTypePlan::LineTo, {-extent, extent}},
            {ShapePathCommandTypePlan::Close}};
    };
    mask.path = rect_path(4.0f);
    mask.morph_target = rect_path(8.0f);
    AnimationTrackPlan animation;
    animation.property = "mask_morph";
    animation.keyframes = {{Frame{0}, {0.0f}}, {Frame{12}, {1.0f}}};
    mask.morph_animation = animation;
    layer.path_mask = mask;
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(prepared->compiled_composition,
                                CompositionEvaluateContext{}, Frame{0});
    const auto end = evaluate(prepared->compiled_composition,
                              CompositionEvaluateContext{}, Frame{12});
    REQUIRE(start);
    REQUIRE(end);
    REQUIRE(start->scene.layers().size() == 1);
    REQUIRE(end->scene.layers().size() == 1);
    CHECK(start->scene.layers()[0].mask.size.x == doctest::Approx(8.0f));
    CHECK(end->scene.layers()[0].mask.size.x == doctest::Approx(16.0f));
}

TEST_CASE("render plan lowers ordered mask operations to the scene compositor") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "ordered-mask-stack";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "masked";
    layer.type = LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};
    const auto rect = [](std::string mode, float extent) {
        LayerPlan::PathMaskPlan mask;
        mask.type = "rect";
        mask.mode = std::move(mode);
        mask.size = {extent * 2.0f, extent * 2.0f};
        return mask;
    };
    const auto ellipse = [](std::string mode, float rx, float ry) {
        LayerPlan::PathMaskPlan mask;
        mask.type = "ellipse";
        mask.mode = std::move(mode);
        mask.size = {rx * 2.0f, ry * 2.0f};
        return mask;
    };
    auto path = LayerPlan::PathMaskPlan{};
    path.type = "path";
    path.mode = "add";
    path.path = {{ShapePathCommandTypePlan::MoveTo, {-1.0f, -1.0f}},
                 {ShapePathCommandTypePlan::LineTo, {1.0f, -1.0f}},
                 {ShapePathCommandTypePlan::LineTo, {1.0f, 1.0f}},
                 {ShapePathCommandTypePlan::LineTo, {-1.0f, 1.0f}},
                 {ShapePathCommandTypePlan::Close}};
    auto rounded_rect = rect("add", 3.0f);
    rounded_rect.type = "rounded_rect";
    rounded_rect.radius = 1.0f;
    layer.masks = {rect("add", 20.0f), rect("subtract", 4.0f), path,
                   ellipse("add", 2.0f, 2.0f), rounded_rect,
                   ellipse("intersect", 30.0f, 30.0f), rect("difference", 1.0f)};
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& mask = evaluated->scene.layers()[0].mask;
    REQUIRE(mask.operations.size() == 6);
    CHECK(mask.operations[0].mode == MaskCombineMode::Subtract);
    CHECK(mask.operations[1].type == MaskType::Path);
    CHECK(mask.operations[2].type == MaskType::Ellipse);
    CHECK(mask.operations[3].type == MaskType::RoundedRect);
    CHECK(mask.operations[4].mode == MaskCombineMode::Intersect);
    CHECK(mask.operations[5].mode == MaskCombineMode::Difference);
    CHECK(mask_alpha_at_local_point(mask, Vec2{0.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(mask_alpha_at_local_point(mask, Vec2{8.0f, 0.0f}) == doctest::Approx(1.0f));
}

TEST_CASE("render plan fill and gradient tracks clamp eased values and hold as steps") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    LayerPlan solid;
    solid.type = LayerType::Color;
    solid.color = {0.0f, 0.0f, 1.0f, 1.0f};
    AnimationTrackPlan fill;
    fill.property = "fill_color";
    fill.easing = "hold";
    fill.keyframes = {{Frame{5}, {1.0f, 0.0f, 0.0f, 1.0f}},
                      {Frame{10}, {0.0f, 1.0f, 0.0f, 1.0f}}};
    solid.animation = AnimationPlan{.tracks = {fill}};
    const auto before = chronon3d::render_plan::detail::fill_color_at(
        solid, 2.0, Color{0.0f, 0.0f, 1.0f, 1.0f}, FrameRate{30, 1});
    REQUIRE(before.has_value());
    CHECK(before->b == doctest::Approx(1.0f));
    const auto held = chronon3d::render_plan::detail::fill_color_at(
        solid, 7.0, Color{0.0f, 0.0f, 1.0f, 1.0f}, FrameRate{30, 1});
    REQUIRE(held.has_value());
    CHECK(held->r == doctest::Approx(1.0f));
    CHECK(held->g == doctest::Approx(0.0f));
    const auto last = chronon3d::render_plan::detail::fill_color_at(
        solid, 10.0, Color{0.0f, 0.0f, 1.0f, 1.0f}, FrameRate{30, 1});
    REQUIRE(last.has_value());
    CHECK(last->g == doctest::Approx(1.0f));

    const auto gradient = [](Color first) {
        return graphics::GradientDefinition::linear(
            {0.0f, 0.0f}, {1.0f, 0.0f},
            {{0.0f, first}, {1.0f, Color::white()}},
            graphics::GradientSpread::Pad);
    };
    LayerPlan gradient_layer;
    gradient_layer.type = LayerType::Shape;
    gradient_layer.shape = ShapeContentPlan{};
    gradient_layer.shape->gradient = gradient(Color::blue());
    GradientAnimationPlan gradient_track;
    gradient_track.easing = "hold";
    gradient_track.keyframes = {
        {Frame{5}, gradient(Color::red())},
        {Frame{10}, gradient(Color::green())}};
    gradient_layer.fill_gradient_animation = gradient_track;
    const auto gradient_before = chronon3d::render_plan::detail::fill_gradient_at(gradient_layer, 2.0);
    REQUIRE(gradient_before.has_value());
    CHECK(gradient_before->color_stops[0].color.b == doctest::Approx(1.0f));
    const auto gradient_held = chronon3d::render_plan::detail::fill_gradient_at(gradient_layer, 7.0);
    REQUIRE(gradient_held.has_value());
    CHECK(gradient_held->color_stops[0].color.r == doctest::Approx(1.0f));
    CHECK(gradient_held->color_stops[0].color.g == doctest::Approx(0.0f));
}

TEST_CASE("render plan compiles a GradientMesh fill to the canonical path paint") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "gradient-mesh-fill";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan shape;
    shape.id = "mesh";
    shape.type = LayerType::Shape;
    shape.size = {32.0f, 32.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->gradient_mesh = graphics::GradientMesh::four_corner(
        Color::red(), Color::green(), Color::blue(), Color::white());
    plan.layers.push_back(std::move(shape));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto& authored_mesh = plan.layers[0].shape->gradient_mesh;
    REQUIRE(authored_mesh);
    REQUIRE(authored_mesh->nodes.size() == 4);
    CHECK(authored_mesh->nodes[0].color.r == doctest::Approx(1.0f));
    CHECK(authored_mesh->nodes[1].color.g == doctest::Approx(1.0f));
    CHECK(authored_mesh->nodes[2].color.b == doctest::Approx(1.0f));
}

TEST_CASE("render plan evaluates animated gradient color ramps in authored sRGB") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-gradient-ramp";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{10}};
    const auto make_gradient = [](Color first, Color last, float start_x) {
        return graphics::GradientDefinition::linear(
            {start_x, 0.0f}, {1.0f, 0.0f},
            {{0.0f, first}, {1.0f, last}}, graphics::GradientSpread::Reflect);
    };
    LayerPlan shape;
    shape.id = "gradient";
    shape.type = LayerType::Shape;
    shape.size = {32.0f, 32.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->gradient = make_gradient(Color::red(), Color::blue(), 0.0f);
    GradientAnimationPlan animation;
    animation.keyframes = {
        {Frame{0}, make_gradient(Color::red(), Color::blue(), 0.0f)},
        {Frame{10}, make_gradient(Color::green(), Color::white(), 0.5f)}};
    shape.fill_gradient_animation = animation;
    plan.layers.push_back(shape);

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{5});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto& fill = evaluated->scene.layers()[0].nodes[0].fill;
    REQUIRE(fill.gradient.type == graphics::GradientType::Linear);
    CHECK(fill.gradient.start.x == doctest::Approx(0.25f));
    REQUIRE(fill.gradient.color_stops.size() == 2);
    CHECK(fill.gradient.color_stops[0].color.r == doctest::Approx(0.7353569f).epsilon(0.002f));
    CHECK(fill.gradient.color_stops[0].color.g == doctest::Approx(0.7353569f).epsilon(0.002f));
}

TEST_CASE("render plan resolves grow-and-shrink text height before evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "responsive-caption";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan text;
    text.id = "caption";
    text.type = LayerType::Text;
    text.text = "A longer caption wraps to multiple lines";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    text.size = {160.0f, 12.0f};
    text.size_dimensions = 2;
    LayerStylePlan style;
    style.font = text.font;
    style.font_size = 24.0f;
    style.fill = "#FFFFFF";
    style.text_fit_mode = "grow_and_shrink";
    style.min_font_size = 12.0f;
    style.max_font_size = 32.0f;
    text.style = style;
    plan.layers.push_back(std::move(text));

    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(source_root);
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto handle = evaluated->scene.layers()[0].nodes[0]
        .shape.text_run_shape_handle().value;
    REQUIRE(handle);
    CHECK(handle->layout_spec.box.x == doctest::Approx(160.0f));
    CHECK(handle->layout_spec.box.y > 12.0f);
    CHECK(handle->layout_spec.auto_fit == false);
}

TEST_CASE("render plan text background becomes a measured card behind the text run") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "caption-card";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan text;
    text.id = "caption";
    text.type = LayerType::Text;
    text.text = "A caption that wraps into multiple lines";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    text.size = {170.0f, 16.0f};
    text.size_dimensions = 2;
    LayerStylePlan style;
    style.font = text.font;
    style.font_size = 24.0f;
    style.fill = "#FFFFFF";
    style.text_fit_mode = "grow_and_shrink";
    style.min_font_size = 12.0f;
    style.background = BackgroundStyle{
        .color = "#101820", .opacity = 0.9f, .radius = 6.0f,
        .padding = {10.0f, 8.0f}, .padding_dimensions = 2};
    text.style = style;
    plan.layers.push_back(std::move(text));

    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(source_root);
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& nodes = evaluated->scene.layers()[0].nodes;
    REQUIRE(nodes.size() == 2);
    CHECK(nodes[0].shape.type() == ShapeType::RoundedRect);
    CHECK(nodes[1].shape.type() == ShapeType::TextRun);
    CHECK(nodes[0].shape.rounded_rect().size.y > 16.0f);
    const auto run = nodes[1].shape.text_run_shape_handle().value;
    REQUIRE(run);
    CHECK(run->layout_spec.box.y > 16.0f);
}

TEST_CASE("render plan composes synchronized X and Y position tracks") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "component-position-tracks";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{25}};
    LayerPlan shape;
    shape.id = "moving-shape";
    shape.type = LayerType::Shape;
    shape.size = {20.0f, 20.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    AnimationPlan animation;
    AnimationTrackPlan x;
    x.property = "position_x";
    x.keyframes = {{Frame{0}, {0.0f}}, {Frame{24}, {480.0f}}};
    AnimationTrackPlan y;
    y.property = "position_y";
    y.keyframes = {{Frame{0}, {0.0f}}, {Frame{24}, {240.0f}}};
    animation.tracks = {x, y};
    shape.animation = std::move(animation);
    plan.layers.push_back(std::move(shape));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    const auto initial = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(initial);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].transform.position.x -
          initial->scene.layers()[0].transform.position.x == doctest::Approx(240.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.y -
          initial->scene.layers()[0].transform.position.y == doctest::Approx(120.0f));
}

TEST_CASE("render plan evaluates animated trim paths at the requested frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-trim-path";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{11}};
    LayerPlan layer;
    layer.id = "progress-route";
    layer.type = LayerType::Shape;
    layer.size = {100.0f, 1.0f};
    layer.size_dimensions = 2;
    ShapeContentPlan path;
    path.type = ShapeContentTypePlan::Path;
    path.path = {
        ShapePathCommandPlan{ShapePathCommandTypePlan::MoveTo, {0.0f, 0.0f}},
        ShapePathCommandPlan{ShapePathCommandTypePlan::LineTo, {100.0f, 0.0f}}};
    PathOperatorPlan trim;
    trim.kind = PathOperatorKindPlan::Trim;
    trim.trim_start = 0.0f;
    trim.trim_end = 0.1f;
    AnimationTrackPlan animation;
    animation.property = "trim";
    animation.keyframes = {
        AnimationKeyframePlan{Frame{0}, {0.0f, 0.1f}},
        AnimationKeyframePlan{Frame{10}, {0.0f, 0.75f}}};
    trim.trim_animation = animation;
    path.operators.push_back(std::move(trim));
    layer.shape = std::move(path);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto at_start = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto at_end = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{10});
    REQUIRE(at_start);
    REQUIRE(at_end);
    REQUIRE(at_start->scene.layers()[0].nodes[0].shape.path().commands.size() >= 2);
    REQUIRE(at_end->scene.layers()[0].nodes[0].shape.path().commands.size() >= 2);
    CHECK(at_start->scene.layers()[0].nodes[0].shape.path().commands.back().p0.x ==
          doctest::Approx(10.0f).epsilon(0.02f));
    CHECK(at_end->scene.layers()[0].nodes[0].shape.path().commands.back().p0.x ==
          doctest::Approx(75.0f).epsilon(0.02f));
}

TEST_CASE("render plan lowers star, polygon, arrow, and arc to prepared path nodes") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const std::vector<nlohmann::json> shapes{
        {{"type", "star"}, {"points", 7}, {"inner_radius_ratio", 0.4}},
        {{"type", "polygon"}, {"points", 6}, {"rotation_degrees", 15.0}},
        {{"type", "arrow"}, {"shaft_ratio", 0.3}, {"head_ratio", 0.8}},
        {{"type", "arc"}, {"start_degrees", 10.0}, {"sweep_degrees", 180.0},
         {"segments", 32}, {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0}}}},
        {{"type", "grid"}, {"spacing", 10.0},
         {"stroke", {{"color", "#FFFFFF"}, {"width", 1.0}}}},
        {{"type", "dot_grid"}, {"spacing", 10.0}, {"dot_radius", 2.0},
         {"fill", {1.0, 0.0, 0.0, 1.0}}}
    };
    const std::array<std::size_t, 6> minimum_commands{15, 7, 8, 17, 18, 100};
    assets::AssetResolver resolver;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const auto source = render_plan_test::base_plan(
            {render_plan_test::shape_layer(shapes[i])});
        const auto decoded = decode_render_plan(source);
        REQUIRE(decoded);
        const auto prepared = compile_render_plan(*decoded, resolver);
        if (!prepared) INFO(prepared.error().message);
        REQUIRE(prepared);
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{0});
        REQUIRE(evaluated);
        REQUIRE(evaluated->scene.layers().size() == 1);
        REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
        const auto& path = evaluated->scene.layers()[0].nodes[0].shape;
        CHECK(path.type() == ShapeType::Path);
        CHECK(path.path().commands.size() >= minimum_commands[i]);
    }
}

TEST_CASE("device frames lower real phone and browser chrome subnodes") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const std::array<std::pair<nlohmann::json, std::size_t>, 2> cases{{
        {nlohmann::json{{"type", "device_frame"}, {"device", "phone"}, {"chrome", true},
                        {"fill", {0.2, 0.3, 0.4, 1.0}}}, 3},
        {nlohmann::json{{"type", "device_frame"}, {"device", "browser"}, {"chrome", true},
                        {"fill", {0.2, 0.3, 0.4, 1.0}}}, 6}
    }};
    assets::AssetResolver resolver;
    for (const auto& [shape, expected_nodes] : cases) {
        const auto source = render_plan_test::base_plan(
            {render_plan_test::shape_layer(shape)});
        const auto decoded = decode_render_plan(source);
        REQUIRE(decoded);
        const auto prepared = compile_render_plan(*decoded, resolver);
        if (!prepared) INFO(prepared.error().message);
        REQUIRE(prepared);
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{0});
        REQUIRE(evaluated);
        REQUIRE(evaluated->scene.layers().size() == 1);
        CHECK(evaluated->scene.layers()[0].nodes.size() == expected_nodes);
    }
}

TEST_CASE("Directional and Radial Blur lower to their registered processor payloads") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "blur-effects";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "source";
    layer.type = LayerType::Color;
    layer.color = {0.2f, 0.3f, 0.4f, 1.0f};
    LayerPlan::EffectPlan directional;
    directional.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    directional.directional_angle = 22.0f;
    directional.directional_length = 18.0f;
    directional.directional_samples = 9;
    LayerPlan::EffectPlan radial;
    radial.kind = LayerPlan::EffectKindPlan::RadialBlur;
    radial.radial_center = {0.25f, 0.75f};
    radial.radial_amount = 0.8f;
    radial.radial_render_samples = 12;
    radial.radial_preview_samples = 6;
    layer.effects = {directional, radial};
    plan.layers.push_back(std::move(layer));
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 2);
    const auto* lowered_directional = std::get_if<DirectionalBlurParams>(&effects[0].params);
    REQUIRE(lowered_directional);
    CHECK(lowered_directional->length == doctest::Approx(18.0f));
    const auto* lowered_radial = std::get_if<RadialBlurParams>(&effects[1].params);
    REQUIRE(lowered_radial);
    CHECK(lowered_radial->center.y == doctest::Approx(0.75f));
    CHECK(lowered_radial->render_samples == 12);
}

TEST_CASE("render plan spring tracks match the canonical spring sample") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "spring-position-track";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{11}};
    LayerPlan layer;
    layer.id = "spring-layer";
    layer.type = LayerType::Shape;
    layer.size = {8.0f, 8.0f};
    layer.size_dimensions = 2;
    layer.position_dimensions = 2;
    layer.shape = ShapeContentPlan{};
    AnimationTrackPlan track;
    track.property = "position_y";
    track.easing = "spring";
    track.spring = Spring::Bouncy;
    track.keyframes = {
        AnimationKeyframePlan{Frame{0}, {-300.0f}},
        AnimationKeyframePlan{Frame{10}, {0.0f}}};
    AnimationPlan animation;
    animation.tracks.push_back(track);
    layer.animation = std::move(animation);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto middle = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{5});
    REQUIRE(start);
    REQUIRE(middle);
    const float actual_delta = middle->scene.layers()[0].transform.position.y -
                               start->scene.layers()[0].transform.position.y;
    const float expected = sample_spring(5.0 / 30.0, -300.0f, 0.0f, Spring::Bouncy).value + 300.0f;
    CHECK(actual_delta == doctest::Approx(expected).epsilon(0.001f));
}

TEST_CASE("render plan forwards cycle, pingpong and offset loop modes to evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto sample = [](std::string mode, Frame frame) -> std::optional<float> {
        RenderPlan plan;
        plan.job_id = "loop-mode-track";
        plan.canvas = {.width = 320, .height = 180,
                       .fps = FrameRate{30, 1}, .duration = Frame{31}};
        LayerPlan layer;
        layer.id = "looping-shape";
        layer.type = LayerType::Shape;
        layer.size = {8.0f, 8.0f};
        layer.size_dimensions = 2;
        layer.shape = ShapeContentPlan{};
        AnimationTrackPlan track;
        track.property = "position_x";
        track.loop_mode = std::move(mode);
        track.keyframes = {{Frame{0}, {0.0f}}, {Frame{10}, {20.0f}}};
        AnimationPlan animation;
        animation.tracks.push_back(std::move(track));
        layer.animation = std::move(animation);
        plan.layers.push_back(std::move(layer));

        assets::AssetResolver resolver;
        const auto prepared = compile_render_plan(plan, resolver);
        if (!prepared) return std::nullopt;
        const auto evaluated = evaluate(
            prepared->compiled_composition, CompositionEvaluateContext{}, frame);
        if (!evaluated) return std::nullopt;
        return evaluated->scene.layers()[0].transform.position.x;
    };

    const auto origin = sample("cycle", Frame{0});
    const auto cycle = sample("cycle", Frame{15});
    const auto pingpong = sample("pingpong", Frame{15});
    const auto offset = sample("offset", Frame{15});
    REQUIRE(origin);
    REQUIRE(cycle);
    REQUIRE(pingpong);
    REQUIRE(offset);
    CHECK(*cycle - *origin == doctest::Approx(10.0f));
    CHECK(*pingpong - *origin == doctest::Approx(10.0f));
    CHECK(*offset - *origin == doctest::Approx(30.0f));
}

TEST_CASE("render plan wiggle driver evaluates deterministically on scalar opacity") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "wiggle-opacity-track";
    plan.canvas = {.width = 32, .height = 32,
                   .fps = FrameRate{30, 1}, .duration = Frame{31}};
    LayerPlan layer;
    layer.id = "wiggle-shape";
    layer.type = LayerType::Shape;
    layer.size = {8.0f, 8.0f};
    layer.size_dimensions = 2;
    layer.shape = ShapeContentPlan{};
    AnimationTrackPlan track;
    track.property = "opacity";
    track.keyframes = {{Frame{0}, {0.5f}}, {Frame{30}, {0.5f}}};
    track.wiggle = AnimationTrackPlan::WiggleDriverPlan{2.0f, 0.2f, 42};
    AnimationPlan animation;
    animation.tracks.push_back(std::move(track));
    layer.animation = std::move(animation);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{15});
    const auto repeated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{15});
    REQUIRE(first);
    REQUIRE(repeated);
    const double expected = 0.5 + math::evaluate_expression(
        "wiggle(2,0.2,time,42)",
        std::unordered_map<std::string, double>{{"time", 0.5}, {"frame", 15.0}, {"value", 0.5}},
        0.0);
    CHECK(first->scene.layers()[0].transform.opacity == doctest::Approx(expected).epsilon(0.0001));
    CHECK(repeated->scene.layers()[0].transform.opacity ==
          doctest::Approx(first->scene.layers()[0].transform.opacity));
}

TEST_CASE("render plan applies animated effect parameters at the sampled frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["layers"][0]["effects"] = {{{"type", "gaussian_blur"}, {"radius", 0.0}}};
    source["layers"][0]["effect_param_tracks"] = nlohmann::json::array({{
        {"effect_id", "blur.gaussian"}, {"param", "radius"},
        {"keyframes", {{{"frame", 0}, {"value", 0.0}}, {{"frame", 10}, {"value", 100.0}}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition, {}, Frame{5});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers()[0].effects().size() == 1);
    const auto* blur = std::get_if<BlurParams>(&evaluated->scene.layers()[0].effects()[0].params);
    REQUIRE(blur);
    CHECK(blur->radius == doctest::Approx(50.0f));
}

TEST_CASE("render plan compiles and samples a catalog-addressed effect track") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["layers"][0]["effects"] = nlohmann::json::array({nlohmann::json{
        {"type", "catalog"}, {"effect_id", "color.tint"},
        {"params", nlohmann::json::array({nlohmann::json{{"param", "amount"}, {"value", 0.25}}})}
    }});
    source["layers"][0]["effect_param_tracks"] = nlohmann::json::array({{
        {"effect_id", "color.tint"}, {"param", "amount"},
        {"keyframes", {{{"frame", 0}, {"value", 0.0}}, {{"frame", 10}, {"value", 1.0}}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition, {}, Frame{5});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers()[0].effects().size() == 1);
    const auto* tint = std::get_if<TintParams>(&evaluated->scene.layers()[0].effects()[0].params);
    REQUIRE(tint);
    CHECK(tint->amount == doctest::Approx(0.5f));
}
