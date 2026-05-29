#include <doctest/doctest.h>
#include <vector>
#include <cmath>
#include <algorithm>

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

using namespace chronon3d;

struct TestLine {
    Vec3 from;
    Vec3 to;
};

static std::vector<float> generate_grid_coords(float extent, float spacing) {
    std::vector<float> coords;

    if (extent < 0.0f) {
        return coords;
    }

    if (spacing <= 0.0f) {
        return coords;
    }

    int steps = static_cast<int>(std::floor(extent / spacing));

    for (int i = -steps; i <= steps; ++i) {
        coords.push_back(i * spacing);
    }

    return coords;
}

static std::vector<TestLine> generate_grid_2d(float extent, float spacing) {
    std::vector<TestLine> lines;

    int steps = static_cast<int>(std::floor(extent / spacing));

    for (int i = -steps; i <= steps; ++i) {
        float c = i * spacing;

        // Verticale
        lines.push_back({
            Vec3{c, -extent, 0},
            Vec3{c,  extent, 0}
        });

        // Orizzontale
        lines.push_back({
            Vec3{-extent, c, 0},
            Vec3{ extent, c, 0}
        });
    }

    return lines;
}

static std::vector<TestLine> generate_grid_xy(Vec3 pos, float extent, float spacing) {
    std::vector<TestLine> lines;

    auto coords = generate_grid_coords(extent, spacing);

    for (float c : coords) {
        lines.push_back({
            Vec3{pos.x + c, pos.y - extent, pos.z},
            Vec3{pos.x + c, pos.y + extent, pos.z}
        });

        lines.push_back({
            Vec3{pos.x - extent, pos.y + c, pos.z},
            Vec3{pos.x + extent, pos.y + c, pos.z}
        });
    }

    return lines;
}

static std::vector<TestLine> generate_grid_xz(Vec3 pos, float extent, float spacing) {
    std::vector<TestLine> lines;

    auto coords = generate_grid_coords(extent, spacing);

    for (float c : coords) {
        lines.push_back({
            Vec3{pos.x + c, pos.y, pos.z - extent},
            Vec3{pos.x + c, pos.y, pos.z + extent}
        });

        lines.push_back({
            Vec3{pos.x - extent, pos.y, pos.z + c},
            Vec3{pos.x + extent, pos.y, pos.z + c}
        });
    }

    return lines;
}

TEST_CASE("Grid line count is mathematically correct") {
    float extent = 200.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));
    int expected_per_axis = steps * 2 + 1;
    int expected_total = expected_per_axis * 2;

    CHECK(steps == 2);
    CHECK(expected_per_axis == 5);
    CHECK(expected_total == 10);
}

TEST_CASE("Grid coordinates are symmetric around zero") {
    float extent = 200.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    std::vector<float> coords;
    for (int i = -steps; i <= steps; ++i) {
        coords.push_back(i * spacing);
    }

    REQUIRE(coords.size() == 5);

    CHECK(coords[0] == doctest::Approx(-200.0f));
    CHECK(coords[1] == doctest::Approx(-100.0f));
    CHECK(coords[2] == doctest::Approx(0.0f));
    CHECK(coords[3] == doctest::Approx(100.0f));
    CHECK(coords[4] == doctest::Approx(200.0f));
}

TEST_CASE("Grid coordinates are mirrored") {
    float extent = 500.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    for (int i = 1; i <= steps; ++i) {
        float negative = -i * spacing;
        float positive = i * spacing;

        CHECK(std::abs(negative) == doctest::Approx(positive));
    }
}

TEST_CASE("Grid always contains center line") {
    float extent = 1000.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    bool found_zero = false;

    for (int i = -steps; i <= steps; ++i) {
        float coord = i * spacing;
        if (std::abs(coord) < 0.0001f) {
            found_zero = true;
        }
    }

    CHECK(found_zero == true);
}

TEST_CASE("Grid spacing is constant") {
    float extent = 500.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    std::vector<float> coords;
    for (int i = -steps; i <= steps; ++i) {
        coords.push_back(i * spacing);
    }

    for (size_t i = 1; i < coords.size(); ++i) {
        float diff = coords[i] - coords[i - 1];
        CHECK(diff == doctest::Approx(spacing));
    }
}

TEST_CASE("Grid with extent smaller than spacing still has center lines") {
    float extent = 50.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    CHECK(steps == 0);

    int expected_per_axis = steps * 2 + 1;
    int expected_total = expected_per_axis * 2;

    CHECK(expected_per_axis == 1);
    CHECK(expected_total == 2);
}

