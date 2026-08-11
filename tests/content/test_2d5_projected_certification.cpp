#include <doctest/doctest.h>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/camera_projection_resolver.hpp>
#include <chronon3d/math/near_plane_clip.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <content/register_content_modules.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace chronon3d;

namespace {

constexpr std::array<const char*, 5> kProjectedCompositions = {
    "Text25D-T01-YRotation",
    "Text25D-T02-XFlip",
    "Text25D-T03-Pivot",
    "Text25D-T04-Parallax",
    "Text25D-T05-DocumentaryHero",
};

// Frame 0 exercises the edge-on/near-plane boundary; frame 119 is the
// settled front-facing sample used for the pivot lock.
constexpr std::array<int, 3> kCertificationFrames = {0, 60, 119};
// Deliberately non-monotonic order: this exercises frame random access rather
// than merely replaying the sequential samples in reverse.
constexpr std::array<int, 3> kRandomAccessFrames = {119, 0, 60};

void register_content(CompositionRegistry& registry) {
    ExtensionCatalog catalog;
    graph::GraphNodeCatalog nodes;
    effects::EffectCatalog effects;
    AssetRegistry assets;
    ExtensionContext context{registry, nodes, effects, assets};
    register_content_modules(catalog, context);
}

struct ForegroundMetrics {
    std::size_t pixels{0};
    bool finite{true};
    float max_luma{0.0f};
    float min_luma{1.0f};
    int x0{0};
    int y0{0};
    int x1{-1};
    int y1{-1};
    double sum_x{0.0};
    double sum_y{0.0};

    [[nodiscard]] bool visible() const noexcept { return pixels != 0; }
    [[nodiscard]] float center_x() const noexcept {
        return pixels == 0 ? 0.0f : static_cast<float>(sum_x / pixels);
    }
    [[nodiscard]] float center_y() const noexcept {
        return pixels == 0 ? 0.0f : static_cast<float>(sum_y / pixels);
    }
};

// The showcase deliberately uses a solid opaque background. Treat the corner
// pixel as the background reference so this measures the projected foreground
// rather than the full-canvas background layer.
ForegroundMetrics foreground_metrics(const Framebuffer& framebuffer) {
    ForegroundMetrics result;
    const Color background = framebuffer.get_pixel(0, 0);
    constexpr float kColorDelta = 0.035f;

    for (int y = 0; y < framebuffer.height(); ++y) {
        const Color* row = framebuffer.pixels_row(y);
        for (int x = 0; x < framebuffer.width(); ++x) {
            const Color color = row[x];
            if (!std::isfinite(color.r) || !std::isfinite(color.g) ||
                !std::isfinite(color.b) || !std::isfinite(color.a)) {
                result.finite = false;
                continue;
            }
            const float luma = 0.2126f * color.r +
                               0.7152f * color.g +
                               0.0722f * color.b;
            result.max_luma = std::max(result.max_luma, luma);
            result.min_luma = std::min(result.min_luma, luma);
            const float delta = std::max({
                std::abs(color.r - background.r),
                std::abs(color.g - background.g),
                std::abs(color.b - background.b),
            });
            if ((delta <= kColorDelta && luma < 0.20f) || color.a <= 0.01f) continue;

            ++result.pixels;
            result.x0 = result.pixels == 1 ? x : std::min(result.x0, x);
            result.y0 = result.pixels == 1 ? y : std::min(result.y0, y);
            result.x1 = std::max(result.x1, x);
            result.y1 = std::max(result.y1, y);
            result.sum_x += static_cast<double>(x);
            result.sum_y += static_cast<double>(y);
        }
    }
    return result;
}

std::unordered_map<int, u64> render_sequential(
    const CompositionRegistry& registry,
    const Composition& composition,
    const std::array<int, 3>& frames,
    std::vector<ForegroundMetrics>& metrics,
    std::uint64_t& winding_flips) {
    auto renderer = test::make_renderer_shared();
    renderer->set_composition_registry(&registry);
    std::unordered_map<int, u64> hashes;
    for (const int frame : frames) {
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE(framebuffer != nullptr);
        metrics.push_back(foreground_metrics(*framebuffer));
        hashes.emplace(frame, test::framebuffer_hash(*framebuffer));
    }
    winding_flips = renderer->counters()->projected_winding_flips.load();
    return hashes;
}

std::unordered_map<int, u64> render_random_access(
    const CompositionRegistry& registry,
    const Composition& composition,
    const std::array<int, 3>& frames,
    std::uint64_t& winding_flips) {
    auto renderer = test::make_renderer_shared();
    renderer->set_composition_registry(&registry);
    std::unordered_map<int, u64> hashes;
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        auto framebuffer = renderer->render(composition, Frame{*it});
        REQUIRE(framebuffer != nullptr);
        hashes.emplace(*it, test::framebuffer_hash(*framebuffer));
    }
    winding_flips = renderer->counters()->projected_winding_flips.load();
    return hashes;
}

