#include "test_render_plan_decoder_test_support.hpp"

TEST_CASE("V3 shape Field2D decoding is bounded, deterministic, and fingerprinted") {

    using nlohmann::json;
    auto input = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "rect"}, {"fill", {0.8, 0.4, 0.2, 1.0}}})});
    input["layers"][0]["shape"]["fill"] = {
        {"type", "field"},
        {"field", {{"generator", "fractal"}, {"seed", 41}, {"frequency", 6.0},
            {"octaves", 5}, {"operators", json::array({
                {{"kind", "warp"}, {"amount", 0.15}},
                {{"kind", "smoothstep"}, {"edge", 0.55}, {"softness", 0.2}}})}}},
        {"field_ramp", {{"type", "linear"}, {"start", json::array({0.0, 0.5})},
            {"end", json::array({1.0, 0.5})}, {"color_stops", json::array({
                json{{"position", 0.0}, {"color", json::array({0.02, 0.01, 0.1, 1.0})}},
                json{{"position", 0.45}, {"color", json::array({0.45, 0.05, 0.8, 1.0})}},
                json{{"position", 1.0}, {"color", json::array({1.0, 0.7, 0.9, 1.0})}}})}}},
        {"field_render_scale", 4}, {"field_drift", {0.1, -0.05}}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape);
    REQUIRE(decoded->layers[0].shape->field);
    CHECK(decoded->layers[0].shape->field->generator ==
          chronon3d::graphics::Field2DGenerator::Fractal);
    CHECK(decoded->layers[0].shape->field->seed == 41);
    CHECK(decoded->layers[0].shape->field->operators.size() == 2);
    REQUIRE(decoded->layers[0].shape->field_ramp);
    CHECK(decoded->layers[0].shape->field_ramp->color_stops.size() == 3);
    CHECK(decoded->layers[0].shape->field_render_scale == 4);
    REQUIRE(decoded->layers[0].shape->field_drift);
    CHECK(decoded->layers[0].shape->field_drift->x == doctest::Approx(0.1f));
    CHECK(decoded->layers[0].shape->field_drift->y == doctest::Approx(-0.05f));

    auto different_seed = input;
    different_seed["layers"][0]["shape"]["fill"]["field"]["seed"] = 42;
    const auto other = chronon3d::render_plan::decode_render_plan(different_seed);
    REQUIRE(other);
    CHECK(decoded->content_fingerprint != other->content_fingerprint);
    auto different_ramp = input;
    different_ramp["layers"][0]["shape"]["fill"]["field_ramp"]["color_stops"][1]["color"][1] = 0.2;
    const auto other_ramp = chronon3d::render_plan::decode_render_plan(different_ramp);
    REQUIRE(other_ramp);
    CHECK(decoded->content_fingerprint != other_ramp->content_fingerprint);
    auto hdr_ramp = input;
    hdr_ramp["layers"][0]["shape"]["fill"]["field_ramp"]["color_stops"][1]["color"][0] = 4.0;
    CHECK(chronon3d::render_plan::decode_render_plan(hdr_ramp));
    hdr_ramp["layers"][0]["shape"]["fill"]["field_ramp"]["color_stops"][1]["color"][0] = 4.01;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(hdr_ramp));
    auto different_scale = input;
    different_scale["layers"][0]["shape"]["fill"]["field_render_scale"] = 2;
    const auto other_scale = chronon3d::render_plan::decode_render_plan(different_scale);
    REQUIRE(other_scale);
    CHECK(decoded->content_fingerprint != other_scale->content_fingerprint);
    auto different_drift = input;
    different_drift["layers"][0]["shape"]["fill"]["field_drift"][0] = 0.2;
    const auto other_drift = chronon3d::render_plan::decode_render_plan(different_drift);
    REQUIRE(other_drift);
    CHECK(decoded->content_fingerprint != other_drift->content_fingerprint);

    auto invalid_scale = input;
    invalid_scale["layers"][0]["shape"]["fill"]["field_render_scale"] = 3;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_scale));

    auto ramp_without_field = input;
    ramp_without_field["layers"][0]["shape"]["fill"].erase("field");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(ramp_without_field));

    auto invalid = input;
    invalid["layers"][0]["shape"]["fill"]["field"]["operators"][0]["amount"] = 2.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));

    auto burst = input;
    burst["layers"][0]["shape"]["fill"]["field"] = {
        {"generator", "burst"}, {"frequency", 1.0}, {"octaves", 8}};
    const auto decoded_burst = chronon3d::render_plan::decode_render_plan(burst);
    REQUIRE(decoded_burst);
    REQUIRE(decoded_burst->layers[0].shape->field);
    CHECK(decoded_burst->layers[0].shape->field->generator ==
          chronon3d::graphics::Field2DGenerator::Burst);
    auto invalid_burst = burst;
    invalid_burst["layers"][0]["shape"]["fill"]["field"]["octaves"] = 2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_burst));
}

