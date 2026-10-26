#include "test_prepared_render_plan_test_support.hpp"

TEST_CASE("render-plan layer transitions lower into the existing scene layer spec") {
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["layers"][0]["transition_in"] = {
        {"kind", "slide"}, {"duration_frames", 6}, {"delay_frames", 1},
        {"direction", "left"}, {"params", {{"distance", 1.25}}}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    chronon3d::assets::AssetResolver resolver;
    const auto prepared = chronon3d::render_plan::compile_render_plan(*decoded, resolver);
    if (!prepared) INFO(prepared.error().message);
    REQUIRE(prepared);

    chronon3d::FrameContextParams params;
    params.global_time = chronon3d::SampleTime::from_frame(0.0, decoded->canvas.fps);
    params.duration = decoded->canvas.duration;
    params.width = decoded->canvas.width;
    params.height = decoded->canvas.height;
    auto scene = prepared->composition->evaluate(chronon3d::make_frame_context(params));
    REQUIRE(scene.layers().size() == 1);
    CHECK(chronon3d::layer_transition_kind(
              scene.layers()[0].transition_in.transition_id) ==
          chronon3d::LayerTransitionKind::Slide);
    CHECK(scene.layers()[0].transition_in.duration == doctest::Approx(0.2));
    CHECK(scene.layers()[0].transition_in.delay == doctest::Approx(1.0 / 30.0));
    const auto transition_hash = chronon3d::graph::hash_layer_transition_spec(
        scene.layers()[0].transition_in);
    auto changed_spec = scene.layers()[0].transition_in;
    changed_spec.parameters = chronon3d::SlideParams{1.5f};
    CHECK(chronon3d::graph::hash_layer_transition_spec(changed_spec) != transition_hash);
}

// ── media card / video corner contract ─────────────────────────────
//
// A media layer may declare the rounded plate behind it (style.background) and
// the corner radius of its own box (radius). One compile must produce:
//
//   - a colour plate layer materialized BEFORE the clip, sized to the clip's
//     displayed box plus 2*padding, carrying the plate colour and radius and
//     the drop shadow (the plate IS the frame);
//   - the clip itself unchanged in size, with its rounded-rect mask derived
//     from radius and no residual shadow/background declaration.
TEST_CASE("media background card and video radius lower into plate plus corner mask") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "video");
    {
        std::ofstream file(directory.path / "video/source.mp4", std::ios::binary);
        file << "video bytes";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v2","version":2,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":2},
      "layers":[{"id":"source","type":"video","source":"video/source.mp4","size":[32,32],
        "radius":4,
        "style":{
          "background":{"color":"#FFFFFF","radius":6,"padding":[4,4]},
          "shadow":{"color":"#000000","opacity":0.5,"blur":4,"offset":[0,2]}}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);

    FrameContextParams params;
    params.global_time = SampleTime::from_frame(0.0, decoded->canvas.fps);
    params.duration = decoded->canvas.duration;
    params.width = decoded->canvas.width;
    params.height = decoded->canvas.height;
    const auto scene = prepared->composition->evaluate(make_frame_context(params));
    REQUIRE(scene.layers().size() == 2);
    // z-order: the plate is composited UNDER the clip.
    CHECK(scene.layers()[0].name == "source__frame");
    CHECK(scene.layers()[1].name == "source");

    const auto& plate = scene.layers()[0];
    REQUIRE(!plate.nodes.empty());
    CHECK(plate.nodes.front().corner_radius == doctest::Approx(6.0f));
    CHECK(plate.transform.position.x == doctest::Approx(32.0f));
    CHECK(plate.transform.position.y == doctest::Approx(32.0f));
    CHECK(plate.nodes.front().color.r == doctest::Approx(1.0f));
    REQUIRE(!plate.effects().empty());
    CHECK(plate.effects()[0].enabled);

    const auto& clip = scene.layers()[1];
    REQUIRE(clip.video_source != nullptr);
    CHECK(clip.video_source->size.x == doctest::Approx(32.0f));
    CHECK(clip.mask.enabled());
    CHECK(clip.mask.type == MaskType::RoundedRect);
    CHECK(clip.mask.radius == doctest::Approx(4.0f));
    CHECK(clip.mask.size.x == doctest::Approx(32.0f));
    CHECK(clip.mask.feather == doctest::Approx(1.0f));
    CHECK(clip.transform.position.x == doctest::Approx(0.0f));
    CHECK(clip.transform.position.y == doctest::Approx(0.0f));
    CHECK(clip.effects().empty());
}