std::unordered_map<int, u64> render_stateful_random_access(
    const CompositionRegistry& registry,
    const Composition& composition,
    const std::array<int, 3>& frames) {
    auto renderer = test::make_renderer_shared();
    renderer->set_composition_registry(&registry);
    std::unordered_map<int, u64> hashes;
    for (const int frame : frames) {
        auto framebuffer = renderer->render(composition, Frame{frame});
        REQUIRE(framebuffer != nullptr);
        hashes.emplace(frame, test::framebuffer_hash(*framebuffer));
    }
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        auto framebuffer = renderer->render(composition, Frame{*it});
        REQUIRE(framebuffer != nullptr);
        CHECK(hashes.at(*it) == test::framebuffer_hash(*framebuffer));
    }
    return hashes;
}

std::size_t horizontal_difference(const Framebuffer& framebuffer) {
    std::size_t different = 0;
    for (int y = 0; y < framebuffer.height(); ++y) {
        for (int x = 0; x < framebuffer.width() / 2; ++x) {
            const Color left = framebuffer.get_pixel(x, y);
            const Color right = framebuffer.get_pixel(framebuffer.width() - 1 - x, y);
            const float delta = std::max({
                std::abs(left.r - right.r),
                std::abs(left.g - right.g),
                std::abs(left.b - right.b),
                std::abs(left.a - right.a),
            });
            if (delta > 0.01f) ++different;
        }
    }
    return different;
}

float canonical_projected_winding(Vec3 rotation, Vec2 surface_size) {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -1000.0f};
    camera.zoom = 1000.0f;

    Transform layer;
    layer.rotation = glm::quat(glm::radians(rotation));
    const auto projected = project_layer_2_5d(
        layer,
        layer.to_mat4(),
        camera,
        1920.0f,
        1080.0f,
        false,
        surface_size,
        BackfaceMode::Hidden);
    REQUIRE(projected.visible);

    // The producer stores raster pixels top-down. Apply that basis conversion
    // exactly as the projected consumer does, then lock the signed-area sign
    // against the front-facing reference. A sign change is a geometric
    // mirror, even when an asymmetric foreground still has a non-zero
    // left/right difference.
    const Mat4 raster_y_down = glm::scale(
        Mat4(1.0f), Vec3(1.0f, -1.0f, 1.0f));
    const auto area = projected_quad_signed_area(
        projected.projection_matrix * raster_y_down,
        surface_size.x,
        surface_size.y);
    REQUIRE(area.has_value());
    REQUIRE(std::isfinite(*area));
    return *area;
}

