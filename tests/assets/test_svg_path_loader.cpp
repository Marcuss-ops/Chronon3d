#include <doctest/doctest.h>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <fstream>
#include <iostream>
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

    // 3. SVG without geometry
    const std::string empty_svg = "temp_test_empty.svg";
    {
        std::ofstream out(empty_svg);
        out << "<svg><defs><style>.cls{fill:red;}</style></defs></svg>";
    }
    auto empty = load_svg_document_file(empty_svg);
    std::remove(empty_svg.c_str());
    CHECK_FALSE(empty.ok);
    CHECK(empty.error.find("CSS esterni non supportata: <style>") != std::string::npos);

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
        out << R"svg(<svg><g fill="#123456" style="stroke:rgb(255,0,128);stroke-width:2;opacity:0.5">)svg"
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
    CHECK(result.elements[0].opacity == doctest::Approx(0.5f));
}

TEST_CASE("SVG advanced stroke parses cap, join, dash array and offset") {
    const std::string filename = "temp_test_advanced_stroke.svg";
    {
        std::ofstream out(filename);
        out << R"svg(<svg><path d="M0 0L10 0L10 10"
          style="stroke:red;stroke-width:3;stroke-linecap:round;stroke-linejoin:bevel;
          stroke-dasharray:4 2;stroke-dashoffset:1.5"/></svg>)svg";
    }
    const auto result = load_svg_document_file(filename);
    std::remove(filename.c_str());
    if (!result.ok) std::cout << "loader error: " << result.error << std::endl;
    REQUIRE(result.ok);
    REQUIRE(result.elements.size() == 1);
    CHECK(result.elements[0].stroke.cap == LineCap::Round);
    CHECK(result.elements[0].stroke.join == LineJoin::Bevel);
    REQUIRE(result.elements[0].stroke.dash_array.size() == 2);
    CHECK(result.elements[0].stroke.dash_array[0] == doctest::Approx(4.0f));
    CHECK(result.elements[0].stroke.dash_array[1] == doctest::Approx(2.0f));
    CHECK(result.elements[0].stroke.dash_offset == doctest::Approx(1.5f));
}

TEST_CASE("SVG advanced stroke rejects invalid enum values and dash lists") {
    for (const auto& fragment : {
             "stroke-linecap:square-x", "stroke-linejoin:fancy",
             "stroke-miterlimit:0", "stroke-dasharray:3 0 4 -1",
             "stroke-dasharray:none-such", "stroke-dashoffset:-2"}) {
        const std::string filename = "temp_test_bad_stroke.svg";
        {
            std::ofstream out(filename);
            out << "<svg><path d=\"M0 0L1 1\" style=\"stroke:red;" << fragment
                << "\"/></svg>";
        }
        const auto result = load_svg_document_file(filename);
        std::remove(filename.c_str());
        CHECK_FALSE(result.ok);
        CHECK(result.error.find("SVG ") != std::string::npos);
    }
}

TEST_CASE("SVG importer rejects unsupported render tags explicitly") {
    for (const auto* tag : {"text", "image"}) {
        const std::string filename = "temp_test_unsupported.svg";
        { std::ofstream out(filename); out << "<svg><" << tag << "/></svg>"; }
        const auto result = load_svg_document_file(filename);
        std::remove(filename.c_str());
        CHECK_FALSE(result.ok);
        CHECK(result.error.find(std::string("non supportata: <") + tag + ">") != std::string::npos);
    }
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
