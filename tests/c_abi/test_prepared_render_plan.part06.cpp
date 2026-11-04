#include "test_prepared_render_plan_test_support.hpp"

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

    const std::array<std::string, 14> unsupported_svg{
        "<svg><path d=\"M0 0L1 1\" fill=\"url(#gradient)\"/></svg>",
        "<svg><path d=\"M0 0L1 1\"/><g><use href=\"#shape\"/></g></svg>",
        "<svg><defs><linearGradient id=\"g\"/></defs><path d=\"M0 0L1 1\"/></svg>",
        "<svg><mask id=\"m\"><path d=\"M0 0L1 1\"/></mask></svg>",
        "<svg><path d=\"M0 0L1 1\" opacity=\"2\"/></svg>",
        "<svg><path d=\"M0 0L1 1\"><animate attributeName=\"opacity\" from=\"0\" to=\"1\" dur=\"1s\" repeatCount=\"indefinite\"/></path></svg>",
        "<svg><g><animate attributeName=\"fill\" from=\"red\" to=\"blue\" dur=\"1s\"/><path d=\"M0 0L1 1\"/></g></svg>",
        "<svg><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"scale\" from=\"1 2\" to=\"3 4\" dur=\"1s\"/></path></svg>",
        "<svg><path d=\"M0 0L1 1\"><animate attributeName=\"opacity\" from=\"0\" to=\"1\" dur=\"1.01s\"/></path></svg>",
        "<svg><g transform=\"scale(2)\"><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"translate\" from=\"0 0\" to=\"1 1\" dur=\"1s\"/></path></g></svg>",
        "<svg><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"rotate\" from=\"0 5 5\" to=\"90 6 5\" dur=\"1s\"/></path></svg>",
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
        </path><g>
          <animateTransform attributeName="transform" type="rotate" from="0 32 32" to="90 32 32" dur="1s" fill="freeze"/>
          <path d="M 42 32 L 52 32" fill="lime"/>
        </g><g transform="translate(5 0)">
          <animateTransform attributeName="transform" type="translate" from="0 0" to="20 0" dur="1s" fill="freeze"/>
          <path d="M 0 40 L 10 40" fill="orange"/>
        </g></svg>)svg";
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
    REQUIRE(first->scene.layers()[0].nodes.size() == 5);
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
    CHECK(first->scene.layers()[0].nodes[3].shape.path().commands[0].p0.x == doctest::Approx(42.0f));
    CHECK(first->scene.layers()[0].nodes[3].shape.path().commands[0].p0.y == doctest::Approx(32.0f));
    CHECK(last->scene.layers()[0].nodes[3].shape.path().commands[0].p0.x == doctest::Approx(32.0f));
    CHECK(last->scene.layers()[0].nodes[3].shape.path().commands[0].p0.y == doctest::Approx(42.0f));
    CHECK(first->scene.layers()[0].nodes[4].shape.path().commands[0].p0.x == doctest::Approx(5.0f));
    CHECK(last->scene.layers()[0].nodes[4].shape.path().commands[0].p0.x == doctest::Approx(25.0f));
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