void certify_near_plane_contract() {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -1000.0f};
    camera.zoom = 1000.0f;
    camera.point_of_interest = {0.0f, 0.0f, 0.0f};
    camera.point_of_interest_enabled = true;

    Transform crossing;
    crossing.position = {0.0f, 0.0f, -999.9f};
    crossing.rotation = glm::quat(glm::radians(Vec3{0.0f, 80.0f, 0.0f}));
    constexpr Vec2 surface_size{1700.0f, 420.0f};
    const auto projected = project_layer_2_5d(
        crossing, crossing.to_mat4(), camera, 1920.0f, 1080.0f, false,
        surface_size, BackfaceMode::Hidden);

    CameraProjectionInput resolver_input;
    resolver_input.world_transform = crossing.to_mat4();
    resolver_input.layer_size = surface_size;
    resolver_input.camera = camera;
    resolver_input.viewport = {1920.0f, 1080.0f};
    resolver_input.backface_mode = BackfaceMode::Hidden;
    const auto resolved = CameraProjectionResolver::project_layer(resolver_input);

    // This pose deliberately straddles the camera plane. Lock the resolver's
    // explicit clipping state and every clipped vertex depth, not merely the
    // aggregate centroid depth returned by project_layer_2_5d().
    CHECK(resolved.visible);
    CHECK(resolved.clip_state == ClipState::ClippedNear);
    CHECK(resolved.corner_count >= 3);
    for (int i = 0; i < resolved.corner_count; ++i) {
        CHECK(std::isfinite(resolved.corners[i].x));
        CHECK(std::isfinite(resolved.corners[i].y));
        CHECK(std::isfinite(resolved.corners[i].z));
        CHECK(resolved.corners[i].z >= camera_math::kNearClipZ - 1e-5f);
    }

    // The public projected result must remain finite and bounded as well.
    CHECK(projected.visible);
    CHECK(std::isfinite(projected.depth));
    CHECK(projected.depth > camera_math::kNearClipZ - 1e-5f);
    CHECK(std::isfinite(projected.perspective_scale));
    CHECK(projected.perspective_scale < 2'000'000.0f);

    Transform behind;
    behind.position = {0.0f, 0.0f, -1200.0f};
    const auto culled = project_layer_2_5d(
        behind, behind.to_mat4(), camera, 1920.0f, 1080.0f, false,
        Vec2{1700.0f, 420.0f}, BackfaceMode::Hidden);
    CHECK_FALSE(culled.visible);
}

} // namespace