TEST_CASE("media background card and rounded stroke frame an image") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::ifstream fixture(repo_image_fixture(), std::ios::binary);
    REQUIRE(fixture.good());
    const std::string image_bytes{std::istreambuf_iterator<char>(fixture),
                                  std::istreambuf_iterator<char>()};
    write_asset(directory.path, image_bytes);

    auto plan = image_plan();
    plan.layers.front().size = {32.0f, 32.0f};
    plan.layers.front().size_dimensions = 2;
    plan.layers.front().position = {32.0f, 32.0f, 0.0f};
    plan.layers.front().position_dimensions = 3;
    plan.layers.front().opacity = 0.0f;
    plan.layers.front().radius = 4.0f;
    AnimationPlan media_animation;
    AnimationTrackPlan opacity_track;
    opacity_track.property = "opacity";
    opacity_track.keyframes = {{Frame{0}, {0.0f}}, {Frame{2}, {1.0f}}};
    media_animation.tracks.push_back(opacity_track);
    AnimationTrackPlan scale_track;
    scale_track.property = "scale";
    scale_track.keyframes = {{Frame{0}, {1.0f, 1.0f, 1.0f}},
                             {Frame{2}, {1.5f, 1.5f, 1.0f}}};
    media_animation.tracks.push_back(scale_track);
    plan.layers.front().animation = std::move(media_animation);
    LayerStylePlan style;
    style.background = BackgroundStyle{
        "#FFFFFF", 1.0f, 6.0f, {4.0f, 4.0f}, 2};
    plan.layers.front().style = style;

    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto prepared = compile_render_plan(plan, resolver);
    if (!prepared) INFO(prepared.error().message);
    REQUIRE(prepared);

    FrameContextParams params;
    params.global_time = SampleTime::from_frame(0.0, plan.canvas.fps);
    params.duration = plan.canvas.duration;
    params.width = plan.canvas.width;
    params.height = plan.canvas.height;
    const auto scene = prepared->composition->evaluate(make_frame_context(params));
    REQUIRE(scene.layers().size() == 2);
    CHECK(scene.layers()[0].name == "image__frame");
    CHECK(scene.layers()[1].name == "image");
    REQUIRE(!scene.layers()[0].nodes.empty());
    const auto& frame_node = scene.layers()[0].nodes.front();
    REQUIRE(frame_node.shape.type() == ShapeType::RoundedRect);
    CHECK(frame_node.shape.rounded_rect().size.x == doctest::Approx(40.0f));
    CHECK(frame_node.shape.rounded_rect().size.y == doctest::Approx(40.0f));
    CHECK(frame_node.shape.rounded_rect().radius == doctest::Approx(6.0f));
    CHECK(frame_node.corner_radius == doctest::Approx(6.0f));
    CHECK(scene.layers()[0].transform.position.x ==
          doctest::Approx(scene.layers()[1].transform.position.x));
    CHECK(scene.layers()[0].transform.position.y ==
          doctest::Approx(scene.layers()[1].transform.position.y));
    CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.0f));
    REQUIRE(scene.layers()[0].anim_transform.scale.is_animated());
    CHECK(scene.layers()[0].anim_transform.scale.evaluate(SampleTime::from_frame(0.0, plan.canvas.fps)).x ==
          doctest::Approx(1.0f));
    CHECK(scene.layers()[0].anim_transform.scale.evaluate(SampleTime::from_frame(2.0, plan.canvas.fps)).x ==
          doctest::Approx(1.5f));
    REQUIRE(scene.layers()[1].nodes.size() == 1);
    CHECK(scene.layers()[1].nodes.front().shape.type() == ShapeType::Image);
    CHECK(scene.layers()[1].nodes.front().shape.image().radius == doctest::Approx(4.0f));
    params.global_time = SampleTime::from_frame(2.0, plan.canvas.fps);
    const auto final_scene = prepared->composition->evaluate(make_frame_context(params));
    CHECK(final_scene.layers()[0].transform.opacity == doctest::Approx(1.0f));
    CHECK(final_scene.layers()[1].transform.opacity == doctest::Approx(1.0f));
    CHECK(final_scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
}