TEST_CASE("V3 Field2D can drive displacement without a layer map") {
    using nlohmann::json;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["effects"] = json::array({{{"type", "displacement_map"},
        {"field", {{"generator", "simplex"}, {"seed", 12}, {"frequency", 3.0}}},
        {"intensity_x", 0.1}, {"intensity_y", -0.2}}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].map_field.has_value());
    CHECK(decoded->layers[0].effects[0].map_layer.empty());

    auto changed = input;
    changed["layers"][0]["effects"][0]["field"]["seed"] = 13;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);

    auto conflict = input;
    conflict["layers"][0]["effects"][0]["map_layer"] = "map";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(conflict));
    auto missing_source = input;
    missing_source["layers"][0]["effects"][0].erase("field");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(missing_source));
}

TEST_CASE("V3 Field2D configures the fractal-noise effect sampler") {
    using nlohmann::json;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["effects"] = json::array({{{"type", "fractal_noise"},
        {"field", {{"generator", "cellular"}, {"seed", 7}, {"frequency", 5.0}}}}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].noise_field.has_value());
    auto changed = input;
    changed["layers"][0]["effects"][0]["field"]["seed"] = 8;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);
}

TEST_CASE("V3 Field2D mask stack entries decode and affect content fingerprints") {
    using nlohmann::json;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["masks"] = json::array({{{"type", "field"}, {"mode", "add"},
        {"size", {40.0, 30.0}},
        {"field", {{"generator", "radial"}, {"seed", 8}}}}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].masks.size() == 1);
    REQUIRE(decoded->layers[0].masks[0].field);
    auto changed = input;
    changed["layers"][0]["masks"][0]["field"]["seed"] = 9;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);
}

