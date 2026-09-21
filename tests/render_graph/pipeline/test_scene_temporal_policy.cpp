#include <doctest/doctest.h>

#include "src/render_graph/pipeline/scene_temporal_policy.hpp"

TEST_CASE("isolated temporal settings disable parent-frame side effects") {
    chronon3d::RenderSettings settings;
    settings.dirty.enabled = true;
    settings.dirty.use_bitmask = true;
    settings.dirty.use_tiles = true;
    settings.dirty.tile_size = 128;
    settings.diagnostics.enabled = true;
    settings.diagnostics.plan = true;
    settings.diagnostics.plan_output = "/tmp/plan-####.json";
    settings.text_layout_debug = true;
    settings.diagnostic_overlay_only = true;
    settings.require_native_gpu = true;

    const auto isolated =
        chronon3d::graph::detail::make_isolated_temporal_settings(settings);

    CHECK_FALSE(isolated.dirty.enabled);
    CHECK_FALSE(isolated.dirty.use_bitmask);
    CHECK_FALSE(isolated.dirty.use_tiles);
    CHECK(isolated.dirty.tile_size == 0);
    CHECK_FALSE(isolated.diagnostics.enabled);
    CHECK_FALSE(isolated.diagnostics.plan);
    CHECK(isolated.diagnostics.plan_output.empty());
    CHECK_FALSE(isolated.text_layout_debug);
    CHECK_FALSE(isolated.diagnostic_overlay_only);
    CHECK(isolated.require_native_gpu);
}
