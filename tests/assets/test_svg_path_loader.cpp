#include <doctest/doctest.h>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <fstream>
using namespace chronon3d;

using namespace chronon3d::assets;

TEST_CASE("SVG path parser supports M L Z") {
    auto res = parse_svg_path_data("M 0 0 L 100 0 L 100 100 Z");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 4);
    CHECK(res.path.commands[0].type == PathCommandType::MoveTo);
    CHECK(res.path.commands[1].type == PathCommandType::LineTo);
    CHECK(res.path.commands[2].type == PathCommandType::LineTo);
    CHECK(res.path.commands[3].type == PathCommandType::Close);
    CHECK(res.path.closed);
}

TEST_CASE("SVG path parser supports H and V") {
    auto res = parse_svg_path_data("M 10 20 H 50 V 80");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 3);
    CHECK(res.path.commands[1].p0.x == doctest::Approx(50.0f));
    CHECK(res.path.commands[1].p0.y == doctest::Approx(20.0f));
    CHECK(res.path.commands[2].p0.x == doctest::Approx(50.0f));
    CHECK(res.path.commands[2].p0.y == doctest::Approx(80.0f));
}

TEST_CASE("SVG path parser supports cubic and quadratic curves") {
    auto res = parse_svg_path_data("M 0 0 C 10 0 20 10 30 30 Q 40 40 50 50");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 3);
    CHECK(res.path.commands[1].type == PathCommandType::CubicTo);
    CHECK(res.path.commands[2].type == PathCommandType::QuadraticTo);
}

TEST_CASE("SVG path parser supports relative commands") {
    auto res = parse_svg_path_data("M 10 10 l 5 0 v 5 h -5 z");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() == 5);
    CHECK(res.path.commands[1].p0.x == doctest::Approx(15.0f));
    CHECK(res.path.commands[1].p0.y == doctest::Approx(10.0f));
    CHECK(res.path.commands[2].p0.x == doctest::Approx(15.0f));
    CHECK(res.path.commands[2].p0.y == doctest::Approx(15.0f));
    CHECK(res.path.commands[3].p0.x == doctest::Approx(10.0f));
    CHECK(res.path.commands[3].p0.y == doctest::Approx(15.0f));
}

TEST_CASE("SVG path importer converts elliptical arcs to cubic commands") {
    auto res = parse_svg_path_data("M 0 0 A 10 10 0 0 1 20 20");

    REQUIRE(res.ok);
    REQUIRE(res.path.commands.size() > 1);
    CHECK(res.path.commands[0].type == PathCommandType::MoveTo);
    CHECK(res.path.commands[1].type == PathCommandType::CubicTo);
}

TEST_CASE("SVG path importer preserves relative-command policy") {
    auto res = parse_svg_path_data(
        "M 10 10 l 5 0",
        SvgPathLoadOptions{.support_relative_commands = false});

    CHECK_FALSE(res.ok);
    CHECK(res.error == "Relative SVG path commands are disabled");
}

TEST_CASE("SVG path loader parses a minimal SVG file") {
    // Write a temporary valid SVG file
    const std::string filename = "temp_test_path.svg";
    {
        std::ofstream out(filename);
        out << R"(<svg width="100" height="100">)" << "\n"
            << R"(  <path d="M 10 10 L 50 50 Z" fill="red" />)" << "\n"
            << R"(</svg>)" << "\n";
    }

    auto res = load_svg_document_file(filename);
    
    // Clean up file
    std::remove(filename.c_str());

    REQUIRE(res.ok);
    REQUIRE(res.paths.size() == 1);
    REQUIRE(res.paths[0].commands.size() == 3);
    CHECK(res.paths[0].commands[0].type == PathCommandType::MoveTo);
    CHECK(res.paths[0].commands[1].type == PathCommandType::LineTo);
    CHECK(res.paths[0].commands[2].type == PathCommandType::Close);
}

TEST_CASE("SVG document importer preserves all path elements in document order") {
    const std::string filename = "temp_test_document.svg";
    {
        std::ofstream out(filename);
        out << R"(<svg><g><path d="M 0 0 L 1 1" /></g><path d="M 2 2 L 3 3" /></svg>)";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 2);
    CHECK(document.paths[0].commands[0].p0.x == doctest::Approx(0.0f));
    CHECK(document.paths[1].commands[0].p0.x == doctest::Approx(2.0f));
}

TEST_CASE("SVG importer retains source element and group ids for plan addressing") {
    const std::string filename = "temp_test_svg_source_ids.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g id="company"><g id="revenue"><path id="arrow" d="M0 0L10 0"/></g>)svg"
            << R"svg(<use id="factory_instance" href="#factory"/></g><defs>)svg"
            << R"svg(<path id="factory" d="M1 1L2 2"/></defs></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 2);
    CHECK(result.elements[0].source_id == "arrow");
    REQUIRE(result.elements[0].group_ids.size() == 2);
    CHECK(result.elements[0].group_ids[0] == "company");
    CHECK(result.elements[0].group_ids[1] == "revenue");
    CHECK(result.elements[1].source_id == "factory");
    REQUIRE(result.elements[1].group_ids.size() == 2);
    CHECK(result.elements[1].group_ids[0] == "company");
    CHECK(result.elements[1].group_ids[1] == "factory_instance");
}