TEST_CASE("V3 text mask sources resolve only to existing text layers") {
    using nlohmann::json;
    auto target = render_plan_test::shape_layer({{"type", "rect"}});
    target["masks"] = json::array({{{"type", "text"}, {"mode", "add"},
                                    {"source", "label"}}});
    auto label = json{{"id", "label"}, {"type", "text"}, {"text", "mask"},
        {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 32.0},
                   {"fill", "#FFFFFF"}}}};
    auto other_label = label;
    other_label["id"] = "other_label";
    const auto input = render_plan_test::base_plan({target, label, other_label});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    const auto decode_error = decoded ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded, decode_error);
    CHECK(decoded->layers[0].masks[0].text_source == "label");

    auto changed = input;
    changed["layers"][0]["masks"][0]["source"] = "other_label";
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);

    auto missing = input;
    missing["layers"][0]["masks"][0]["source"] = "missing";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(missing));

    auto wrong_type = input;
    wrong_type["layers"][1]["type"] = "color";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(wrong_type));

    auto self = input;
    self["layers"][0]["masks"][0]["source"] = self["layers"][0]["id"];
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(self));

    auto cycle = input;
    cycle["layers"][1]["masks"] = json::array({{{"type", "text"},
        {"mode", "add"}, {"source", "shape"}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(cycle));
}

TEST_CASE("V3 shape path commands decode in order and malformed commands fail closed") {
    using namespace chronon3d;
    auto layer = render_plan_test::shape_layer({
        {"type", "path"},
        {"path", nlohmann::json::array({
            {{"type", "move_to"}, {"point", {0, 0}}},
            {{"type", "line_to"}, {"point", {1, 1}}},
            {{"type", "quadratic_to"}, {"control1", {2, 3}}, {"point", {4, 4}}},
            {{"type", "cubic_to"}, {"control1", {5, 6}}, {"control2", {7, 8}}, {"point", {9, 9}}},
            {{"type", "close"}}
        })}
    });
    auto input = render_plan_test::base_plan({layer});
    auto decoded = chronon3d::render_plan::decode_render_plan(input);
    if (!decoded) INFO(decoded.error().message);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded);
    const auto& commands = decoded->layers[0].shape->path;
    REQUIRE(commands.size() == 5);
    CHECK(commands[0].type == render_plan::ShapePathCommandTypePlan::MoveTo);
    CHECK(commands[1].type == render_plan::ShapePathCommandTypePlan::LineTo);
    CHECK(commands[2].type == render_plan::ShapePathCommandTypePlan::QuadraticTo);
    CHECK(commands[3].type == render_plan::ShapePathCommandTypePlan::CubicTo);
    CHECK(commands[4].type == render_plan::ShapePathCommandTypePlan::Close);

    auto unknown = input;
    unknown["layers"][0]["shape"]["path"][1]["type"] = "arc_to";
    const auto unknown_error = render_plan::decode_render_plan(unknown);
    REQUIRE_FALSE(unknown_error);
    CHECK(unknown_error.error().message.find("layers[0].shape.path[1].type") != std::string::npos);
    CHECK(unknown_error.error().message.find("required enum") != std::string::npos);

    auto wrong_arity = input;
    wrong_arity["layers"][0]["shape"]["path"][1]["type"] = "line_to";
    wrong_arity["layers"][0]["shape"]["path"][1]["point"] = {1};
    const auto arity_error = render_plan::decode_render_plan(wrong_arity);
    REQUIRE_FALSE(arity_error);
    CHECK(arity_error.error().message.find("layers[0].shape.path[1].point") != std::string::npos);
    CHECK(arity_error.error().message.find("array has too few items") != std::string::npos);
}

TEST_CASE("V3 shape decoding rejects non-finite geometry and oversized rounded corners") {
    using namespace chronon3d;
    auto point_shape = nlohmann::json{
        {"type", "path"},
        {"path", nlohmann::json::array({
            {{"type", "move_to"}, {"point", {0.0, 0.0}}},
            {{"type", "line_to"}, {"point", {std::numeric_limits<double>::infinity(), 1.0}}}
        })}
    };
    auto point_layer = render_plan_test::shape_layer(std::move(point_shape));
    auto input = render_plan_test::base_plan({point_layer});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));

    auto rounded = render_plan_test::shape_layer({{"type", "rounded_rect"}, {"radius", 15.0}});
    input = render_plan_test::base_plan({rounded});
    REQUIRE(render_plan::decode_render_plan(input));
    input["layers"][0]["shape"]["radius"] = 15.01;
    const auto radius_error = render_plan::decode_render_plan(input);
    REQUIRE_FALSE(radius_error);
    CHECK(radius_error.error().message.find("rounded_rect radius") != std::string::npos);

    auto bad_size = render_plan_test::shape_layer({{"type", "rect"}});
    bad_size["size"][0] = std::numeric_limits<double>::infinity();
    CHECK_FALSE(render_plan::decode_render_plan(render_plan_test::base_plan({bad_size})));
}

