#include "test_prepared_render_plan_test_support.hpp"

TEST_CASE("render plan lowers animated Lottie layer transforms to native tracks") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/motion.json");
        const std::string lottie_source = R"JSON({"v":"5.7.4","ip":0,"op":12,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[5,0,0]},
            "p":{"a":1,"k":[{"t":0,"s":[10,20,0],"o":{"x":0.333,"y":0.333}},
              {"t":10,"s":[50,20,0],"i":{"x":0.667,"y":0.667}}]},
            "s":{"a":1,"k":[{"t":0,"s":[100,100,100]},
              {"t":10,"s":[200,200,100]}]},
            "r":{"a":1,"k":[{"t":0,"s":[0],"h":1},{"t":10,"s":[90]}]},
            "o":{"a":1,"k":[{"t":0,"s":[100]},{"t":10,"s":[0]}]}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,"v":[[0,0],[10,0]],
            "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
        file << lottie_source;
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"motion","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/motion.json"}}],
      "output":{"path":"out.mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(prepared->compiled_composition,
                                CompositionEvaluateContext{}, Frame{0});
    const auto end = evaluate(prepared->compiled_composition,
                              CompositionEvaluateContext{}, Frame{10});
    const auto middle = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{5});
    REQUIRE_MESSAGE(start.has_value(), start.error().message);
    REQUIRE_MESSAGE(end.has_value(), end.error().message);
    REQUIRE_MESSAGE(middle.has_value(), middle.error().message);
    REQUIRE(start->scene.layers().size() == 1);
    REQUIRE(end->scene.layers().size() == 1);
    CHECK(end->scene.layers()[0].transform.position.x -
          start->scene.layers()[0].transform.position.x == doctest::Approx(40.0f));
    CHECK(middle->scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
    CHECK(middle->scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    CHECK(glm::degrees(glm::eulerAngles(middle->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(0.0f));
    CHECK(glm::degrees(glm::eulerAngles(end->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(90.0f));
    CHECK(start->scene.layers()[0].nodes[0].shape.path().fill.solid.r == doctest::Approx(1.0f));
    CHECK(start->scene.layers()[0].nodes[0].shape.path().commands[0].p0.x ==
          doctest::Approx(-5.0f));

    const auto original_source = [&] {
        std::ifstream input(directory.path / "shapes/motion.json");
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }();
    auto changed_source = original_source;
    const auto changed_value = changed_source.find("[50,20,0]");
    REQUIRE(changed_value != std::string::npos);
    changed_source.replace(changed_value, std::string("[50,20,0]").size(), "[51,20,0]");
    {
        std::ofstream file(directory.path / "shapes/motion.json");
        file << changed_source;
    }
    const auto changed = compile_render_plan(*decoded, resolver);
    REQUIRE(changed);
    CHECK(changed->fingerprint.content_digest != prepared->fingerprint.content_digest);

    {
        std::ofstream file(directory.path / "shapes/repeater.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[0,0]},
            "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[0,0],[10,0],[10,10]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}},
            {"ty":"rp","c":{"a":0,"k":3},"tr":{"a":{"a":0,"k":[0,0]},
              "p":{"a":0,"k":[10,0]},"s":{"a":0,"k":[100,100]},
              "r":{"a":0,"k":0},"so":{"a":0,"k":100},"eo":{"a":0,"k":50}}}]}]})JSON";
    }
    auto repeater_plan = *decoded;
    repeater_plan.layers[0].shape->lottie_asset = "shapes/repeater.json";
    const auto repeater_prepared = compile_render_plan(repeater_plan, resolver);
    REQUIRE(repeater_prepared);
    const auto repeated = evaluate(repeater_prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(repeated);
    REQUIRE(repeated->scene.layers().size() == 1);
    REQUIRE(repeated->scene.layers()[0].nodes.size() == 3);
    CHECK(repeated->scene.layers()[0].nodes[2].fill.solid.a == doctest::Approx(0.5f));

    {
        std::ofstream file(directory.path / "shapes/motion.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":1,"k":[{"t":0,"s":[0,0]},
            {"t":10,"s":[5,0]}]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},
            "o":{"a":0,"k":100}},"shapes":[]}]})JSON";
    }
    const auto animated_anchor = compile_render_plan(*decoded, resolver);
    CHECK_FALSE(animated_anchor);
    if (!animated_anchor)
        CHECK(animated_anchor.error().code == "lottie_unsupported_or_invalid");
}