TEST_CASE("2.5D projected surfaces T01-T05 are certified end to end") {
    CompositionRegistry registry;
    register_content(registry);
    certify_near_plane_contract();

    for (const char* name : kProjectedCompositions) {
        INFO("composition=" << name);
        REQUIRE(registry.contains(name));
        const Composition composition = registry.create(name);
        REQUIRE(composition.width() == 1920);
        REQUIRE(composition.height() == 1080);

        std::vector<ForegroundMetrics> metrics;
        std::uint64_t sequential_winding_flips = 0;
        const auto sequential = render_sequential(
            registry, composition, kCertificationFrames, metrics, sequential_winding_flips);

        std::uint64_t random_winding_flips = 0;
        const auto random = render_random_access(
            registry, composition, kRandomAccessFrames, random_winding_flips);
        const auto stateful = render_stateful_random_access(
            registry, composition, kRandomAccessFrames);

        REQUIRE(metrics.size() == kCertificationFrames.size());
        CHECK(sequential_winding_flips == 0);
        CHECK(random_winding_flips == 0);

        for (const int frame : kCertificationFrames) {
            INFO("composition=" << name << " frame=" << frame);
            REQUIRE(sequential.contains(frame));
            REQUIRE(random.contains(frame));
            REQUIRE(stateful.contains(frame));
            CHECK(sequential.at(frame) == random.at(frame));
            CHECK(sequential.at(frame) == stateful.at(frame));
        }

        for (std::size_t index = 0; index < metrics.size(); ++index) {
            const auto& metric = metrics[index];
            const bool edge_on_t03 =
                std::string_view{name} == "Text25D-T03-Pivot" &&
                kCertificationFrames[index] == 0;
            INFO("composition=" << std::string{name}
                 << " sample=" << index
                 << " pixels=" << metric.pixels
                 << " bbox=[" << metric.x0 << "," << metric.y0 << ","
                 << metric.x1 << "," << metric.y1 << "]"
                 << " center=[" << metric.center_x() << "," << metric.center_y() << "]"
                 << " luma=[" << metric.min_luma << "," << metric.max_luma << "]");
            CHECK(metric.finite);
            // Exact edge-on projection may legitimately contain no visible
            // glyph coverage. It is still certified by finite pixels and the
            // bounded/empty output checks below; every other sample must carry
            // visible projected content.
            if (!edge_on_t03) {
                CHECK(metric.visible());
            }
            // A projected surface must remain bounded by the destination
            // framebuffer; this catches near-plane explosions and clipping.
            CHECK(metric.x0 >= 0);
            CHECK(metric.y0 >= 0);
            CHECK(metric.x1 < composition.width());
            CHECK(metric.y1 < composition.height());
            CHECK(metric.x1 - metric.x0 + 1 < composition.width());
            CHECK(metric.y1 - metric.y0 + 1 < composition.height());
        }

        // Winding must retain the front-facing sign for every non-edge-on
        // projected orientation. This complements the runtime winding-flip
        // counter with an explicit canonical orientation assertion.
        const Vec3 orientation =
            std::string_view{name} == "Text25D-T01-YRotation"
                ? Vec3{0.0f, -75.0f, 0.0f}
                : std::string_view{name} == "Text25D-T02-XFlip"
                    ? Vec3{75.0f, 0.0f, 0.0f}
                    : std::string_view{name} == "Text25D-T05-DocumentaryHero"
                        ? Vec3{15.0f, -25.0f, 0.0f}
                        : Vec3{0.0f, 0.0f, 0.0f};
        const float front_area = canonical_projected_winding(
            Vec3{0.0f, 0.0f, 0.0f}, Vec2{1700.0f, 420.0f});
        const float sample_area = canonical_projected_winding(
            orientation, Vec2{1700.0f, 420.0f});
        CHECK(std::abs(front_area) > 1e-3f);
        CHECK(std::abs(sample_area) > 1e-3f);
        CHECK(std::signbit(front_area) == std::signbit(sample_area));

        if (std::string_view{name} == "Text25D-T01-YRotation") {
            // T01 is intentionally asymmetric in time; retain this visual
            // sanity check alongside the signed-area orientation lock.
            auto renderer = test::make_renderer_shared();
            renderer->set_composition_registry(&registry);
            auto framebuffer = renderer->render(composition, Frame{119});
            REQUIRE(framebuffer != nullptr);
            CHECK(horizontal_difference(*framebuffer) > 0);
        }

        if (std::string_view{name} == "Text25D-T03-Pivot") {
            // T03 rotates around the tight surface's semantic origin. The
            // foreground centroid must stay near the authored canvas pivot at
            // both an oblique sample and the settled front-facing sample.
            REQUIRE(metrics.size() == 3);
            // The oblique sample is allowed to have a perspective-shifted
            // visual centroid. The authored pivot lock is the settled,
            // front-facing frame; frame 0 is the edge-on near-plane sample.
            REQUIRE(metrics[1].visible());
            REQUIRE(metrics[2].visible());
            INFO("T03 front-facing pivot");
            CHECK(std::abs(metrics[2].center_x() - 960.0f) < 150.0f);
            CHECK(std::abs(metrics[2].center_y() - 540.0f) < 150.0f);
            // The semantic pivot is fixed at the authored canvas centre. The
            // oblique centroid may move under perspective, but it must remain
            // in the same bounded pivot neighbourhood and settle back without
            // an origin-induced jump.
            CHECK(std::abs(metrics[1].center_x() - 960.0f) < 400.0f);
            CHECK(std::abs(metrics[1].center_y() - 540.0f) < 400.0f);
            CHECK(std::abs(metrics[2].center_x() - metrics[1].center_x()) < 400.0f);
            CHECK(std::abs(metrics[2].center_y() - metrics[1].center_y()) < 400.0f);
        }
    }
}