TEST_CASE("V3 rounded_rect accepts four corner radii and rejects ambiguous or invalid values") {
    auto layer = render_plan_test::shape_layer({
        {"type", "rounded_rect"}, {"corner_radius", {14.0, 14.0, 0.0, 14.0}}});
    auto input = render_plan_test::base_plan({layer});
    auto decoded = chronon3d::render_plan::decode_render_plan(input);
    INFO("render plan decode failure: " << decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape->corner_radius);
    CHECK((*decoded->layers[0].shape->corner_radius)[2] == doctest::Approx(0.0f));

    input["layers"][0]["shape"]["radius"] = 8.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));
    input["layers"][0]["shape"].erase("radius");
    input["layers"][0]["shape"]["corner_radius"][0] = 15.01;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));
    input["layers"][0]["shape"]["corner_radius"] = {1.0, 2.0, 3.0};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));
}

TEST_CASE("Render plan shadow arrays are bounded and mutually exclusive with legacy shadow") {
    auto layer = render_plan_test::shape_layer({{"type", "rounded_rect"}});
    layer["style"] = {{"shadows", nlohmann::json::array({
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}, {"blur", 8.0}},
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}, {"blur", 24.0}}})}};
    auto plan = render_plan_test::base_plan({layer});
    auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].style);
    CHECK(decoded->layers[0].style->shadows.size() == 2);
    plan["layers"][0]["style"]["shadow"] = {{"color", "#000000"}, {"offset", {0, 0}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan["layers"][0]["style"].erase("shadow");
    plan["layers"][0]["style"]["shadows"].push_back(
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}});
    plan["layers"][0]["style"]["shadows"].push_back(
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("Render plan variables resolve typed values before validation and fingerprinting") {
    nlohmann::json layer{
        {"id", "title"}, {"type", "text"}, {"text", "Hello ${title}"},
        {"style", {{"font", "font.ttf"}, {"font_size", 20}, {"fill", "#FFFFFF"}}},
        {"animation", {{"tracks", nlohmann::json::array({
            {{"property", "opacity"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", "${score}"}}
            })}}
        })}}}
    };
    auto source = render_plan_test::base_plan({layer});
    source["schema"] = "chronon.render-plan.v2";
    source["version"] = 2;
    source["variables"] = {{"title", "Ada"}, {"score", 73}, {"unused", "x"}};
    auto resolved = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(resolved);
    CHECK(resolved->layers[0].text == "Hello Ada");
    REQUIRE(resolved->layers[0].animation);
    CHECK(resolved->layers[0].animation->tracks[0].keyframes[0].value[0] == doctest::Approx(73.0f));
    REQUIRE(resolved->warnings.size() == 1);
    auto manual = source;
    manual.erase("variables");
    manual["layers"][0]["text"] = "Hello Ada";
    manual["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["value"] = 73;
    auto hand_written = chronon3d::render_plan::decode_render_plan(manual);
    REQUIRE(hand_written);
    CHECK(resolved->content_fingerprint == hand_written->content_fingerprint);
    source["layers"][0]["text"] = "${missing}";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
    source["layers"][0]["text"] = "ok";
    source["layers"][0]["animation"]["tracks"][0]["property"] = "${score}";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
}

TEST_CASE("Render plan declares PNG sequence and GIF output formats") {
    auto source = render_plan_test::base_plan();
    source["output"]["format"] = "png_sequence";
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    CHECK(decoded->output.format == chronon3d::render_plan::OutputFormat::PngSequence);
    source["output"]["format"] = "gif";
    const auto gif = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(gif);
    CHECK(gif->output.format == chronon3d::render_plan::OutputFormat::Gif);
}