// A shadow without a plate is the clip's own shadow (the widened style.shadow
// gate): no plate is materialized and the clip keeps its square corners.
TEST_CASE("video shadow without a background stays on the clip") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "video");
    {
        std::ofstream file(directory.path / "video/source.mp4", std::ios::binary);
        file << "video bytes";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v2","version":2,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"source","type":"video","source":"video/source.mp4","size":[32,32],
        "style":{"shadow":{"color":"#000000","opacity":0.4,"blur":3,"offset":[1,2]}}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);

    FrameContextParams params;
    params.global_time = SampleTime::from_frame(0.0, decoded->canvas.fps);
    params.duration = decoded->canvas.duration;
    params.width = decoded->canvas.width;
    params.height = decoded->canvas.height;
    const auto scene = prepared->composition->evaluate(make_frame_context(params));
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].name == "source");
    CHECK(!scene.layers()[0].mask.enabled());
    REQUIRE(!scene.layers()[0].effects().empty());
    CHECK(scene.layers()[0].effects()[0].enabled);
}

TEST_CASE("render plan lowers a bounded static Lottie path through the resolver") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[8,10,0]},
            "s":{"a":0,"k":[120,80,100]},"r":{"a":0,"k":15},"o":{"a":0,"k":75}},
          "shapes":[{"ty":"gr","it":[{"ty":"gr","it":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":1},
              "d":[{"n":"d","v":{"a":0,"k":4}},
                {"n":"g","v":{"a":0,"k":2}},
                {"n":"o","v":{"a":0,"k":1}}]},
            {"ty":"tr","a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[10,0]},
              "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":50}}]},
            {"ty":"tr","a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[5,0]},
              "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}}]},
            {"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[30,0],[50,0],[50,20]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"el","p":{"a":0,"k":[0,30]},"s":{"a":0,"k":[20,10]}},
            {"ty":"rc","p":{"a":0,"k":[30,30]},"s":{"a":0,"k":[20,10]},"r":{"a":0,"k":2}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[0,1,0,1]}},
            {"ty":"gf","r":1,"t":1,"s":{"a":0,"k":[0,30]},
              "e":{"a":0,"k":[20,30]},"o":{"a":0,"k":100},
              "g":{"p":2,"k":{"a":0,"k":[0,0,0,1,1,1,1,1]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,1,1,1]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":2}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":3}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":4}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/mark.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);
    CHECK(prepared->assets.assets().size() == 1);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].transform.position.x == doctest::Approx(-24.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.y == doctest::Approx(-22.0f));
    CHECK(evaluated->scene.layers()[0].transform.scale.x == doctest::Approx(1.2f));
    CHECK(evaluated->scene.layers()[0].transform.scale.y == doctest::Approx(0.8f));
    CHECK(glm::degrees(glm::eulerAngles(evaluated->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(15.0f));
    CHECK(evaluated->scene.layers()[0].transform.opacity == doctest::Approx(0.75f));
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 4);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().commands.size() == 4);
    CHECK(evaluated->scene.layers()[0].nodes[1].shape.path().commands.size() == 4);
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().commands.size() == 6);
    CHECK(evaluated->scene.layers()[0].nodes[3].shape.path().commands.size() == 10);
    const auto& lottie_nodes = evaluated->scene.layers()[0].nodes;
    CHECK(lottie_nodes[0].shape.path().fill.solid.r == doctest::Approx(1.0f));
    CHECK(lottie_nodes[0].shape.path().fill.solid.a == doctest::Approx(0.5f));
    CHECK(lottie_nodes[0].shape.path().commands[0].p0.x == doctest::Approx(15.0f));
    CHECK(lottie_nodes[0].shape.path().fill.solid.g == doctest::Approx(0.0f));
    CHECK(lottie_nodes[1].shape.path().fill.solid.r == doctest::Approx(0.0f));
    CHECK(lottie_nodes[1].shape.path().fill.solid.g == doctest::Approx(1.0f));
    CHECK(lottie_nodes[2].shape.path().fill.gradient.type == graphics::GradientType::Linear);
    REQUIRE(lottie_nodes[2].shape.path().fill.gradient.color_stops.size() == 2);
    CHECK(lottie_nodes[2].shape.path().fill.gradient.color_stops[0].color.b == doctest::Approx(1.0f));
    CHECK(lottie_nodes[3].shape.path().fill.solid.r == doctest::Approx(1.0f));
    CHECK(lottie_nodes[0].shape.path().stroke.enabled);
    CHECK(lottie_nodes[0].shape.path().stroke.width == doctest::Approx(1.0f));
    REQUIRE(lottie_nodes[0].shape.path().stroke.dash_array.size() == 2);
    CHECK(lottie_nodes[0].shape.path().stroke.dash_array[0] == doctest::Approx(4.0f));
    CHECK(lottie_nodes[0].shape.path().stroke.dash_offset == doctest::Approx(1.0f));
    CHECK(lottie_nodes[3].shape.path().stroke.width == doctest::Approx(4.0f));

    auto unsupported = *decoded;
    // The decoder is allowed to retain the reference; the compiler rejects
    // unsupported expressions explicitly instead of silently dropping them.
    {
        std::ofstream file(directory.path / "shapes/mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,"exp":"x"}]})JSON";
    }
    CHECK_FALSE(compile_render_plan(unsupported, resolver));

    using json = nlohmann::json;
    const auto identity_layer = [] {
        return json{{"ty", 4}, {"ddd", 0},
            {"ks", {{"a", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"p", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
                    {"r", {{"a", 0}, {"k", 0}}},
                    {"o", {{"a", 0}, {"k", 100}}}}},
            {"shapes", json::array()}};
    };
    const auto static_path = [] {
        return json{{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
            {"c", true}, {"v", {{0, 0}, {10, 0}}},
            {"i", {{0, 0}, {0, 0}}}, {"o", {{0, 0}, {0, 0}}}}}}}};
    };
    std::vector<json> unsupported_documents;
    {
        auto layer = identity_layer();
        layer["masksProperties"] = json::array({json{{"mode", "a"}}});
        layer["shapes"].push_back(static_path());
        layer["shapes"].push_back(json{{"ty", "fl"}, {"r", 1},
            {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({json{{"ty", "gr"}, {"it", json::array()}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        const auto animated_group_transform = json{{"ty", "tr"},
            {"a", {{"a", 0}, {"k", {0, 0}}}},
            {"p", {{"a", 1}, {"k", {{{"t", 0}, {"s", {0, 0}}}, {{"t", 10}, {"s", {10, 0}}}}}}},
            {"s", {{"a", 0}, {"k", {100, 100}}}},
            {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}};
        const auto static_group_transform = json{{"ty", "tr"},
            {"a", {{"a", 0}, {"k", {0, 0}}}}, {"p", {{"a", 0}, {"k", {5, 0}}}},
            {"s", {{"a", 0}, {"k", {100, 100}}}},
            {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}};
        layer["shapes"] = json::array({json{{"ty", "gr"}, {"it", json::array({
            json{{"ty", "gr"}, {"it", json::array({static_path(), animated_group_transform})}},
            static_group_transform})}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({json{{"ty", "el"},
            {"p", {{"a", 1}, {"k", json::array()}}},
            {"s", {{"a", 0}, {"k", {20, 10}}}},
            {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(),
            json{{"ty", "gf"}, {"g", {{"p", 2}, {"k", {{"a", 0}, {"k", json::array()}}}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(), static_path(),
            json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}},
            json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {0, 1, 0, 1}}}}},
            json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {0, 0, 1, 1}}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(),
            json{{"ty", "st"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 1, 1, 1}}}},
                {"w", {{"a", 0}, {"k", 2}}},
                {"d", json::array({json{{"n", "unsupported"}, {"v", {{"a", 0}, {"k", 4}}}}})}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    for (const auto& document : unsupported_documents) {
        { std::ofstream file(directory.path / "shapes/mark.json"); file << document.dump(); }
        CHECK_FALSE(compile_render_plan(unsupported, resolver));
    }
}

TEST_CASE("render plan retains animated Lottie repeater operators until frame evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    {
        std::ofstream file(directory.path / "mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[0,0]},
                "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[0,0],[8,0],[8,8],[0,8]],"i":[[0,0],[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":31},
      "layers":[{"id":"vector","type":"shape","size":[64,64],"position":[32,32],
        "shape":{"type":"path","lottie":"mark.json","operators":[
          {"kind":"repeater","params":{"copies":3,"position":[6,0],
            "animation":{"easing":"linear","keyframes":[
              {"frame":0,"value":[0,0,1,1,0]},
              {"frame":30,"value":[12,0,2,1,0]}]}}}]}}],
      "output":{"path":"out.mp4","format":"mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);
    const auto first = evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto last = evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{30});
    REQUIRE(first);
    REQUIRE(last);
    REQUIRE(first->scene.layers().size() == 1);
    REQUIRE(last->scene.layers().size() == 1);
    const auto& initial_nodes = first->scene.layers()[0].nodes;
    const auto& final_nodes = last->scene.layers()[0].nodes;
    REQUIRE(initial_nodes.size() == 3);
    REQUIRE(final_nodes.size() == 3);
    CHECK(initial_nodes[2].shape.path().commands[0].p0.x == doctest::Approx(0.0f));
    CHECK(final_nodes[2].shape.path().commands[0].p0.x == doctest::Approx(24.0f));
    CHECK(final_nodes[2].shape.path().commands[1].p0.x >
          initial_nodes[2].shape.path().commands[1].p0.x);
}

TEST_CASE("render plan lowers animated Lottie group transforms through native tracks") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/group.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"gr","it":[
            {"ty":"sh","ks":{"a":0,"k":{"c":false,"v":[[0,0],[10,0]],
              "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[0,0,1,1]},"w":{"a":0,"k":2}},
            {"ty":"tr","a":{"a":0,"k":[0,0]},
              "p":{"a":1,"k":[{"t":0,"s":[0,0],"e":[0,0]},{"t":10,"s":[10,0]}]},
              "s":{"a":1,"k":[{"t":0,"s":[100,100],"e":[100,100]},{"t":10,"s":[200,200]}]},
              "r":{"a":1,"k":[{"t":0,"s":[0],"e":[0]},{"t":10,"s":[90]}]},
              "o":{"a":1,"k":[{"t":0,"s":[100],"e":[100]},{"t":10,"s":[50]}]}}
          ]}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"group","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/group.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    for (const auto [frame, expected_x, expected_y, expected_alpha, expected_width] : {
            std::tuple{0, 10.0f, 0.0f, 1.0f, 2.0f},
            std::tuple{5, 15.6066f, 10.6066f, 0.75f, 3.0f},
            std::tuple{10, 10.0f, 20.0f, 0.5f, 4.0f}}) {
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{frame});
        REQUIRE(evaluated);
        const auto& node = evaluated->scene.layers()[0].nodes[0];
        CHECK(node.shape.path().commands[1].p0.x == doctest::Approx(expected_x));
        CHECK(node.shape.path().commands[1].p0.y == doctest::Approx(expected_y));
        CHECK(node.fill.solid.a == doctest::Approx(expected_alpha));
        CHECK(node.shape.path().stroke.width == doctest::Approx(expected_width));
    }
}

TEST_CASE("render plan lowers Lottie masks with animated opacity and path") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/mask.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,
          "layers":[{"ty":4,"ddd":0,
            "masksProperties":[{"mode":"s","inv":false,
            "pt":{"a":0,"k":{"c":true,"v":[[0,0],[20,0],[20,20]],
              "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}},
            "o":{"a":1,"k":[{"t":0,"s":[100],"e":[100]},{"t":10,"s":[50]}]},
            "x":{"a":0,"k":2},"f":{"a":0,"k":[2,2]}},
            {"mode":"a","inv":false,
             "pt":{"a":1,"k":[
               {"t":0,"s":[{"c":true,"v":[[0,0],[20,0],[20,20]],
                 "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}],
                "e":[{"c":true,"v":[[0,0],[30,0],[30,30]],
                 "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]},
               {"t":10,"s":[{"c":true,"v":[[0,0],[30,0],[30,30]],
                 "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]}]},
             "o":{"a":0,"k":100}}],
            "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
              "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
            "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
              "v":[[0,0],[30,0],[30,30]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}}},
              {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"masked","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/mask.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    for (const auto [frame, expected_opacity] : {
            std::pair{0, 1.0f}, std::pair{5, 0.75f}, std::pair{10, 0.5f}}) {
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{frame});
        REQUIRE(evaluated);
        const auto& mask = evaluated->scene.layers()[0].mask;
        CHECK(mask.type == MaskType::Path);
        CHECK(mask.initial_mode == MaskCombineMode::Subtract);
        CHECK(mask.opacity == doctest::Approx(expected_opacity));
        CHECK(mask.feather == doctest::Approx(2.0f));
        CHECK(mask.expansion == doctest::Approx(2.0f));
        CHECK(mask.path.size() == 4);
        REQUIRE(mask.operations.size() == 1);
        CHECK(mask.operations[0].path[1].p0.x == doctest::Approx(
            frame == 0 ? 10.0f : frame == 5 ? 15.0f : 20.0f));
    }
    nlohmann::json incompatible;
    {
        std::ifstream input(directory.path / "shapes/mask.json");
        input >> incompatible;
    }
    auto& target_shape = incompatible["layers"][0]["masksProperties"][1]
        ["pt"]["k"][0]["e"][0];
    target_shape["v"].push_back({40, 40});
    target_shape["i"].push_back({0, 0});
    target_shape["o"].push_back({0, 0});
    {
        std::ofstream output(directory.path / "shapes/mask.json");
        output << incompatible.dump();
    }
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
}

TEST_CASE("render plan lowers bounded two-keyframe Lottie L4 path morphs") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/morph.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":1,"k":[
            {"t":0,"s":[{"c":true,"v":[[0,0],[10,0],[10,10]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}],
              "e":[{"c":true,"v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]},
            {"t":10,"s":[{"c":true,"v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]}]}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"morph","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/morph.json"}}],"output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto first = evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto last = evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{10});
    REQUIRE(first);
    REQUIRE(last);
    const auto& first_path = first->scene.layers()[0].nodes[0].shape.path().commands;
    const auto& last_path = last->scene.layers()[0].nodes[0].shape.path().commands;
    REQUIRE(first_path.size() == last_path.size());
    CHECK(first_path[1].p0.x == doctest::Approx(10.0f));
    CHECK(last_path[1].p0.x == doctest::Approx(20.0f));
}

TEST_CASE("render plan lowers multi-keyframe Lottie path morphs piecewise") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/morph.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":21,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":1,"k":[
            {"t":0,"s":[{"c":false,"v":[[0,0],[10,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}],
              "e":[{"c":false,"v":[[0,0],[20,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}]},
            {"t":10,"s":[{"c":false,"v":[[0,0],[20,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}],
              "e":[{"c":false,"v":[[0,0],[30,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}]},
            {"t":20,"s":[{"c":false,"v":[[0,0],[30,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}]}]}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":21},
      "layers":[{"id":"morph","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/morph.json"}}],"output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    for (const auto [frame, expected_x] : {std::pair{5, 15.0f}, std::pair{10, 20.0f},
                                            std::pair{15, 25.0f}, std::pair{20, 30.0f}}) {
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{frame});
        REQUIRE(evaluated);
        const auto& commands = evaluated->scene.layers()[0].nodes[0].shape.path().commands;
        REQUIRE(commands.size() == 2);
        CHECK(commands[1].p0.x == doctest::Approx(expected_x));
    }
}

TEST_CASE("render plan lowers animated Lottie Trim Paths into the native trim track") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    const std::string lottie_source = R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
      "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
        "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
      "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":false,"v":[[0,0],[100,0]],
        "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
        {"ty":"tm","s":{"a":0,"k":0},"e":{"a":1,"k":[
          {"t":0,"s":[50],"o":{"x":0.333,"y":0.333}},
          {"t":10,"s":[50],"i":{"x":0.667,"y":0.667}}]},
          "o":{"a":1,"k":[{"t":0,"s":[0],"o":{"x":0.333,"y":0.333}},
            {"t":10,"s":[180],"i":{"x":0.667,"y":0.667}}]},"m":1},
        {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    const auto write_source = [&](const std::string& source) {
        std::ofstream file(directory.path / "shapes/trim.json");
        file << source;
    };
    write_source(lottie_source);
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":128,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"trim","type":"shape","size":[128,64],
        "shape":{"type":"path","lottie":"shapes/trim.json"}}],
      "output":{"path":"out.mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto first = evaluate(prepared->compiled_composition,
                                CompositionEvaluateContext{}, Frame{0});
    const auto last = evaluate(prepared->compiled_composition,
                               CompositionEvaluateContext{}, Frame{10});
    const auto middle = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{5});
    REQUIRE(first);
    REQUIRE(last);
    REQUIRE(middle);
    const auto& first_path = first->scene.layers()[0].nodes[0].shape.path().commands;
    const auto& last_path = last->scene.layers()[0].nodes[0].shape.path().commands;
    const auto& middle_path = middle->scene.layers()[0].nodes[0].shape.path().commands;
    REQUIRE(first_path.size() >= 2);
    REQUIRE(last_path.size() >= 2);
    REQUIRE(middle_path.size() >= 2);
    CHECK(first_path.front().p0.x == doctest::Approx(0.0f));
    CHECK(first_path.back().p0.x == doctest::Approx(50.0f));
    CHECK(middle_path.front().p0.x == doctest::Approx(25.0f));
    CHECK(middle_path.back().p0.x == doctest::Approx(75.0f));
    CHECK(last_path.front().p0.x == doctest::Approx(50.0f));
    CHECK(last_path.back().p0.x == doctest::Approx(100.0f));

    auto unsupported_mode = lottie_source;
    const auto mode = unsupported_mode.find("\"m\":1");
    REQUIRE(mode != std::string::npos);
    unsupported_mode.replace(mode, std::string("\"m\":1").size(), "\"m\":2");
    write_source(unsupported_mode);
    CHECK_FALSE(compile_render_plan(*decoded, resolver));

    auto mismatched_timing = lottie_source;
    const auto last_offset_frame = mismatched_timing.find("\"t\":10,\"s\":[180]");
    REQUIRE(last_offset_frame != std::string::npos);
    mismatched_timing.replace(last_offset_frame, std::string("\"t\":10,\"s\":[180]").size(),
                              "\"t\":11,\"s\":[180]");
    write_source(mismatched_timing);
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
}

TEST_CASE("render plan lowers animated Lottie solid fill and stroke paint") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/paint.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":false,"v":[[0,0],[100,0]],
            "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":1,"k":[
              {"t":0,"s":[1,0,0,1],"o":{"x":0.333,"y":0.333}},
              {"t":10,"s":[0,0,1,1],"i":{"x":0.667,"y":0.667}}]},
              "o":{"a":1,"k":[{"t":0,"s":[100],"o":{"x":0.333,"y":0.333}},
                {"t":10,"s":[50],"i":{"x":0.667,"y":0.667}}]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[0,1,0,1]},"w":{"a":1,"k":[
              {"t":0,"s":[2],"o":{"x":0.333,"y":0.333}},
              {"t":10,"s":[10],"i":{"x":0.667,"y":0.667}}]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":128,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"paint","type":"shape","size":[128,64],
        "shape":{"type":"path","lottie":"shapes/paint.json"}}],
      "output":{"path":"out.mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto at_start = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto at_middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{5});
    const auto at_end = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{10});
    REQUIRE(at_start);
    REQUIRE(at_middle);
    REQUIRE(at_end);
    const auto& start = at_start->scene.layers()[0].nodes[0].shape.path();
    const auto& middle = at_middle->scene.layers()[0].nodes[0].shape.path();
    const auto& end = at_end->scene.layers()[0].nodes[0].shape.path();
    CHECK(start.fill.solid.r == doctest::Approx(1.0f));
    CHECK(start.fill.solid.b == doctest::Approx(0.0f));
    CHECK(start.fill.solid.a == doctest::Approx(1.0f));
    CHECK(middle.fill.solid.r == doctest::Approx(0.5f));
    CHECK(middle.fill.solid.b == doctest::Approx(0.5f));
    CHECK(middle.fill.solid.a == doctest::Approx(0.75f));
    CHECK(end.fill.solid.r == doctest::Approx(0.0f));
    CHECK(end.fill.solid.b == doctest::Approx(1.0f));
    CHECK(end.fill.solid.a == doctest::Approx(0.5f));
    CHECK(start.stroke.width == doctest::Approx(2.0f));
    CHECK(middle.stroke.width == doctest::Approx(6.0f));
    CHECK(end.stroke.width == doctest::Approx(10.0f));
}