TEST_CASE("SVG importer preserves stroke cap join and dash styling") {
    const std::string filename = "temp_test_svg_stroke_style.svg";
    {
        std::ofstream file(filename);
        file << R"svg(<svg><path d="M0 0 L20 0" fill="none" fill-rule="nonzero" stroke="red"
            stroke-width="3" stroke-linecap="round" stroke-linejoin="bevel" stroke-miterlimit="6"
            stroke-dasharray="4 2 1" stroke-dashoffset="-2"/></svg>)svg";
    }
    const auto result = chronon3d::assets::load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 1);
    CHECK(result.elements[0].stroke.cap == chronon3d::LineCap::Round);
    CHECK(result.elements[0].stroke.join == chronon3d::LineJoin::Bevel);
    CHECK(result.elements[0].stroke.miter_limit == doctest::Approx(6.0f));
    REQUIRE(result.elements[0].stroke.dash_array.size() == 6);
    CHECK(result.elements[0].stroke.dash_array[0] == doctest::Approx(4.0f));
    CHECK(result.elements[0].stroke.dash_array[3] == doctest::Approx(4.0f));
    CHECK(result.elements[0].stroke.dash_offset == doctest::Approx(-2.0f));
    CHECK(result.elements[0].path.fill_rule == chronon3d::PathFillRule::NonZero);
}

TEST_CASE("SVG importer honors display and inherited visibility for geometry") {
    const std::string filename = "temp_test_svg_display_visibility.svg";
    {
        std::ofstream file(filename);
        file << R"svg(<svg><style>
          .omit { display: none; }
          .conceal { visibility: hidden; }
        </style>
        <g class="omit"><path id="display_omitted" d="M0 0L5 0"/>
          <path id="display_child_cannot_restore" style="display:inline" d="M0 1L5 1"/></g>
        <g class="conceal"><path id="visibility_inherited" d="M0 2L5 2"/>
          <path id="visibility_restored" visibility="visible" d="M0 3L5 3"/></g>
        <path id="visible_default" d="M0 4L5 4"/></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 2);
    CHECK(result.elements[0].source_id == "visibility_restored");
    CHECK(result.elements[1].source_id == "visible_default");
}

TEST_CASE("SVG text visibility can be restored by a nested tspan") {
    const std::string filename = "temp_test_svg_text_visibility.svg";
    {
        std::ofstream file(filename);
        file << R"svg(<svg><text id="caption" visibility="hidden">hidden<tspan id="shown" visibility="visible">shown</tspan></text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    CHECK(result.text_runs[0].text == "shown");
    CHECK(result.text_runs[0].source_id == "shown");
}

TEST_CASE("SVG CSS class and id rules resolve currentColor with presentation cascade") {
    const std::string filename = "temp_test_svg_css_rules.svg";
    {
        std::ofstream file(filename);
        file << R"svg(<svg><style>
          .brand { color: #5ce1d2; stroke: currentColor; stroke-width: 3; }
          path.accent { fill: currentColor; }
          #mark { fill: #f7c66a; }
        </style><g class="brand"><path id="shape" class="accent" d="M0 0 L20 0" fill="red"/>
          <path id="mark" class="accent" d="M0 8 L20 8"/></g></svg>)svg";
    }
    const auto result = chronon3d::assets::load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 2);
    REQUIRE(result.elements[0].fill.has_value());
    REQUIRE(result.elements[1].fill.has_value());
    CHECK(result.elements[0].fill->solid.r == doctest::Approx(0x5c / 255.0f).epsilon(0.01f));
    CHECK(result.elements[0].fill->solid.g == doctest::Approx(0xe1 / 255.0f).epsilon(0.01f));
    CHECK(result.elements[0].stroke.enabled);
    CHECK(result.elements[0].stroke.color.g == doctest::Approx(0xe1 / 255.0f).epsilon(0.01f));
    CHECK(result.elements[0].stroke.width == doctest::Approx(3.0f));
    CHECK(result.elements[1].fill->solid.r == doctest::Approx(0xf7 / 255.0f).epsilon(0.01f));
    CHECK(result.elements[1].fill->solid.g == doctest::Approx(0xc6 / 255.0f).epsilon(0.01f));
}

TEST_CASE("SVG document importer bakes nested group transforms into geometry") {
    const std::string filename = "temp_test_transform.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g transform="translate(10 20)"><g transform="scale(2)">)svg"
            << R"svg(<path d="M 1 2 L 3 4" /></g></g></svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 1);
    CHECK(document.paths[0].commands[0].p0.x == doctest::Approx(12.0f));
    CHECK(document.paths[0].commands[0].p0.y == doctest::Approx(24.0f));
}

TEST_CASE("SVG document importer converts primitive geometry to PathShape") {
    const std::string filename = "temp_test_primitives.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><rect x="1" y="2" width="10" height="20"/>)svg"
            << R"svg(<circle cx="5" cy="6" r="2"/>)svg"
            << R"svg(<line x1="0" y1="0" x2="3" y2="4"/>)svg"
            << R"svg(<polygon points="0,0 4,0 4,4"/></svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 4);
    CHECK(document.paths[0].commands.size() == 5);
    CHECK(document.paths[0].commands[1].p0.x == doctest::Approx(11.0f));
    CHECK(document.paths[1].commands[1].type == PathCommandType::CubicTo);
    CHECK(document.paths[2].commands.size() == 2);
    CHECK(document.paths[3].closed);
}

TEST_CASE("SVG document importer maps viewBox to the declared viewport") {
    const std::string filename = "temp_test_viewbox.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg width="200" height="100" viewBox="10 20 100 50"><path d="M 10 20 L 110 70"/></svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 1);
    CHECK(document.paths[0].commands[0].p0.x == doctest::Approx(0.0f));
    CHECK(document.paths[0].commands[0].p0.y == doctest::Approx(0.0f));
    CHECK(document.paths[0].commands[1].p0.x == doctest::Approx(200.0f));
    CHECK(document.paths[0].commands[1].p0.y == doctest::Approx(100.0f));
}