TEST_CASE("V3 brush strokes decode texture, linear color ramp, and deterministic profile") {
    auto source = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "path"},
        {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                   {{"type", "line_to"}, {"point", {20, 0}}}}},
        {"stroke", {{"width", 8}, {"color", {1, 0.5, 0.25, 1}},
            {"brush", {{"size", 12}, {"hardness", 0.8}, {"opacity", 0.7},
                {"flow", 0.5}, {"spacing", 0.2}, {"grain", 0.3},
                {"texture", {{"generator", "linear"}, {"frequency", 2.0}}},
                {"along_path_gradient", render_plan_test::gradient()}}}}}
    })});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().path << ": " << decoded.error().message);
    REQUIRE(decoded);
    const auto& profile = *decoded->layers[0].shape->stroke->brush;
    REQUIRE(profile.texture);
    CHECK(profile.texture->generator == chronon3d::graphics::Field2DGenerator::Linear);
    REQUIRE(profile.along_path_gradient);
    CHECK(profile.along_path_gradient->type == chronon3d::graphics::GradientType::Linear);
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated);
    CHECK(decoded->content_fingerprint == repeated->content_fingerprint);

    auto unsupported_gradient = source;
    unsupported_gradient["layers"][0]["shape"]["stroke"]["brush"]["along_path_gradient"]["type"] = "radial";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(unsupported_gradient));
    auto bad_pressure = source;
    bad_pressure["layers"][0]["shape"]["stroke"]["brush"]["pressure_curve"] = {{0.8, 0.4}, {0.2, 0.9}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_pressure));
    auto imported = source;
    imported["layers"][0]["shape"] = {{"type", "path"}, {"asset", "shapes/icon.svg"},
        {"stroke", {{"width", 8}, {"brush", {{"size", 12}}}}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(imported));
    auto no_base_width = source;
    no_base_width["layers"][0]["shape"]["stroke"]["width"] = 0;
    no_base_width["layers"][0]["shape"]["stroke"]["brush"]["size"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(no_base_width));

    auto brush_size_supplies_width = source;
    brush_size_supplies_width["layers"][0]["shape"]["stroke"].erase("width");
    const auto size_only = chronon3d::render_plan::decode_render_plan(brush_size_supplies_width);
    REQUIRE(size_only);
    CHECK(size_only->layers[0].shape->stroke->brush->size == doctest::Approx(12.0f));
    CHECK_FALSE(size_only->layers[0].shape->stroke->width.has_value());

    auto negative_base_width = source;
    negative_base_width["layers"][0]["shape"]["stroke"]["width"] = -1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(negative_base_width));

    auto closed_solid = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "path"},
        {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                   {{"type", "line_to"}, {"point", {20, 0}}},
                   {{"type", "line_to"}, {"point", {10, 20}}},
                   {{"type", "close"}}}},
        {"stroke", {{"width", 4}, {"taper_start", 0.4}}}
    })});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(closed_solid));
    closed_solid["layers"][0]["shape"]["stroke"].erase("taper_start");
    REQUIRE(chronon3d::render_plan::decode_render_plan(closed_solid));

    auto closed_brush = closed_solid;
    closed_brush["layers"][0]["shape"]["stroke"]["brush"] = {{"size", 8}};
    REQUIRE(chronon3d::render_plan::decode_render_plan(closed_brush));
    closed_brush["layers"][0]["shape"]["stroke"]["brush"]["taper_end"] = 0.5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(closed_brush));

    auto open_brush_with_stroke_taper = source;
    open_brush_with_stroke_taper["layers"][0]["shape"]["stroke"]["taper_start"] = 0.5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(open_brush_with_stroke_taper));
}