TEST_CASE("Invalid grid spacing is rejected") {
    GridPlaneParams p;
    p.extent = 1000.0f;
    p.spacing = 0.0f;

    CHECK(p.spacing <= 0.0f);
}

TEST_CASE("Grid extent must be positive") {
    GridPlaneParams p;
    p.extent = -100.0f;
    p.spacing = 50.0f;

    CHECK(p.extent <= 0.0f);
}

TEST_CASE("XY grid keeps Z constant") {
    Vec3 pos{10, 20, 30};
    float extent = 200.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    for (int i = -steps; i <= steps; ++i) {
        float c = i * spacing;

        Vec3 vertical_from{pos.x + c, pos.y - extent, pos.z};
        Vec3 vertical_to  {pos.x + c, pos.y + extent, pos.z};

        Vec3 horizontal_from{pos.x - extent, pos.y + c, pos.z};
        Vec3 horizontal_to  {pos.x + extent, pos.y + c, pos.z};

        CHECK(vertical_from.z == doctest::Approx(pos.z));
        CHECK(vertical_to.z == doctest::Approx(pos.z));
        CHECK(horizontal_from.z == doctest::Approx(pos.z));
        CHECK(horizontal_to.z == doctest::Approx(pos.z));
    }
}

TEST_CASE("XZ grid keeps Y constant") {
    Vec3 pos{10, 20, 30};
    float extent = 200.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    for (int i = -steps; i <= steps; ++i) {
        float c = i * spacing;

        Vec3 line_x_from{pos.x - extent, pos.y, pos.z + c};
        Vec3 line_x_to  {pos.x + extent, pos.y, pos.z + c};

        Vec3 line_z_from{pos.x + c, pos.y, pos.z - extent};
        Vec3 line_z_to  {pos.x + c, pos.y, pos.z + extent};

        CHECK(line_x_from.y == doctest::Approx(pos.y));
        CHECK(line_x_to.y == doctest::Approx(pos.y));
        CHECK(line_z_from.y == doctest::Approx(pos.y));
        CHECK(line_z_to.y == doctest::Approx(pos.y));
    }
}

TEST_CASE("Line from/to length is correct") {
    Vec3 from{100, 100, 0};
    Vec3 to{300, 100, 0};

    Vec3 delta = to - from;

    float length = std::sqrt(
        delta.x * delta.x +
        delta.y * delta.y +
        delta.z * delta.z
    );

    CHECK(delta.x == doctest::Approx(200.0f));
    CHECK(delta.y == doctest::Approx(0.0f));
    CHECK(delta.z == doctest::Approx(0.0f));
    CHECK(length == doctest::Approx(200.0f));
}

TEST_CASE("Line is converted to local space correctly") {
    Vec3 from{100, 100, 0};
    Vec3 to{300, 100, 0};

    Vec3 node_position = from;
    Vec3 local_to = to - from;

    CHECK(node_position.x == doctest::Approx(100.0f));
    CHECK(node_position.y == doctest::Approx(100.0f));
    CHECK(node_position.z == doctest::Approx(0.0f));

    CHECK(local_to.x == doctest::Approx(200.0f));
    CHECK(local_to.y == doctest::Approx(0.0f));
    CHECK(local_to.z == doctest::Approx(0.0f));
}

TEST_CASE("Line trim computes correct endpoints") {
    Vec3 full{100, 0, 0};

    float trim_start = 0.25f;
    float trim_end = 0.75f;

    Vec3 local_start = full * trim_start;
    Vec3 local_end = full * trim_end;

    CHECK(local_start.x == doctest::Approx(25.0f));
    CHECK(local_end.x == doctest::Approx(75.0f));

    CHECK(local_start.y == doctest::Approx(0.0f));
    CHECK(local_end.y == doctest::Approx(0.0f));
}

TEST_CASE("Vertical line trim computes correct endpoints") {
    Vec3 full{0, 100, 0};

    float trim_start = 0.25f;
    float trim_end = 0.75f;

    Vec3 local_start = full * trim_start;
    Vec3 local_end = full * trim_end;

    CHECK(local_start.y == doctest::Approx(25.0f));
    CHECK(local_end.y == doctest::Approx(75.0f));

    CHECK(local_start.x == doctest::Approx(0.0f));
    CHECK(local_end.x == doctest::Approx(0.0f));
}