TEST_CASE("SVG text and tspan runs inherit paint, font, position, and IDs") {
    const std::string filename = "temp_test_svg_text_runs.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg width="200" height="100" viewBox="0 0 200 100">
          <g id="labels" fill="#123456" color="#123456" font-family="Chronon Sans" font-size="18">
            <text id="caption" x="20" y="40" text-anchor="middle" dominant-baseline="central">Revenue <tspan id="accent" dx="3" fill="currentColor" font-weight="700" baseline-shift="25%">growth</tspan></text>
          </g></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    CHECK(result.elements.empty());
    REQUIRE(result.text_runs.size() == 2);
    CHECK(result.text_runs[0].text == "Revenue ");
    CHECK(result.text_runs[0].source_id == "caption");
    CHECK(result.text_runs[0].group_ids == std::vector<std::string>{"labels", "caption"});
    CHECK(result.text_runs[0].x == doctest::Approx(20.0f));
    CHECK(result.text_runs[0].y == doctest::Approx(40.0f));
    CHECK(result.text_runs[0].font_size == doctest::Approx(18.0f));
    CHECK(result.text_runs[0].text_anchor == "middle");
    CHECK(result.text_runs[0].dominant_baseline == "central");
    CHECK(result.text_runs[0].baseline_shift == doctest::Approx(0.0f));
    CHECK(result.text_runs[1].text == "growth");
    CHECK(result.text_runs[1].source_id == "accent");
    CHECK(result.text_runs[1].group_ids ==
          std::vector<std::string>{"labels", "caption", "accent"});
    CHECK_FALSE(result.text_runs[1].x.has_value());
    CHECK(result.text_runs[1].dx == doctest::Approx(3.0f));
    CHECK(result.text_runs[1].font_weight == 700);
    CHECK(result.text_runs[1].fill == "#123456");
    CHECK(result.text_runs[1].dominant_baseline == "central");
    CHECK(result.text_runs[1].baseline_shift == doctest::Approx(-4.5f));
}

TEST_CASE("SVG adjacent same-style text spans share a shaping run") {
    const std::string filename = "temp_test_svg_text_shaping_chunk.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text font-family="Chronon Sans">A<tspan>B</tspan>C</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    CHECK(result.text_runs.front().text == "ABC");
}

TEST_CASE("SVG text retains cap join miter and dash stroke styling") {
    const std::string filename = "temp_test_svg_text_stroke_style.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g stroke="#123456" stroke-width="3"
          stroke-linecap="round" stroke-linejoin="bevel" stroke-miterlimit="8"
          stroke-dasharray="2 1" stroke-dashoffset="-1">
          <text id="label">dashed</text></g></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    const auto& run = result.text_runs[0];
    REQUIRE(run.stroke.has_value());
    CHECK(run.stroke_width == doctest::Approx(3.0f));
    CHECK(run.stroke_cap == LineCap::Round);
    CHECK(run.stroke_join == LineJoin::Bevel);
    CHECK(run.stroke_miter_limit == doctest::Approx(8.0f));
    REQUIRE(run.stroke_dash_array.size() == 2);
    CHECK(run.stroke_dash_array[0] == doctest::Approx(2.0f));
    CHECK(run.stroke_dash_array[1] == doctest::Approx(1.0f));
    CHECK(run.stroke_dash_offset == doctest::Approx(-1.0f));
}

TEST_CASE("SVG text paint inherits through nested tspans and resets after each chunk") {
    const std::string filename = "temp_test_svg_nested_text_paint.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text id="caption" fill="#112233" stroke="#445566"
          stroke-width="2" opacity="0.8">A<tspan id="outer" fill="#ff0000"
          stroke-opacity="0.5" opacity="0.5">B<tspan id="inner"
          font-size="10">C</tspan>D</tspan>E</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 5);
    CHECK(result.text_runs[0].text == "A");
    CHECK(result.text_runs[0].fill == "#112233");
    CHECK(result.text_runs[0].stroke == std::optional<std::string>{"#445566"});
    CHECK(result.text_runs[0].opacity == doctest::Approx(0.8f));
    CHECK(result.text_runs[1].text == "B");
    CHECK(result.text_runs[1].source_id == "outer");
    CHECK(result.text_runs[1].fill == "#FF0000");
    CHECK(result.text_runs[1].stroke_opacity == doctest::Approx(0.5f));
    CHECK(result.text_runs[1].opacity == doctest::Approx(0.4f));
    CHECK(result.text_runs[2].text == "C");
    CHECK(result.text_runs[2].source_id == "inner");
    CHECK(result.text_runs[2].fill == "#FF0000");
    CHECK(result.text_runs[2].stroke == std::optional<std::string>{"#445566"});
    CHECK(result.text_runs[2].font_size == doctest::Approx(10.0f));
    CHECK(result.text_runs[2].opacity == doctest::Approx(0.4f));
    CHECK(result.text_runs[3].text == "D");
    CHECK(result.text_runs[3].source_id == "outer");
    CHECK(result.text_runs[3].fill == "#FF0000");
    CHECK(result.text_runs[3].opacity == doctest::Approx(0.4f));
    CHECK(result.text_runs[4].text == "E");
    CHECK(result.text_runs[4].fill == "#112233");
    CHECK(result.text_runs[4].stroke_opacity == doctest::Approx(1.0f));
    CHECK(result.text_runs[4].opacity == doctest::Approx(0.8f));
}