#if defined(CHRONON_TEST_ENABLE_SVG)
TEST_CASE("render plan lowers SVG text and tspan into canonical text layers") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    std::filesystem::create_directories(directory.path / "fonts");
    const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::filesystem::copy_file(source_root / "assets/fonts/Inter-Regular.ttf",
        directory.path / "fonts/Inter-Regular.ttf", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(source_root / "assets/fonts/Inter-Bold.ttf",
        directory.path / "fonts/Inter-Bold.ttf", std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream svg(directory.path / "art/text.svg");
        svg << R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="80">
          <g id="labels" font-family="Inter" font-size="22" fill="#123456">
            <text id="headline" x="100" y="38" text-anchor="middle" dominant-baseline="middle">Hello <tspan id="accent" fill="#ff0000" font-weight="bold" baseline-shift="25%">Chronon</tspan></text>
            <text id="central" x="100" y="38" dominant-baseline="central">Baseline</text>
            <text id="coordinates" x="10 80 150" y="65 25 65" fill-opacity="0.5" stroke="#00ff00" stroke-width="2" stroke-opacity="0.25" stroke-linecap="round" stroke-linejoin="bevel" stroke-miterlimit="8" stroke-dasharray="2 1" stroke-dashoffset="-1">ABC</text>
            <text id="before" x="10" y="72" dominant-baseline="text-before-edge">Top</text>
            <text id="after" x="10" y="72" dominant-baseline="text-after-edge">Bottom</text>
          </g></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":200,"height":80,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"svg","type":"shape","size":[200,80],
        "shape":{"type":"path","asset":"art/text.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 9);
    REQUIRE(evaluated->scene.layers()[1].nodes.size() == 1);
    REQUIRE(evaluated->scene.layers()[2].nodes.size() == 1);
    const auto& first_text_layer = evaluated->scene.layers()[1];
    const auto& second_text_layer = evaluated->scene.layers()[2];
    const float first_glyph_origin = first_text_layer.transform.position.x +
        first_text_layer.nodes[0].world_transform.position.x;
    const float second_glyph_origin = second_text_layer.transform.position.x +
        second_text_layer.nodes[0].world_transform.position.x;
    CHECK(first_glyph_origin < second_glyph_origin);
    const float first_glyph_y = first_text_layer.transform.position.y +
        first_text_layer.nodes[0].world_transform.position.y;
    const float second_glyph_y = second_text_layer.transform.position.y +
        second_text_layer.nodes[0].world_transform.position.y;
    CHECK(first_glyph_y != doctest::Approx(second_glyph_y));
    const auto& central_text_layer = evaluated->scene.layers()[3];
    REQUIRE(central_text_layer.nodes.size() == 1);
    const float central_glyph_y = central_text_layer.transform.position.y +
        central_text_layer.nodes[0].world_transform.position.y;
    CHECK(first_glyph_y != doctest::Approx(central_glyph_y));
    CHECK(evaluated->scene.layers()[4].transform.position.x <
          evaluated->scene.layers()[5].transform.position.x);
    CHECK(evaluated->scene.layers()[5].transform.position.x <
          evaluated->scene.layers()[6].transform.position.x);
    CHECK(evaluated->scene.layers()[4].transform.position.y !=
          doctest::Approx(evaluated->scene.layers()[5].transform.position.y));
    const auto styled_glyph = evaluated->scene.layers()[4].nodes[0]
        .shape.text_run_shape_handle().value;
    REQUIRE(styled_glyph);
    CHECK(styled_glyph->paint.fill.a == doctest::Approx(0.5f));
    CHECK(styled_glyph->paint.stroke_enabled);
    CHECK(styled_glyph->paint.stroke_width == doctest::Approx(2.0f));
    CHECK(styled_glyph->paint.stroke_color.g == doctest::Approx(1.0f));
    CHECK(styled_glyph->paint.stroke_color.a == doctest::Approx(0.25f));
    CHECK(styled_glyph->paint.stroke_cap == LineCap::Round);
    CHECK(styled_glyph->paint.stroke_join == LineJoin::Bevel);
    CHECK(styled_glyph->paint.stroke_miter_limit == doctest::Approx(8.0f));
    REQUIRE(styled_glyph->paint.stroke_dash_array.size() == 2);
    CHECK(styled_glyph->paint.stroke_dash_array[0] == doctest::Approx(2.0f));
    CHECK(styled_glyph->paint.stroke_dash_offset == doctest::Approx(-1.0f));
    REQUIRE(evaluated->scene.layers()[7].nodes.size() == 1);
    REQUIRE(evaluated->scene.layers()[8].nodes.size() == 1);
    const float before_edge_y = evaluated->scene.layers()[7].transform.position.y +
        evaluated->scene.layers()[7].nodes[0].world_transform.position.y;
    const float after_edge_y = evaluated->scene.layers()[8].transform.position.y +
        evaluated->scene.layers()[8].nodes[0].world_transform.position.y;
    CHECK(before_edge_y > after_edge_y);
}

TEST_CASE("render plan lays out SVG textPath clusters on an addressable path") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    std::filesystem::create_directories(directory.path / "fonts");
    const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::filesystem::copy_file(source_root / "assets/fonts/Inter-Regular.ttf",
        directory.path / "fonts/Inter-Regular.ttf", std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream svg(directory.path / "art/text_path.svg");
        svg << R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="80">
          <defs><path id="route" d="M 20 40 L 180 40"/></defs>
          <text font-family="Inter" font-size="22" fill="#ffffff">
            <textPath id="caption" href="#route" startOffset="10">ABC</textPath>
          </text></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":200,"height":80,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"svg","type":"shape","size":[200,80],"position":[0,0],
        "shape":{"type":"path","asset":"art/text_path.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE_MESSAGE(prepared, prepared.error().code << ": " << prepared.error().message);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 4);
    std::array<float, 3> glyph_x{};
    for (std::size_t index = 0; index < glyph_x.size(); ++index) {
        const auto& text_layer = evaluated->scene.layers()[index + 1];
        REQUIRE(text_layer.nodes.size() == 1);
        glyph_x[index] = text_layer.transform.position.x +
            text_layer.nodes[0].world_transform.position.x;
    }
    CHECK(glyph_x[0] < glyph_x[1]);
    CHECK(glyph_x[1] < glyph_x[2]);
}

TEST_CASE("render plan imports a resolver-backed SVG before composition compile") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/vector.svg");
        svg << R"svg(<svg clip-path="url(#clip)"><!-- comments do not change the rendered subset --><defs>
          <clipPath id="clip"><rect x="0" y="0" width="48" height="48"/></clipPath>
          <linearGradient id="linear" gradientTransform="translate(0.1 0.2)"><stop offset="0" stop-color="#ff0000"><animate attributeName="stop-opacity" from="1" to="0" dur="1s"/></stop><stop offset="1" style="stop-color:blue;stop-opacity:0.5"><animate attributeName="stop-color" from="blue" to="lime" dur="1s"/></stop></linearGradient>
          <radialGradient id="radial" gradientTransform="translate(0.1 0)"><stop offset="0" stop-color="white"/><stop offset="1" stop-color="black"/></radialGradient>
          <path id="reusable" d="M 0 0 L 4 0 L 4 4 Z" fill="lime"/>
          </defs><path d="M 0 0 L 20 0 L 20 20 Z" fill="#ff0000" stroke="#00ff00" stroke-width="2" stroke-linecap="round" stroke-linejoin="bevel" stroke-miterlimit="6" stroke-dasharray="4 2" stroke-dashoffset="1" opacity="0.5"/>
          <path d="M 24 0 L 44 0 L 44 20 Z" fill="#0000ff" stroke="none"/>
          <path d="M 0 24 L 20 24 L 20 44 Z" fill="url(#linear)"/>
          <path d="M 24 24 L 44 24 L 44 44 Z" fill="url(#radial)"/>
          <use href="#reusable" x="5" y="6"/></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/vector.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE_MESSAGE(prepared, prepared.error().code << ": " << prepared.error().message);
    CHECK(prepared->assets.assets().size() == 1);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 5);
    CHECK(evaluated->scene.layers()[0].nodes[0].fill.solid.r == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].fill.solid.b == doctest::Approx(0.0f));
    CHECK(evaluated->scene.layers()[0].nodes[1].fill.solid.r == doctest::Approx(0.0f));
    CHECK(evaluated->scene.layers()[0].nodes[1].fill.solid.b == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.5f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.enabled);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(2.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.cap == LineCap::Round);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.join == LineJoin::Bevel);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.miter_limit == doctest::Approx(6.0f));
    REQUIRE(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.dash_array.size() == 2);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.dash_array[0] == doctest::Approx(4.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.dash_offset == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.color.g == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.color.a == doctest::Approx(0.5f));
    CHECK_FALSE(evaluated->scene.layers()[0].nodes[1].shape.path().stroke.enabled);
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.type ==
          graphics::GradientType::Linear);
    REQUIRE(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops.size() == 2);
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[0].color.r == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.a == doctest::Approx(0.5f));
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.start.x == doctest::Approx(2.0f));
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.start.y == doctest::Approx(28.0f));
    CHECK(evaluated->scene.layers()[0].nodes[3].shape.path().fill.gradient.type ==
          graphics::GradientType::Radial);
    CHECK(evaluated->scene.layers()[0].nodes[3].shape.path().fill.gradient.center.x == doctest::Approx(36.0f));
    CHECK(evaluated->scene.layers()[0].nodes[4].shape.path().commands[1].p0.x == doctest::Approx(9.0f));
    CHECK(evaluated->scene.layers()[0].nodes[4].shape.path().commands[1].p0.y == doctest::Approx(6.0f));
    CHECK(evaluated->scene.layers()[0].nodes[4].fill.solid.g == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].mask.type == MaskType::Path);
    const auto gradient_middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{15});
    const auto gradient_end = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{30});
    REQUIRE(gradient_middle); REQUIRE(gradient_end);
    CHECK(gradient_middle->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.g == doctest::Approx(0.735357f));
    CHECK(gradient_middle->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.b == doctest::Approx(0.735357f));
    CHECK(gradient_middle->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[0].color.a == doctest::Approx(0.5f));
    CHECK(gradient_end->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.g == doctest::Approx(1.0f));
    CHECK(gradient_end->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.b == doctest::Approx(0.0f));
    CHECK(gradient_end->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[0].color.a == doctest::Approx(0.0f));

    {
        std::ofstream svg(directory.path / "art/masked.svg");
        svg << R"svg(<svg><defs>
          <mask id="alpha" maskUnits="userSpaceOnUse" maskContentUnits="userSpaceOnUse">
            <path d="M 0 0 L 32 0 L 32 32 Z" fill="white"/>
          </mask></defs><path d="M 0 0 L 40 0 L 40 40 Z" fill="red" mask="url(#alpha)"/></svg>)svg";
    }
    auto masked_plan = *decoded;
    masked_plan.layers[0].shape->svg_asset = "art/masked.svg";
    const auto masked = compile_render_plan(masked_plan, resolver);
    REQUIRE(masked);
    const auto masked_frame = evaluate(masked->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(masked_frame);
    CHECK(masked_frame->scene.layers()[0].mask.type != MaskType::Path);
    REQUIRE(masked_frame->scene.layers()[0].nodes.size() == 1);
    CHECK(masked_frame->scene.layers()[0].nodes[0].svg_local_masks.size() == 1);

    auto missing = *decoded;
    missing.layers[0].shape->svg_asset = "art/not-found.svg";
    CHECK_FALSE(compile_render_plan(missing, resolver));

    const std::array<std::string, 13> unsupported_svg{
        "<svg><path d=\"M0 0L1 1\" fill=\"url(#gradient)\"/></svg>",
        "<svg><path d=\"M0 0L1 1\"/><g><use href=\"#shape\"/></g></svg>",
        "<svg><defs><linearGradient id=\"g\"/></defs><path d=\"M0 0L1 1\"/></svg>",
        "<svg><mask id=\"m\"><path d=\"M0 0L1 1\"/></mask></svg>",
        "<svg><path d=\"M0 0L1 1\" opacity=\"2\"/></svg>",
        "<svg><path d=\"M0 0L1 1\"><animate attributeName=\"opacity\" from=\"0\" to=\"1\" dur=\"1s\" repeatCount=\"indefinite\"/></path></svg>",
        "<svg><g><animate attributeName=\"fill\" from=\"red\" to=\"blue\" dur=\"1s\"/><path d=\"M0 0L1 1\"/></g></svg>",
        "<svg><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"scale\" from=\"1 2\" to=\"3 4\" dur=\"1s\"/></path></svg>",
        "<svg><path d=\"M0 0L1 1\"><animate attributeName=\"opacity\" from=\"0\" to=\"1\" dur=\"1.01s\"/></path></svg>",
        "<svg><g transform=\"translate(2 3)\"><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"translate\" from=\"0 0\" to=\"1 1\" dur=\"1s\"/></path></g></svg>",
        "<svg><use href=\"#missing\"/></svg>",
        "<svg><defs><g id=\"cycle\"><use href=\"#cycle\"/></g></defs><use href=\"#cycle\"/></svg>",
        "<svg><path d=\"M0 0L1 0\"><animate attributeName=\"d\" values=\"M0 0L1 0;M0 0L1 1L2 2\" dur=\"1s\"/></path></svg>"};
    for (std::size_t i = 0; i < unsupported_svg.size(); ++i) {
        const std::string logical = "art/rejected-" + std::to_string(i) + ".svg";
        { std::ofstream svg(directory.path / logical); svg << unsupported_svg[i]; }
        auto rejected = *decoded;
        rejected.layers[0].shape->svg_asset = logical;
        const auto result = compile_render_plan(rejected, resolver);
        INFO("unsupported SVG fixture index " << i);
        CHECK_FALSE(result);
        if (!result) {
            const bool expected_error = result.error().code == "svg_import_failed" ||
                result.error().code == "svg_unsupported_style" ||
                result.error().code == "svg_animation_timing_unsupported";
            CHECK(expected_error);
        }
    }
}