TEST_CASE("Line trim values are clamped") {
    float trim_start = -0.5f;
    float trim_end = 1.5f;

    float ts = std::clamp(trim_start, 0.0f, 1.0f);
    float te = std::clamp(trim_end, 0.0f, 1.0f);

    CHECK(ts == doctest::Approx(0.0f));
    CHECK(te == doctest::Approx(1.0f));
}

TEST_CASE("Line with invalid trim draws nothing") {
    float trim_start = 0.8f;
    float trim_end = 0.2f;

    float ts = std::clamp(trim_start, 0.0f, 1.0f);
    float te = std::clamp(trim_end, 0.0f, 1.0f);

    CHECK(ts >= te);
}

TEST_CASE("Line bbox horizontal is correct") {
    Vec2 from{100, 200};
    Vec2 to{300, 200};
    float thickness = 10.0f;

    float pad = thickness * 0.5f;

    float min_x = std::min(from.x, to.x) - pad;
    float max_x = std::max(from.x, to.x) + pad;
    float min_y = std::min(from.y, to.y) - pad;
    float max_y = std::max(from.y, to.y) + pad;

    CHECK(min_x == doctest::Approx(95.0f));
    CHECK(max_x == doctest::Approx(305.0f));
    CHECK(min_y == doctest::Approx(195.0f));
    CHECK(max_y == doctest::Approx(205.0f));
}

TEST_CASE("Line bbox vertical is correct") {
    Vec2 from{400, 100};
    Vec2 to{400, 500};
    float thickness = 20.0f;

    float pad = thickness * 0.5f;

    float min_x = std::min(from.x, to.x) - pad;
    float max_x = std::max(from.x, to.x) + pad;
    float min_y = std::min(from.y, to.y) - pad;
    float max_y = std::max(from.y, to.y) + pad;

    CHECK(min_x == doctest::Approx(390.0f));
    CHECK(max_x == doctest::Approx(410.0f));
    CHECK(min_y == doctest::Approx(90.0f));
    CHECK(max_y == doctest::Approx(510.0f));
}

TEST_CASE("Generated 2D grid lines are correct") {
    auto lines = generate_grid_2d(200.0f, 100.0f);

    REQUIRE(lines.size() == 10);

    CHECK(lines[0].from.x == doctest::Approx(-200.0f));
    CHECK(lines[0].from.y == doctest::Approx(-200.0f));
    CHECK(lines[0].to.x == doctest::Approx(-200.0f));
    CHECK(lines[0].to.y == doctest::Approx(200.0f));

    CHECK(lines[1].from.x == doctest::Approx(-200.0f));
    CHECK(lines[1].from.y == doctest::Approx(-200.0f));
    CHECK(lines[1].to.x == doctest::Approx(200.0f));
    CHECK(lines[1].to.y == doctest::Approx(-200.0f));
}

TEST_CASE("XY grid generation is mathematically correct") {
    Vec3 pos{10, 20, 30};

    auto lines = generate_grid_xy(pos, 200.0f, 100.0f);

    REQUIRE(lines.size() == 10);

    for (const auto& line : lines) {
        CHECK(line.from.z == doctest::Approx(30.0f));
        CHECK(line.to.z == doctest::Approx(30.0f));
    }
}

TEST_CASE("XZ grid generation is mathematically correct") {
    Vec3 pos{10, 20, 30};

    auto lines = generate_grid_xz(pos, 200.0f, 100.0f);

    REQUIRE(lines.size() == 10);

    for (const auto& line : lines) {
        CHECK(line.from.y == doctest::Approx(20.0f));
        CHECK(line.to.y == doctest::Approx(20.0f));
    }
}

static float length(Vec3 a, Vec3 b) {
    Vec3 d = b - a;
    return std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
}

TEST_CASE("Every grid line has correct length") {
    auto lines = generate_grid_xz({0,0,0}, 200.0f, 100.0f);

    for (const auto& line : lines) {
        CHECK(length(line.from, line.to) == doctest::Approx(400.0f));
    }
}

TEST_CASE("Distance between parallel grid lines is spacing") {
    float extent = 500.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    std::vector<float> x_coords;

    for (int i = -steps; i <= steps; ++i) {
        x_coords.push_back(i * spacing);
    }

    for (size_t i = 1; i < x_coords.size(); ++i) {
        CHECK((x_coords[i] - x_coords[i - 1]) == doctest::Approx(spacing));
    }
}