TEST_CASE("SVG absolute text coordinates start each positioning scope once") {
    const std::string filename = "temp_test_svg_absolute_text_scope.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text x="10" y="20">A<tspan fill="#ff0000">B</tspan>C<tspan x="30" y="40">D</tspan>E</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 5);
    CHECK(result.text_runs[0].text == "A");
    CHECK(result.text_runs[0].x_explicit);
    CHECK(result.text_runs[0].y_explicit);
    CHECK(result.text_runs[1].text == "B");
    CHECK_FALSE(result.text_runs[1].x_explicit);
    CHECK_FALSE(result.text_runs[1].y_explicit);
    CHECK(result.text_runs[2].text == "C");
    CHECK_FALSE(result.text_runs[2].x_explicit);
    CHECK_FALSE(result.text_runs[2].y_explicit);
    CHECK(result.text_runs[3].text == "D");
    CHECK(result.text_runs[3].x_explicit);
    CHECK(result.text_runs[3].y_explicit);
    CHECK(result.text_runs[3].x == doctest::Approx(30.0f));
    CHECK(result.text_runs[3].y == doctest::Approx(40.0f));
    CHECK(result.text_runs[4].text == "E");
    CHECK_FALSE(result.text_runs[4].x_explicit);
    CHECK_FALSE(result.text_runs[4].y_explicit);
}

TEST_CASE("SVG text direction is inherited and restored across tspan scopes") {
    const std::string filename = "temp_test_svg_text_direction.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text direction="rtl">A<tspan direction="ltr">B</tspan>C</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 3);
    CHECK(result.text_runs[0].direction == "rtl");
    CHECK(result.text_runs[1].direction == "ltr");
    CHECK(result.text_runs[2].direction == "rtl");
}

TEST_CASE("SVG text imports bounded letter spacing, word spacing, and rotate lists") {
    const std::string filename = "temp_test_svg_text_spacing_rotate.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text font-family="Chronon Sans" letter-spacing="1.5px" word-spacing="0.25em" rotate="0, 15deg, -10">A B</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    CHECK(result.text_runs[0].letter_spacing == doctest::Approx(1.5f));
    CHECK(result.text_runs[0].word_spacing == doctest::Approx(4.0f));
    REQUIRE(result.text_runs[0].rotate_degrees.size() == 3);
    CHECK(result.text_runs[0].rotate_degrees[0] == doctest::Approx(0.0f));
    CHECK(result.text_runs[0].rotate_degrees[1] == doctest::Approx(15.0f));
    CHECK(result.text_runs[0].rotate_degrees[2] == doctest::Approx(-10.0f));
}

TEST_CASE("SVG text imports bounded textLength adjustment") {
    const std::string filename = "temp_test_svg_text_length.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text font-family="Chronon Sans" textLength="120" lengthAdjust="spacingAndGlyphs">AB</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    REQUIRE(result.text_runs[0].text_length.has_value());
    CHECK(*result.text_runs[0].text_length == doctest::Approx(120.0f));
    CHECK(result.text_runs[0].length_adjust == "spacingAndGlyphs");

    {
        std::ofstream out(filename);
        out << R"svg(<svg><text font-family="Chronon Sans" textLength="120">A<tspan>B</tspan></text></svg>)svg";
    }
    const auto nested = load_svg_document_file(filename);
    std::remove(filename.c_str());
    CHECK_FALSE(nested.ok);
}

TEST_CASE("SVG vertical writing mode fails closed") {
    const std::string filename = "temp_test_svg_vertical_text.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text writing-mode="vertical-rl">縦書き</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    CHECK_FALSE(result.ok);
}

TEST_CASE("SVG text accepts the SVG dominant-baseline identifiers") {
    const std::string filename = "temp_test_svg_dominant_baselines.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text font-family="Chronon Sans" dominant-baseline="hanging">)svg";
        for (const auto* baseline : {"auto", "use-script", "no-change", "reset-size",
                "ideographic", "alphabetic", "hanging", "mathematical", "central",
                "middle", "text-after-edge", "text-before-edge"})
            out << "<tspan dominant-baseline=\"" << baseline << "\">x</tspan>";
        out << R"svg(</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 12);
    CHECK(result.text_runs[4].dominant_baseline == "ideographic");
    CHECK(result.text_runs[2].dominant_baseline == "hanging");
    CHECK(result.text_runs[3].dominant_baseline == "hanging");
    CHECK(result.text_runs[7].dominant_baseline == "mathematical");
    CHECK(result.text_runs[10].dominant_baseline == "text-after-edge");
    CHECK(result.text_runs[11].dominant_baseline == "text-before-edge");
}

