#pragma once

#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <initializer_list>
#include <memory>
#include <string>
#include <cmath>

namespace chronon3d::test {

struct RenderSample {
    Frame frame{0};
    std::shared_ptr<Framebuffer> framebuffer;
    std::optional<renderer::BrightRegion2D> bbox;
    std::optional<Vec2> centroid;
};

inline SoftwareRenderer make_regression_renderer(bool diagnostics = false) {
    SoftwareRenderer renderer(Config{});
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.diagnostics.enabled = diagnostics;
    renderer.set_settings(settings);
    return renderer;
}

inline RenderSample render_sample(SoftwareRenderer& renderer, const Composition& comp, Frame frame) {
    RenderSample sample;
    sample.frame = frame;
    sample.framebuffer = renderer.render_frame(comp, frame);
    if (sample.framebuffer) {
        sample.bbox = renderer::bright_bbox(*sample.framebuffer);
        sample.centroid = renderer::bright_centroid(*sample.framebuffer);
    }
    return sample;
}

inline void expect_no_projected_winding_flips(const SoftwareRenderer& renderer) {
    CHECK(renderer.counters()->projected_winding_flips.load() == 0);
}

inline void expect_centered_bright_region(
    const RenderSample& sample,
    Vec2 target,
    float max_dx,
    float max_dy
) {
    REQUIRE(sample.framebuffer != nullptr);
    REQUIRE(sample.bbox.has_value());
    REQUIRE(sample.centroid.has_value());

    CHECK(sample.bbox->min_x < static_cast<int>(target.x));
    CHECK(sample.bbox->max_x > static_cast<int>(target.x));
    CHECK(sample.bbox->min_y < static_cast<int>(target.y));
    CHECK(sample.bbox->max_y > static_cast<int>(target.y));
    CHECK(std::abs(sample.centroid->x - target.x) <= max_dx);
    CHECK(std::abs(sample.centroid->y - target.y) <= max_dy);
}

inline void save_sample_debug(const RenderSample& sample, const std::string& path) {
    REQUIRE(sample.framebuffer != nullptr);
    save_debug(*sample.framebuffer, path);
}

} // namespace chronon3d::test