TEST_CASE("Backdrop blur is accepted only on shape layers with bounded parameters") {
    auto shape = render_plan_test::shape_layer({{"type", "rounded_rect"}});
    shape["style"] = {{"backdrop", {{"blur", 16.0}, {"brightness", 1.0}, {"saturation", 1.0},
        {"rim_light", 0.65}, {"refraction", 1.25}, {"surface_noise", 0.025}}}};
    auto plan = render_plan_test::base_plan({shape});
    auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].style->backdrop);
    CHECK(decoded->layers[0].style->backdrop->blur == doctest::Approx(16.0f));
    CHECK(decoded->layers[0].style->backdrop->rim_light == doctest::Approx(0.65f));
    CHECK(decoded->layers[0].style->backdrop->refraction == doctest::Approx(1.25f));
    CHECK(decoded->layers[0].style->backdrop->surface_noise == doctest::Approx(0.025f));
    for (const auto& [field, value] : std::array<std::pair<const char*, double>, 3>{{
             {"rim_light", 2.1}, {"refraction", 4.1}, {"surface_noise", 0.11}}}) {
        auto invalid = plan;
        invalid["layers"][0]["style"]["backdrop"][field] = value;
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
    }
    plan["layers"][0]["style"]["backdrop"]["blur"] = 65.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan = render_plan_test::base_plan({{{"id", "txt"}, {"type", "text"}, {"text", "x"},
        {"style", {{"font", "font.ttf"}, {"font_size", 10}, {"fill", "#FFFFFF"},
                    {"backdrop", {{"blur", 16}}}}}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("V3 color alias normalizes to shape.fill with explicit fill precedence") {
    using namespace chronon3d;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["color"] = {0.2, 0.3, 0.4, 1.0};
    auto input = render_plan_test::base_plan({layer});
    auto alias = render_plan::decode_render_plan(input);
    REQUIRE(alias);
    REQUIRE(alias->layers[0].shape->fill_color);
    CHECK(alias->layers[0].shape->fill_color->at(0) == doctest::Approx(0.2f));
    CHECK(alias->layers[0].color[0] == doctest::Approx(0.2f));

    input["layers"][0]["shape"]["fill"] = {0.8, 0.7, 0.6, 1.0};
    const auto both = render_plan::decode_render_plan(input);
    REQUIRE(both);
    CHECK(both->layers[0].shape->fill_color->at(0) == doctest::Approx(0.8f));
    CHECK(both->layers[0].color[0] == doctest::Approx(0.8f));

    const auto legacy = render_plan::decode_render_plan(render_plan_test::v2_legacy_plan());
    REQUIRE(legacy);
    CHECK(legacy->layers[0].type == render_plan::LayerType::Color);
    CHECK(legacy->layers[0].color[0] == doctest::Approx(0.1f));
    CHECK_FALSE(legacy->layers[0].shape);
}

TEST_CASE("V3 gradient decoding requires two stops and preserves spread and opacity stops") {
    using namespace chronon3d;
    const auto gradient = render_plan_test::gradient();
    auto layer = render_plan_test::shape_layer({{"type", "rect"}, {"fill", gradient}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape->gradient);
    CHECK(decoded->layers[0].shape->gradient->spread == graphics::GradientSpread::Reflect);
    CHECK(decoded->layers[0].shape->gradient->opacity_stops.size() == 2);
    auto fewer_stops = input;
    fewer_stops["layers"][0]["shape"]["fill"]["color_stops"].erase(1);
    CHECK_FALSE(render_plan::decode_render_plan(fewer_stops));

    auto solid_or_gradient = render_plan_test::shape_layer({
        {"type", "rect"}, {"fill", gradient}});
    const auto gradient_plan = render_plan::decode_render_plan(
        render_plan_test::base_plan({solid_or_gradient}));
    REQUIRE(gradient_plan);
    REQUIRE(gradient_plan->layers[0].shape->gradient);
    CHECK_FALSE(gradient_plan->layers[0].shape->fill_color);
}

TEST_CASE("RenderPlan image crop decodes normalized rectangles and rejects invalid bounds") {
    using namespace chronon3d;
    auto image = nlohmann::json{{"id", "archive"}, {"type", "image"},
        {"asset", "archive.jpg"}, {"size", {400, 220}}, {"fit", "cover"},
        {"crop", {{"enabled", true}, {"origin", {0.14, 0.08}},
                  {"size", {0.72, 0.84}}, {"flip_x", true}}}};
    auto source = render_plan_test::base_plan({image});
    const auto decoded = render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].crop);
    CHECK(decoded->layers[0].crop->enabled);
    CHECK(decoded->layers[0].crop->origin[0] == doctest::Approx(0.14f));
    CHECK(decoded->layers[0].crop->origin[1] == doctest::Approx(0.08f));
    CHECK(decoded->layers[0].crop->size[0] == doctest::Approx(0.72f));
    CHECK(decoded->layers[0].crop->size[1] == doctest::Approx(0.84f));
    CHECK(decoded->layers[0].crop->flip_x);

    auto out_of_bounds = source;
    out_of_bounds["layers"][0]["crop"]["origin"] = {0.8, 0.2};
    CHECK_FALSE(render_plan::decode_render_plan(out_of_bounds));
    auto on_text = source;
    on_text["layers"][0]["type"] = "text";
    on_text["layers"][0].erase("asset");
    on_text["layers"][0]["text"] = "archive";
    on_text["layers"][0]["crop"] = {{"origin", {0.0, 0.0}}, {"size", {1.0, 1.0}}};
    CHECK_FALSE(render_plan::decode_render_plan(on_text));
}

TEST_CASE("resolved camera samples carry DOF and physical optics") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({{
        {"id", "camera-background"}, {"type", "color"},
        {"size", {320, 180}}, {"color", {0, 0, 0, 1}}
    }}));
    source["canvas"]["duration_frames"] = 2;
    source["camera"] = {{"type", "perspective"}, {"fov_deg", 50}, {"near", 1}, {"far", 10000},
        {"zoom", 1}, {"focal_length_mm", 50}, {"sensor_width_mm", 32},
        {"lens_shift", {0.04, -0.02}}, {"f_stop", 2.0}, {"iris_blades", 6},
        {"dof", {{"enabled", true}, {"focus_distance", 1400}, {"aperture", 0.02}, {"max_blur", 24},
            {"f_stop", 2.0}, {"iris_blades", 6}, {"highlight_gain", 1.5}}}};
    source["camera_samples"] = nlohmann::json::array({
        {{"frame", 0}, {"camera", {{"focal_length_mm", 50}, {"f_stop", 2.0},
            {"dof", {{"enabled", true}, {"focus_distance", 480}, {"aperture", 0.04}, {"max_blur", 12}}}}}},
        {{"frame", 1}, {"camera", {{"focal_length_mm", 85}, {"f_stop", 2.8},
            {"sensor_width_mm", 24}, {"lens_shift", {-0.1, 0.03}},
            {"dof", {{"enabled", true}, {"focus_distance", 720}, {"aperture", 0.06}, {"max_blur", 20}}}}}}});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->camera_samples);
    CHECK(decoded->camera_samples->at(1).camera.focal_length_mm == doctest::Approx(85.f));
    CHECK(decoded->camera_samples->at(1).camera.sensor_width_mm == doctest::Approx(24.f));
    CHECK(decoded->camera_samples->at(1).camera.lens_shift[0] == doctest::Approx(-0.1f));
    CHECK(decoded->camera_samples->at(1).camera.lens_shift[1] == doctest::Approx(0.03f));
    CHECK(decoded->camera_samples->at(1).camera.dof_focus_distance == doctest::Approx(720.f));
    CHECK(decoded->camera_samples->at(1).camera.dof_aperture == doctest::Approx(0.06f));

    auto bad_order = source;
    bad_order["camera_samples"][1]["frame"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_order));
    auto bad_camera = source;
    bad_camera["camera_samples"][1]["camera"]["focal_length_mm"] = 400;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_camera));
    auto bad_sensor = source;
    bad_sensor["camera"]["sensor_width_mm"] = 0.2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_sensor));
    auto bad_shift = source;
    bad_shift["camera"]["lens_shift"] = {1.5, 0};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_shift));
    auto bad_iris = source;
    bad_iris["camera"]["dof"]["iris_blades"] = 16;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_iris));
}