TEST_CASE("SVG root Gaussian blur lowers only supported isotropic filters") {
    const std::string filename = "temp_test_svg_root_blur.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg filter="url(#soften)"><defs>
          <filter id="soften"><feGaussianBlur stdDeviation="2, 2"/></filter>
          </defs><path d="M0 0L10 0L10 10Z"/></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.root_gaussian_blur.has_value());
    CHECK(*result.root_gaussian_blur == doctest::Approx(2.0f));

    const auto rejected = [&](const std::string& content) {
        { std::ofstream out(filename); out << content; }
        const auto loaded = load_svg_document_file(filename);
        std::remove(filename.c_str());
        return loaded;
    };
    const auto scaled = rejected(
        R"svg(<svg width="200" height="200" viewBox="0 0 100 100" filter="url(#f)"><defs><filter id="f"><feGaussianBlur stdDeviation="2"/></filter></defs><path d="M0 0L10 0L10 10Z"/></svg>)svg");
    REQUIRE(scaled.ok);
    REQUIRE(scaled.root_gaussian_blur.has_value());
    CHECK(*scaled.root_gaussian_blur == doctest::Approx(4.0f));
    const auto anisotropic = rejected(
        R"svg(<svg filter="url(#f)"><defs><filter id="f"><feGaussianBlur stdDeviation="2 3"/></filter></defs><path d="M0 0L1 1"/></svg>)svg");
    CHECK_FALSE(anisotropic.ok);
    const auto non_root = rejected(
        R"svg(<svg><defs><filter id="f"><feGaussianBlur stdDeviation="2"/></filter></defs><path filter="url(#f)" d="M0 0L1 1"/></svg>)svg");
    CHECK(non_root.ok);
    CHECK(non_root.root_gaussian_blur.has_value());
    CHECK(*non_root.root_gaussian_blur == doctest::Approx(2.0f));
    const auto arbitrary_graph = rejected(
        R"svg(<svg filter="url(#f)"><defs><filter id="f"><feOffset dx="2"/></filter></defs><path d="M0 0L1 1"/></svg>)svg");
    CHECK_FALSE(arbitrary_graph.ok);
}

TEST_CASE("SVG textPath preserves local path target and start offset") {
    const std::string filename = "temp_test_svg_text_path.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><defs><path id="route" d="M 0 20 L 100 20"/></defs>
          <text id="caption" font-family="Chronon Sans" font-size="18">
            <textPath id="caption_on_route" href="#route" startOffset="15%">Company growth</textPath>
          </text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    CHECK(result.text_runs[0].text == "Company growth");
    CHECK(result.text_runs[0].source_id == "caption_on_route");
    REQUIRE(result.text_runs[0].text_path_id.has_value());
    CHECK(*result.text_runs[0].text_path_id == "route");
    CHECK(result.text_runs[0].text_path_start_offset == doctest::Approx(15.0f));
    CHECK(result.text_runs[0].text_path_start_offset_percent);
}

TEST_CASE("SVG text resolves objectBoundingBox linear gradient paint") {
    const std::string filename = "temp_test_svg_text_gradient.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><defs><linearGradient id="ink"><stop offset="0" stop-color="#FF0000"/><stop offset="1" stop-color="#0000FF"/></linearGradient></defs><text font-family="Chronon Sans" fill="url(#ink)">Gradient</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    REQUIRE(result.text_runs[0].fill_gradient.has_value());
    CHECK(result.text_runs[0].fill_gradient->type == graphics::GradientType::Linear);
    REQUIRE(result.text_runs[0].fill_gradient->color_stops.size() == 2);
    CHECK(result.text_runs[0].fill_gradient->color_stops.front().color.r == doctest::Approx(1.0f));
    CHECK(result.text_runs[0].fill_gradient->color_stops.back().color.b == doctest::Approx(1.0f));
}

TEST_CASE("SVG textPath retains relative per-character coordinate lists") {
    const std::string filename = "temp_test_svg_text_path_coordinates.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><defs><path id="route" d="M 10 30 L 180 30"/></defs><text font-family="Chronon Sans"><textPath href="#route" startOffset="5" dx="0 8 0" dy="0 4 0">ABC</textPath></text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    REQUIRE(result.text_runs.front().text_path_id.has_value());
    CHECK(result.text_runs.front().dx_coordinates == (std::vector<float>{0.0f, 8.0f, 0.0f}));
    CHECK(result.text_runs.front().dy_coordinates == (std::vector<float>{0.0f, 4.0f, 0.0f}));
}

TEST_CASE("SVG rotated text coordinate lists preserve both transformed axes") {
    const std::string filename = "temp_test_svg_text_rotated_coordinates.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text font-family="Chronon Sans" transform="rotate(90)" x="10 20" y="5 10" dx="1 2" dy="3 4">AB</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    REQUIRE(result.text_runs.front().transformed_position_coordinates.size() == 2);
    REQUIRE(result.text_runs.front().transformed_delta_coordinates.size() == 2);
    CHECK(result.text_runs.front().transformed_position_coordinates[0][0] == doctest::Approx(-5.0f));
    CHECK(result.text_runs.front().transformed_position_coordinates[0][1] == doctest::Approx(10.0f));
    CHECK(result.text_runs.front().transformed_delta_coordinates[0][0] == doctest::Approx(-3.0f));
    CHECK(result.text_runs.front().transformed_delta_coordinates[0][1] == doctest::Approx(1.0f));
}

TEST_CASE("SVG document importer converts rounded rectangles") {
    const std::string filename = "temp_test_rounded_rect.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><rect x="0" y="0" width="20" height="10" rx="3"/></svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 1);
    REQUIRE(document.paths[0].commands.size() == 14);
    CHECK(document.paths[0].commands[1].type == PathCommandType::LineTo);
    CHECK(document.paths[0].commands[2].type == PathCommandType::CubicTo);
    CHECK(document.paths[0].closed);
}

TEST_CASE("SVG document importer parses skewX and skewY transforms") {
    const std::string filename = "temp_test_skew.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g transform="skewX(45)"><path d="M 0 0 L 0 10" /></g>)svg"
            << R"svg(<g transform="skewY(45)"><path d="M 10 0 L 10 0" /></g></svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 2);
    // skewX(45 deg): x' = x + y * tan(45) = 0 + 10 * 1 = 10, y' = 10
    CHECK(document.paths[0].commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(document.paths[0].commands[1].p0.y == doctest::Approx(10.0f));
    // skewY(45 deg): x' = 10, y' = y + x * tan(45) = 0 + 10 * 1 = 10
    CHECK(document.paths[1].commands[0].p0.x == doctest::Approx(10.0f));
    CHECK(document.paths[1].commands[0].p0.y == doctest::Approx(10.0f));
}

TEST_CASE("SVG document importer parses rotate transform with pivot") {
    const std::string filename = "temp_test_rotate_pivot.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g transform="rotate(90 50 50)"><path d="M 50 0 L 50 50" /></g></svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 1);
    // Point (50, 0) rotated 90 deg around (50, 50) -> (100, 50)
    CHECK(document.paths[0].commands[0].p0.x == doctest::Approx(100.0f));
    CHECK(document.paths[0].commands[0].p0.y == doctest::Approx(50.0f));
    // Pivot (50, 50) remains (50, 50)
    CHECK(document.paths[0].commands[1].p0.x == doctest::Approx(50.0f));
    CHECK(document.paths[0].commands[1].p0.y == doctest::Approx(50.0f));
}

TEST_CASE("SVG document importer differentiates circle and ellipse") {
    const std::string filename = "temp_test_circle_ellipse.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg>)svg"
            << R"svg(<circle cx="10" cy="20" r="5"/>)svg"
            << R"svg(<ellipse cx="30" cy="40" rx="15" ry="8"/>)svg"
            << R"svg(</svg>)svg";
    }

    auto document = load_svg_document_file(filename);
    std::remove(filename.c_str());

    REQUIRE(document.ok);
    REQUIRE(document.paths.size() == 2);
    CHECK(document.paths[0].commands[0].p0.x == doctest::Approx(5.0f));
    CHECK(document.paths[0].commands[0].p0.y == doctest::Approx(20.0f));
    CHECK(document.paths[1].commands[0].p0.x == doctest::Approx(15.0f));
    CHECK(document.paths[1].commands[0].p0.y == doctest::Approx(40.0f));
}

TEST_CASE("SVG document importer handles invalid SVG and malformed input") {
    // 1. Missing file
    auto non_existent = load_svg_document_file("non_existent_file_path_12345.svg");
    CHECK_FALSE(non_existent.ok);

    // 2. Malformed XML syntax
    const std::string malformed_file = "temp_test_malformed.svg";
    {
        std::ofstream out(malformed_file);
        out << "<svg><path d=\"M 0 0\" unclosed_tag";
    }
    auto malformed = load_svg_document_file(malformed_file);
    std::remove(malformed_file.c_str());
    CHECK_FALSE(malformed.ok);

    // 3. Unsupported raster image references are rejected explicitly.
    const std::string empty_svg = "temp_test_empty.svg";
    {
        std::ofstream out(empty_svg);
        out << R"(<svg><image href="external.png"/></svg>)";
    }
    auto empty = load_svg_document_file(empty_svg);
    std::remove(empty_svg.c_str());
    CHECK_FALSE(empty.ok);
    CHECK(empty.error.find("image") != std::string::npos);

    // 4. Invalid path data within SVG
    const std::string invalid_path_svg = "temp_test_invalid_path.svg";
    {
        std::ofstream out(invalid_path_svg);
        out << R"(<svg><path d="INVALID_PATH_DATA 123 456" /></svg>)";
    }
    auto invalid_path = load_svg_document_file(invalid_path_svg);
    std::remove(invalid_path_svg.c_str());
    CHECK_FALSE(invalid_path.ok);
}

TEST_CASE("SVG styles parse colors, inline declarations and group inheritance") {
    const std::string filename = "temp_test_styled.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g fill="#123456" color="#123456" style="stroke:rgb(255,0,128);stroke-width:2;opacity:0.5">)svg"
            << R"svg(<path d="M0 0L1 1" fill="#abc"/>)svg"
            << R"svg(<path d="M0 0L1 1" fill="currentColor"/>)svg"
            << R"svg(<path d="M0 0L1 1" fill="none"/>)svg"
            << R"svg(<path d="M0 0L1 1" style="fill:#a1b2c3;fill-opacity:0.25"/>)svg"
            << R"svg(</g></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 4);
    CHECK(result.elements[0].fill->solid.r == doctest::Approx(170.0f / 255.0f));
    CHECK(result.elements[1].fill->solid.g == doctest::Approx(0x34 / 255.0f));
    CHECK_FALSE(result.elements[2].fill.has_value());
    CHECK(result.elements[3].fill->solid.r == doctest::Approx(0xa1 / 255.0f));
    CHECK(result.elements[0].stroke.enabled);
    CHECK(result.elements[0].stroke.width == doctest::Approx(2.0f));
    // ADR-035: group opacity is no longer flattened into element opacity,
    // it is carried as the innermost isolated-group factor.
    CHECK(result.elements[0].opacity == doctest::Approx(1.0f));
    REQUIRE(result.elements[0].group.has_value());
    CHECK(result.elements[0].group->id != 0u);
    CHECK(result.elements[0].group->opacity == doctest::Approx(0.5f));
    // All four paths share the same enclosing isolated group.
    for (std::size_t index = 1; index < result.elements.size(); ++index) {
        REQUIRE(result.elements[index].group.has_value());
        CHECK(result.elements[index].group->id == result.elements[0].group->id);
        CHECK(result.elements[index].group->opacity == doctest::Approx(0.5f));
        CHECK(result.elements[index].opacity == doctest::Approx(1.0f));
    }
}

TEST_CASE("SVG group opacity over element opacity preserves both factors") {
    const std::string filename = "temp_test_group_opacity.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g opacity="0.5">)svg"
            << R"svg(<path d="M0 0L1 1" opacity="0.75"/>)svg"
            << R"svg(</g></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 1);
    CHECK(result.elements[0].opacity == doctest::Approx(0.75f));
    REQUIRE(result.elements[0].group.has_value());
    CHECK(result.elements[0].group->id != 0u);
    CHECK(result.elements[0].group->opacity == doctest::Approx(0.5f));
}

TEST_CASE("SVG group opacity animation stays on the isolated group scope") {
    const std::string filename = "temp_test_group_animated_opacity.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><g><animate attributeName="opacity" from="1" to="0" dur="1s" fill="freeze"/><path d="M0 0L10 0L10 10Z"/></g></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 1);
    REQUIRE(result.elements.front().groups.size() == 1);
    CHECK(result.elements.front().opacity == doctest::Approx(1.0f));
    REQUIRE(result.elements.front().groups.front().opacity_animation.has_value());
    CHECK(result.elements.front().groups.front().opacity_animation->kind == assets::SvgAnimationKind::Opacity);
    CHECK(result.elements.front().groups.front().opacity_animation->to[0] == doctest::Approx(0.0f));
}

TEST_CASE("SVG nested isolated groups preserve their ordered scope") {
    const std::string filename = "temp_test_nested_isolated_groups.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg xmlns="http://www.w3.org/2000/svg"><g opacity="0.5">)svg"
            << R"svg(<g opacity="0.4">)svg"
            << R"svg(<path d="M0 0L1 1"/>)svg"
            << R"svg(</g></g></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 1);
    const auto& element = result.elements[0];
    REQUIRE(element.groups.size() == 2);
    CHECK(element.groups[0].id != 0u);
    CHECK(element.groups[1].id != 0u);
    CHECK(element.groups[0].id != element.groups[1].id);
    CHECK(element.groups[0].opacity == doctest::Approx(0.5f));
    CHECK(element.groups[1].opacity == doctest::Approx(0.4f));
    REQUIRE(element.group.has_value());
    CHECK(element.group->id == element.groups.back().id);
}

TEST_CASE("SVG clip scopes stay distinct for grouped sibling paths") {
    const std::string filename = "temp_test_svg_distinct_clip_scopes.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="outer"><rect x="0" y="0" width="20" height="20"/></clipPath>
            <clipPath id="inner"><rect x="5" y="5" width="10" height="10"/></clipPath>
          </defs>
          <g clip-path="url(#outer)">
            <path d="M0 0H20V20H0Z" fill="red"/>
            <path clip-path="url(#inner)" d="M0 0H20V20H0Z" fill="blue"/>
          </g>
        </svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 2);
    REQUIRE(result.elements[0].vector_masks.size() == 1);
    REQUIRE(result.elements[1].vector_masks.size() == 2);
    CHECK(result.elements[0].vector_masks[0].commands.size() > 0);
    CHECK(result.elements[1].vector_masks[0].commands.size() > 0);
    CHECK(result.elements[1].vector_masks[1].commands.size() > 0);
}

TEST_CASE("SVG clip-rule maps to the canonical clip fill rule") {
    const std::string filename = "temp_test_svg_clip_rule.svg";
    const auto load = [&](const std::string& definition) {
        {
            std::ofstream out(filename);
            out << "<svg clip-path=\"url(#clip)\"><defs>" << definition
                << "</defs><path d=\"M0 0H20V20H0Z\"/></svg>";
        }
        const auto result = load_svg_document_file(filename);
        std::remove(filename.c_str());
        return result;
    };

    const auto even_odd = load(
        R"svg(<clipPath id="clip" clip-rule="evenodd"><path d="M0 0H20V20H0Z M5 5H15V15H5Z"/></clipPath>)svg");
    REQUIRE(even_odd.ok);
    REQUIRE(even_odd.vector_mask.has_value());
    CHECK(even_odd.vector_mask->fill_rule == PathFillRule::EvenOdd);

    const auto inherited = load(
        R"svg(<clipPath id="clip" style="clip-rule:evenodd"><g><path d="M0 0H20V20H0Z"/></g></clipPath>)svg");
    REQUIRE(inherited.ok);
    REQUIRE(inherited.vector_mask.has_value());
    CHECK(inherited.vector_mask->fill_rule == PathFillRule::EvenOdd);

    const auto inconsistent = load(
        R"svg(<clipPath id="clip"><path clip-rule="evenodd" d="M0 0H10V10H0Z"/><path clip-rule="nonzero" d="M10 10H20V20H10Z"/></clipPath>)svg");
    CHECK_FALSE(inconsistent.ok);
}

TEST_CASE("SVG tspan text-anchor creates a distinct chunk scope") {
    const std::string filename = "temp_test_svg_text_anchor_scope.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><text id="caption"><tspan>left</tspan><tspan text-anchor="end">right</tspan></text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 2);
    CHECK(result.text_runs[0].text_anchor == "start");
    CHECK(result.text_runs[1].text_anchor == "end");
    CHECK_FALSE(result.text_runs[0].text_anchor_scope_id.empty());
    CHECK(result.text_runs[0].text_anchor_scope_id != result.text_runs[1].text_anchor_scope_id);
}

TEST_CASE("SVG text coordinate lists retain bounded x/y/dx/dy entries") {
    const std::string filename = "temp_test_svg_text_coordinate_lists.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg width="100" height="100"><text id="coords" x="10, 20 30" y="40 50 60" dx="1 2" dy="3,4" fill-opacity="0.5" stroke="#123456" stroke-width="3" stroke-opacity="0.25">ABC</text></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.text_runs.size() == 1);
    const auto& run = result.text_runs.front();
    CHECK(run.x_coordinates == std::vector<float>{10.0f, 20.0f, 30.0f});
    CHECK(run.y_coordinates == std::vector<float>{40.0f, 50.0f, 60.0f});
    CHECK(run.dx_coordinates == std::vector<float>{1.0f, 2.0f});
    CHECK(run.dy_coordinates == std::vector<float>{3.0f, 4.0f});
    CHECK(run.fill_opacity == doctest::Approx(0.5f));
    REQUIRE(run.stroke.has_value());
    CHECK(*run.stroke == "#123456");
    CHECK(run.stroke_width == doctest::Approx(3.0f));
    CHECK(run.stroke_opacity == doctest::Approx(0.25f));

    const std::string rotated_filename = "temp_test_svg_text_coordinate_lists_rotated.svg";
    {
        std::ofstream out(rotated_filename);
        out << R"svg(<svg><g transform="rotate(30)"><text x="10 20">AB</text></g></svg>)svg";
    }
    const auto rotated = load_svg_document_file(rotated_filename);
    std::remove(rotated_filename.c_str());
    CHECK_FALSE(rotated.ok);
    CHECK(rotated.error.find("needs paired x/y coordinate lists") != std::string::npos);
}

TEST_CASE("SVG importer rejects unsupported render tags explicitly") {
    const std::string unsupported_filename = "temp_test_unsupported.svg";
    { std::ofstream out(unsupported_filename); out << "<svg><image/></svg>"; }
    const auto unsupported = load_svg_document_file(unsupported_filename);
    std::remove(unsupported_filename.c_str());
    CHECK_FALSE(unsupported.ok);
    CHECK(unsupported.error.find("non supportata: <image>") != std::string::npos);

    const std::string malformed_filename = "temp_test_malformed_text_path.svg";
    { std::ofstream out(malformed_filename); out << "<svg><text><textPath/></text></svg>"; }
    const auto malformed_text_path = load_svg_document_file(malformed_filename);
    std::remove(malformed_filename.c_str());
    CHECK_FALSE(malformed_text_path.ok);
    CHECK(malformed_text_path.error.find("requires a local href") != std::string::npos);

    const std::string unsupported_method_filename = "temp_test_text_path_method.svg";
    { std::ofstream out(unsupported_method_filename); out << "<svg><text><textPath href='#p' method='stretch'>x</textPath></text></svg>"; }
    const auto unsupported_method = load_svg_document_file(unsupported_method_filename);
    std::remove(unsupported_method_filename.c_str());
    CHECK_FALSE(unsupported_method.ok);
    CHECK(unsupported_method.error.find("supports only method=align") != std::string::npos);
    const std::string filename = "temp_test_invalid_svg_use.svg";
    { std::ofstream out(filename); out << "<svg><use/></svg>"; }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("local #id references") != std::string::npos);
}

TEST_CASE("SVG importer rejects excessive XML nodes before geometry lowering") {
    const std::string filename = "temp_test_svg_node_limit.svg";
    {
        std::ofstream out(filename);
        out << "<svg>";
        for (std::size_t i = 0; i < 100001; ++i) out << "<g/>";
        out << "</svg>";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("node count exceeds 100000") != std::string::npos);
}

TEST_CASE("SVG imported coordinates are quantized to four decimal places") {
    const std::string filename = "temp_test_svg_quantize.svg";
    { std::ofstream out(filename); out << R"(<svg><path d="M 0.123456 0.987654 L 1 1"/></svg>)"; }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    REQUIRE(result.ok);
    REQUIRE(result.paths.size() == 1);
    CHECK(result.paths[0].commands[0].p0.x == doctest::Approx(0.1235f));
    CHECK(result.paths[0].commands[0].p0.y == doctest::Approx(0.9877f));
}

TEST_CASE("SVG importer enforces byte, command, coordinate, and malformed XML limits") {
    const auto run = [](const std::string& filename, const std::string& contents) {
        { std::ofstream out(filename, std::ios::binary); out << contents; }
        const auto result = load_svg_document_file(filename);
        std::remove(filename.c_str());
        return result;
    };
    const auto too_large = run("temp_test_svg_bytes.svg",
        "<svg>" + std::string(8U * 1024U * 1024U, ' ') + "</svg>");
    CHECK_FALSE(too_large.ok);
    CHECK(too_large.error.find("8 MiB") != std::string::npos);

    std::string path_data = "M0 0";
    for (std::size_t i = 0; i < 65537; ++i) path_data += " L1 1";
    const auto too_many_commands = run("temp_test_svg_commands.svg",
        "<svg><path d=\"" + path_data + "\"/></svg>");
    CHECK_FALSE(too_many_commands.ok);
    CHECK(too_many_commands.error.find("command count exceeds 65536") != std::string::npos);

    const auto out_of_bounds = run("temp_test_svg_bounds.svg",
        R"svg(<svg><path transform="translate(200001 0)" d="M0 0L1 1"/></svg>)svg");
    CHECK_FALSE(out_of_bounds.ok);
    CHECK(out_of_bounds.error.find("coordinate exceeds") != std::string::npos);

    const auto malformed = run("temp_test_svg_malformed.svg", "<svg><path></svg>");
    CHECK_FALSE(malformed.ok);
    CHECK_FALSE(malformed.error.empty());
}