TEST_CASE("render plan lowers root SVG Gaussian blur through the native effect stack") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/blurred.svg");
        svg << R"svg(<svg filter="url(#soften)"><defs>
          <filter id="soften"><feGaussianBlur stdDeviation="3"/></filter>
          </defs><path d="M8 8L56 8L56 56L8 56Z" fill="#ff0000"/></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"svg","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/blurred.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* blur = std::get_if<BlurParams>(&effects[0].params);
    REQUIRE(blur);
    CHECK(blur->radius == doctest::Approx(3.0f));
}

TEST_CASE("render plan lowers SVG opacity and translate animations into native sampled paths") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/animated.svg");
        svg << R"svg(<svg><path d="M 0 0 L 10 0" fill="red" stroke="blue" stroke-width="2">
          <animateTransform attributeName="transform" type="translate" from="0 0" to="20 0" dur="1s" fill="freeze"/>
          <animateTransform attributeName="transform" type="scale" from="1 1" to="2 2" dur="1s" fill="freeze"/>
          <animateTransform attributeName="transform" type="rotate" from="0" to="90" dur="1s" fill="freeze"/>
          <animate attributeName="opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
          <animate attributeName="fill" from="red" to="white" dur="1s" fill="freeze"/>
          <animate attributeName="stroke" from="blue" to="lime" dur="1s" fill="freeze"/>
          <animate attributeName="stroke-width" from="2" to="6" dur="1s" fill="freeze"/>
          <animate attributeName="fill-opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
          <animate attributeName="stroke-opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
        </path><g>
          <animateTransform attributeName="transform" type="translate" from="0 0" to="10 0" dur="1s" fill="freeze"/>
          <animate attributeName="opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
          <path d="M 0 10 L 10 10" fill="blue"/>
        </g><path d="M 0 30 L 10 30" fill="red">
          <animate attributeName="d" values="M 0 30 L 10 30;M 0 30 L 20 30;M 0 30 L 30 30" dur="1s"/>
        </path></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":31},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/animated.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto first = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{15});
    const auto last = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{30});
    REQUIRE(first); REQUIRE(middle); REQUIRE(last);
    REQUIRE(first->scene.layers()[0].nodes.size() == 3);
    CHECK(first->scene.layers()[0].nodes[1].shape.path().commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(middle->scene.layers()[0].nodes[1].shape.path().commands[1].p0.x == doctest::Approx(15.0f));
    CHECK(last->scene.layers()[0].nodes[1].shape.path().commands[1].p0.x == doctest::Approx(20.0f));
    CHECK(middle->scene.layers()[0].nodes[1].fill.solid.a == doctest::Approx(1.0f));
    REQUIRE(middle->scene.layers()[0].nodes[1].svg_groups.size() == 1);
    REQUIRE(middle->scene.layers()[0].nodes[1].svg_groups[0].opacity_animation.has_value());
    CHECK(middle->scene.layers()[0].nodes[1].svg_groups[0].opacity_animation->from == doctest::Approx(1.0f));
    CHECK(middle->scene.layers()[0].nodes[1].svg_groups[0].opacity_animation->to == doctest::Approx(0.5f));
    CHECK(first->scene.layers()[0].nodes[2].shape.path().commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(middle->scene.layers()[0].nodes[2].shape.path().commands[1].p0.x == doctest::Approx(20.0f));
    CHECK(last->scene.layers()[0].nodes[2].shape.path().commands[1].p0.x == doctest::Approx(30.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().commands[1].p0.x == doctest::Approx(20.6066f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().commands[1].p0.y == doctest::Approx(10.6066f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().commands[1].p0.x == doctest::Approx(20.0f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().commands[1].p0.y == doctest::Approx(20.0f));
    CHECK(first->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(1.0f));
    CHECK(middle->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.5625f));
    CHECK(last->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.25f));
    CHECK(first->scene.layers()[0].nodes[0].fill.solid.g == doctest::Approx(0.0f));
    CHECK(middle->scene.layers()[0].nodes[0].fill.solid.g == doctest::Approx(0.5f));
    CHECK(last->scene.layers()[0].nodes[0].fill.solid.g == doctest::Approx(1.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().stroke.color.b == doctest::Approx(1.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().stroke.color.g == doctest::Approx(0.5f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().stroke.color.g == doctest::Approx(1.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(2.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(4.0f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(6.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().stroke.color.a == doctest::Approx(0.5625f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().stroke.color.a == doctest::Approx(0.25f));
}

TEST_CASE("render plan addresses imported SVG group ids and applies path operators") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/group.svg");
        svg << R"svg(<svg><g id="cluster"><path id="arrow" d="M 0 0 L 20 0" fill="red" stroke="white" stroke-width="2"/></g><path id="caption" d="M 0 12 L 8 12" fill="blue"/></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":31},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/group.svg",
          "element_animations":[{"target":"cluster","property":"opacity",
            "animation":{"keyframes":[{"frame":0,"value":[1]},
              {"frame":30,"value":[0.2]}]}}],
          "operators":[{"kind":"repeater","target":"cluster","params":{"copies":2,
            "position":[0,8],"opacity_decay":0.5,
            "animation":{"keyframes":[{"frame":0,"value":[0,8,1,1,0]},
              {"frame":30,"value":[20,8,1,1,0]}]}}},
            {"kind":"offset_path","target":"arrow","params":{"amount":2,"miter_limit":4}},
            {"kind":"wiggle_path","target":"caption","params":
              {"amount":0.5,"frequency":0.75,"seed":1234}}]} }],
      "output":{"path":"out.png"}})JSON");
    auto decoded = decode_render_plan(source);
    REQUIRE_MESSAGE(decoded, decoded.error().message);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{15});
    REQUIRE(first); REQUIRE(middle);
    REQUIRE(first->scene.layers()[0].nodes.size() == 3);
    CHECK(first->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(1.0f));
    CHECK(first->scene.layers()[0].nodes[1].fill.solid.a == doctest::Approx(0.5f));
    CHECK(first->scene.layers()[0].nodes[2].fill.solid.a == doctest::Approx(1.0f));
    CHECK(first->scene.layers()[0].nodes[1].shape.path().commands[0].p0.x == doctest::Approx(0.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().commands[0].p0.y == doctest::Approx(2.0f));
    CHECK(first->scene.layers()[0].nodes[2].shape.path().commands[1].p0.y != doctest::Approx(12.0f));
    CHECK(middle->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.6f));
    CHECK(middle->scene.layers()[0].nodes[1].fill.solid.a == doctest::Approx(0.3f));
    CHECK(middle->scene.layers()[0].nodes[2].fill.solid.a == doctest::Approx(1.0f));
    CHECK(middle->scene.layers()[0].nodes[1].shape.path().commands[0].p0.x == doctest::Approx(10.0f));

    auto changed_binding = source;
    changed_binding["layers"][0]["shape"]["element_animations"][0]
        ["animation"]["keyframes"][1]["value"][0] = 0.1;
    const auto changed_decoded = decode_render_plan(changed_binding);
    REQUIRE(changed_decoded);
    const auto changed_prepared = compile_render_plan(*changed_decoded, resolver);
    REQUIRE(changed_prepared);
    CHECK(changed_prepared->fingerprint.content_digest != prepared->fingerprint.content_digest);

    auto missing_target = source;
    missing_target["layers"][0]["shape"]["element_animations"][0]["target"] = "absent";
    const auto unresolved = decode_render_plan(missing_target);
    REQUIRE(unresolved);
    const auto rejected = compile_render_plan(*unresolved, resolver);
    CHECK_FALSE(rejected);
    if (!rejected) CHECK(rejected.error().code == "svg_target_missing");

    auto unsupported_property = source;
    unsupported_property["layers"][0]["shape"]["element_animations"][0]["target"] = "arrow";
    unsupported_property["layers"][0]["shape"]["element_animations"][0]["property"] = "filter.blur";
    const auto unsupported_decoded = decode_render_plan(unsupported_property);
    CHECK_FALSE(unsupported_decoded);
    auto invalid_programmatic_plan = *decoded;
    invalid_programmatic_plan.layers[0].shape->svg_element_animations[0].property = "filter.blur";
    const auto unsupported_result = compile_render_plan(invalid_programmatic_plan, resolver);
    CHECK_FALSE(unsupported_result);
    if (!unsupported_result)
        CHECK(unsupported_result.error().code == "svg_target_property_invalid");

    auto missing_operator_target = source;
    missing_operator_target["layers"][0]["shape"]["operators"][0]["target"] = "absent";
    const auto unresolved_operator = decode_render_plan(missing_operator_target);
    REQUIRE(unresolved_operator);
    const auto operator_rejected = compile_render_plan(*unresolved_operator, resolver);
    CHECK_FALSE(operator_rejected);
    if (!operator_rejected)
        CHECK(operator_rejected.error().code == "svg_target_missing");

    {
        std::ofstream svg(directory.path / "art/ambiguous.svg");
        svg << R"svg(<svg><defs><path id="symbol" d="M0 0 L4 0" fill="red"/></defs>
          <use id="instance_a" href="#symbol"/><use id="instance_b" href="#symbol"/></svg>)svg";
    }
    auto ambiguous_target = source;
    ambiguous_target["layers"][0]["shape"]["asset"] = "art/ambiguous.svg";
    ambiguous_target["layers"][0]["shape"]["operators"] = nlohmann::json::array();
    ambiguous_target["layers"][0]["shape"]["element_animations"][0]["target"] = "symbol";
    const auto ambiguous_plan = decode_render_plan(ambiguous_target);
    REQUIRE(ambiguous_plan);
    const auto ambiguous_result = compile_render_plan(*ambiguous_plan, resolver);
    CHECK_FALSE(ambiguous_result);
    if (!ambiguous_result)
        CHECK(ambiguous_result.error().code == "svg_target_ambiguous");

    {
        std::ofstream svg(directory.path / "art/duplicate.svg");
        svg << R"svg(<svg><path id="same" d="M0 0 L4 0" fill="red"/>
          <path id="same" d="M0 4 L4 4" fill="blue"/></svg>)svg";
    }
    auto duplicate_target = source;
    duplicate_target["layers"][0]["shape"]["asset"] = "art/duplicate.svg";
    duplicate_target["layers"][0]["shape"]["operators"] = nlohmann::json::array();
    duplicate_target["layers"][0]["shape"]["element_animations"][0]["target"] = "same";
    const auto duplicate_plan = decode_render_plan(duplicate_target);
    REQUIRE(duplicate_plan);
    const auto duplicate_result = compile_render_plan(*duplicate_plan, resolver);
    CHECK_FALSE(duplicate_result);
    if (!duplicate_result)
        CHECK(duplicate_result.error().code == "svg_import_failed");

    auto svg_merge = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/group.svg","operators":[
          {"kind":"merge","target":"arrow","params":{"paths":[[
            {"type":"move_to","point":[0,20]},
            {"type":"line_to","point":[4,20]}]]}}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto merge_plan = decode_render_plan(svg_merge);
    REQUIRE(merge_plan);
    const auto merge_result = compile_render_plan(*merge_plan, resolver);
    REQUIRE(merge_result);
    const auto merged = evaluate(merge_result->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(merged);
    REQUIRE(merged->scene.layers()[0].nodes.size() == 2);
    const auto& merged_path = merged->scene.layers()[0].nodes[0].shape.path();
    REQUIRE(merged_path.commands.size() == 4);
    CHECK(merged_path.commands[2].type == PathCommandType::MoveTo);
    CHECK(merged_path.commands[2].p0.x == doctest::Approx(0.0f));
    CHECK(merged_path.commands[2].p0.y == doctest::Approx(20.0f));

    svg_merge["layers"][0]["shape"]["operators"][0]["target"] = "cluster";
    const auto group_merge_plan = decode_render_plan(svg_merge);
    REQUIRE(group_merge_plan);
    const auto group_merge_result = compile_render_plan(*group_merge_plan, resolver);
    CHECK_FALSE(group_merge_result);
    if (!group_merge_result)
        CHECK(group_merge_result.error().code == "svg_target_property_invalid");
}

TEST_CASE("render plan morphs a targeted imported SVG element on the shared timeline") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/morph.svg");
        svg << R"svg(<svg width="32" height="32" viewBox="0 0 32 32">
          <path id="morphable" d="M 2 2 L 12 2 L 12 12 Z" fill="#00D9FF"/>
        </svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"vector","type":"shape","position":[32,32],"size":[64,64],
        "shape":{"type":"path","asset":"art/morph.svg","operators":[
          {"kind":"morph","target":"morphable","params":{
            "target":[{"type":"move_to","point":[2,2]},
              {"type":"line_to","point":[22,2]},
              {"type":"line_to","point":[22,12]},{"type":"close"}],
            "progress":0,"animation":{"keyframes":[
              {"frame":0,"value":0},{"frame":10,"value":1}]}}}]}}],
      "output":{"path":"out.mp4","format":"mp4"}})JSON");
    const auto decoded = decode_render_plan(source);
    REQUIRE_MESSAGE(decoded, decoded.error().message);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{5});
    const auto last = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{10});
    REQUIRE(first); REQUIRE(middle); REQUIRE(last);
    REQUIRE(first->scene.layers()[0].nodes.size() == 1);
    REQUIRE(middle->scene.layers()[0].nodes.size() == 1);
    REQUIRE(last->scene.layers()[0].nodes.size() == 1);
    const auto x_at = [](const auto& evaluated) {
        return evaluated.scene.layers()[0].nodes[0].shape.path().commands[1].p0.x;
    };
    CHECK(x_at(middle.value()) == doctest::Approx((x_at(first.value()) + x_at(last.value())) * 0.5f));
    CHECK(x_at(last.value()) > x_at(first.value()));

    auto incompatible = source;
    incompatible["layers"][0]["shape"]["operators"][0]["params"]["target"][1]["type"] = "cubic_to";
    incompatible["layers"][0]["shape"]["operators"][0]["params"]["target"][1]["control1"] = {6, 2};
    incompatible["layers"][0]["shape"]["operators"][0]["params"]["target"][1]["control2"] = {8, 2};
    const auto incompatible_plan = decode_render_plan(incompatible);
    REQUIRE(incompatible_plan);
    const auto incompatible_result = compile_render_plan(*incompatible_plan, resolver);
    REQUIRE(incompatible_result);
    const auto incompatible_evaluation = evaluate(incompatible_result->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    CHECK_FALSE(incompatible_evaluation);
}
#endif