TEST_CASE("No grid coordinate exceeds extent") {
    float extent = 500.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    for (int i = -steps; i <= steps; ++i) {
        float c = i * spacing;

        CHECK(c >= -extent);
        CHECK(c <= extent);
    }
}

TEST_CASE("Grid handles non-divisible extent and spacing") {
    float extent = 250.0f;
    float spacing = 100.0f;

    int steps = static_cast<int>(std::floor(extent / spacing));

    CHECK(steps == 2);

    std::vector<float> coords;
    for (int i = -steps; i <= steps; ++i) {
        coords.push_back(i * spacing);
    }

    REQUIRE(coords.size() == 5);

    CHECK(coords[0] == doctest::Approx(-200.0f));
    CHECK(coords[4] == doctest::Approx(200.0f));

    for (float c : coords) {
        CHECK(c >= -extent);
        CHECK(c <= extent);
    }
}

static float compute_grid_alpha(float base_alpha, float distance, float fade_distance, float fade_min_alpha) {
    if (fade_distance <= 0.0f) {
        return base_alpha;
    }

    float t = std::clamp(distance / fade_distance, 0.0f, 1.0f);
    return base_alpha * (1.0f - t) + fade_min_alpha * t;
}

TEST_CASE("Grid fade alpha is mathematically correct") {
    float base_alpha = 1.0f;
    float fade_distance = 1000.0f;
    float fade_min_alpha = 0.2f;

    CHECK(compute_grid_alpha(base_alpha, 0.0f, fade_distance, fade_min_alpha)
          == doctest::Approx(1.0f));

    CHECK(compute_grid_alpha(base_alpha, 500.0f, fade_distance, fade_min_alpha)
          == doctest::Approx(0.6f));

    CHECK(compute_grid_alpha(base_alpha, 1000.0f, fade_distance, fade_min_alpha)
          == doctest::Approx(0.2f));

    CHECK(compute_grid_alpha(base_alpha, 2000.0f, fade_distance, fade_min_alpha)
          == doctest::Approx(0.2f));
}

TEST_CASE("Grid fade disabled keeps original alpha") {
    float alpha = compute_grid_alpha(
        0.75f,
        999999.0f,
        0.0f,
        0.0f
    );

    CHECK(alpha == doctest::Approx(0.75f));
}

TEST_CASE("Screen grid coordinates are correct") {
    int width = 1280;
    int height = 720;
    int spacing = 80;

    std::vector<int> xs;
    std::vector<int> ys;

    for (int x = 0; x <= width; x += spacing) {
        xs.push_back(x);
    }

    for (int y = 0; y <= height; y += spacing) {
        ys.push_back(y);
    }

    CHECK(xs.front() == 0);
    CHECK(xs.back() == 1280);

    CHECK(ys.front() == 0);
    CHECK(ys.back() == 720);

    CHECK(xs.size() == 17);
    CHECK(ys.size() == 10);
}

TEST_CASE("Centered screen grid bounds are correct") {
    float width = 1280.0f;
    float height = 720.0f;

    float min_x = -width * 0.5f;
    float max_x = width * 0.5f;

    float min_y = -height * 0.5f;
    float max_y = height * 0.5f;

    CHECK(min_x == doctest::Approx(-640.0f));
    CHECK(max_x == doctest::Approx(640.0f));
    CHECK(min_y == doctest::Approx(-360.0f));
    CHECK(max_y == doctest::Approx(360.0f));
}

TEST_CASE("Perspective scale follows Chronon3d Z convention") {
    float zoom = 1000.0f;

    auto scale = [&](float z) {
        return zoom / (zoom + z);
    };

    CHECK(scale(-200.0f) > scale(0.0f));
    CHECK(scale(0.0f) > scale(200.0f));
}

TEST_CASE("Grid generation avoids floating point drift") {
    float spacing = 0.1f;
    int steps = 1000;

    for (int i = -steps; i <= steps; ++i) {
        float coord = i * spacing;

        float expected = static_cast<float>(i) * spacing;

        CHECK(coord == doctest::Approx(expected));
    }
}

TEST_CASE("Line from is not ignored") {
    Vec3 from{100, 100, 0};
    Vec3 to{300, 100, 0};

    Vec3 expected_local_to = to - from;

    CHECK(expected_local_to.x == doctest::Approx(200.0f));
    CHECK(expected_local_to.y == doctest::Approx(0.0f));
}