TEST_CASE("all text backends share one linear-domain color packer") {
    const chronon3d::Color color{0.2158605f, 0.5f, 1.0f, 0.5f};
    const auto packed = chronon3d::text::pack_text_color(color);
    CHECK((packed & 0xFFu) == 55u);
    CHECK(((packed >> 8) & 0xFFu) == 128u);
    CHECK(((packed >> 16) & 0xFFu) == 255u);
    CHECK(((packed >> 24) & 0xFFu) == 128u);
}

TEST_CASE("compile_render_plan fails loudly when the budget is exceeded") {
    chronon3d::render_plan::RenderPlan plan;
    plan.canvas = {.width = 0, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::assets::AssetResolver resolver;
    const auto result = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE_FALSE(result);
    CHECK(result.error().path == "canvas.width");
}

TEST_CASE("prepared render plan owns the canonical compiled composition") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "prepared-plan-test";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};

    chronon3d::render_plan::LayerPlan color;
    color.id = "background";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.1f, 0.2f, 0.3f, 1.0f};
    plan.layers.push_back(color);

    chronon3d::assets::AssetResolver resolver;
    const auto prepared_result =
        chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared_result);
    const auto& prepared = prepared_result.value();

    REQUIRE(prepared.compiled_composition.composition);
    REQUIRE(prepared.compiled_composition.asset_manifest);
    CHECK(prepared.compiled_composition.asset_manifest->assets().empty());
    CHECK(prepared.compiled_composition.composition->name() ==
          "prepared-plan-test");
    CHECK(prepared.compiled_composition.composition->width() == 320);
    CHECK(prepared.compiled_composition.fingerprint != 0);
    REQUIRE(prepared.composition);
    CHECK(prepared.composition->width() ==
          prepared.compiled_composition.composition->width());

    // The compiled value owns its definition.  Dropping the source plan and
    // resolver cannot invalidate the scene callback or its metadata.
    const auto compiled = prepared.compiled_composition;
    const auto evaluated = chronon3d::evaluate(
        compiled, chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{1});
    REQUIRE(evaluated);
    CHECK(evaluated->scene.layers().size() == 1);

    chronon3d::assets::AssetResolver second_resolver;
    const auto repeated =
        chronon3d::render_plan::compile_render_plan(plan, second_resolver);
    REQUIRE(repeated);
    CHECK(repeated->compiled_composition.fingerprint ==
          prepared.compiled_composition.fingerprint);
    CHECK(repeated->fingerprint.content_digest == prepared.fingerprint.content_digest);
    CHECK(repeated->fingerprint.request_digest == prepared.fingerprint.request_digest);
}

TEST_CASE("render plan compiler transfers safe insets to layer layout rules") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "safe-insets-layout";
    plan.canvas = {.width = 1080, .height = 1920,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan shape;
    shape.id = "caption-card";
    shape.type = LayerType::Shape;
    shape.size = {800.0f, 180.0f};
    shape.size_dimensions = 2;
    shape.position = {900.0f, 1800.0f, 0.0f};
    shape.position_dimensions = 2;
    shape.safe_insets = std::array<float, 4>{80.0f, 60.0f, 220.0f, 60.0f};
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->fill_color = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
    plan.layers.push_back(shape);

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    CompositionEvaluateContext evaluate_context;
    evaluate_context.frame_context = make_frame_context(FrameContextParams{
        .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        .duration = Frame{1}, .width = 1080, .height = 1920});
    const auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition, evaluate_context, chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& layout = evaluated->scene.layers()[0].layout;
    CHECK(layout.enabled);
    CHECK(layout.keep_in_safe_area);
    CHECK(layout.safe_area.top == doctest::Approx(-880.0f));
    CHECK(layout.safe_area.right == doctest::Approx(1400.0f));
    CHECK(layout.safe_area.bottom == doctest::Approx(1360.0f));
    CHECK(layout.safe_area.left == doctest::Approx(-480.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.x == doctest::Approx(-320.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.y == doctest::Approx(560.0f));
}
