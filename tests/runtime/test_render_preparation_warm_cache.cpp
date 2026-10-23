// ===========================================================================
// tests/runtime/test_render_preparation_warm_cache.cpp
//
// Fix 4 (TICKET-OVERLAY-THROUGHPUT-100-MIN) — in the warm daemon every item is
// its own RenderJob on the same warm renderer. The per-item prepare must then
// reuse the persistent caches instead of redoing the work:
//   * the prepared MTSDF text-run cache (keyed by the immutable layout
//     fingerprint) is consulted before preflight rebuilds the run, so a
//     repeated item's identical layout is served from cache;
//   * the framebuffer-pool warmup is top-up only (ensure_preallocated), so a
//     warm item allocates and touches nothing.
// ===========================================================================

#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

using namespace chronon3d;

// Resolve the font fixture relative to the test's CWD (the asset mount root is
// the current path in test renderers).
std::string fixture_font_path() {
    for (const char* candidate : {
             "assets/fonts/Poppins-Regular.ttf",
             "Chronon3d/assets/fonts/Poppins-Regular.ttf",
             "../assets/fonts/Poppins-Regular.ttf",
             "../../assets/fonts/Poppins-Regular.ttf"}) {
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return {};
}

Composition make_text_composition() {
    return Composition(
        CompositionSpec{
            .name = "warm-cache-text",
            .width = 640,
            .height = 360,
            .frame_rate = FrameRate{30, 1},
            .duration = Frame{10}},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("title", [](LayerBuilder& l) {
                l.kind(LayerKind::Text);
                l.text("hello", TextDefinition{
                    .content = {.value = "warm cache item"},
                    .style = {
                        .font = {.font_path = fixture_font_path(),
                                 .font_family = "Poppins",
                                 .font_size = 48.0f},
                        .color = Color::white()},
                    .frame = {.size = {640.0f, 360.0f}}});
            });
            return s.build();
        });
}

std::uint64_t counter(const std::atomic<std::uint64_t>& c) {
    return c.load(std::memory_order_relaxed);
}

} // namespace

TEST_CASE("warm daemon prepare reuses the prepared text-run cache across items (fix 4)") {
    auto renderer = test::make_renderer_shared();
    REQUIRE(renderer);
    REQUIRE(renderer->counters());
    const auto composition = make_text_composition();
    auto* counters = renderer->counters();

    // Item 1: cold prepare builds the MTSDF prepared run for the layout.
    const auto first = runtime::prepare_render(renderer.get(), composition, {});
    REQUIRE(first.ok());
    const auto built_after_first = counter(counters->gpu_text_prepared_batch_build_count);
    const auto reused_after_first = counter(counters->gpu_text_prepared_batch_reuse_count);
    CHECK(built_after_first >= 1);

    // Item 2: same warm renderer, identical layout — the prepared-run cache
    // must serve it without rebuilding (this is the fix-4 reuse).
    const auto second = runtime::prepare_render(renderer.get(), composition, {});
    REQUIRE(second.ok());
    CHECK(counter(counters->gpu_text_prepared_batch_build_count) == built_after_first);
    CHECK(counter(counters->gpu_text_prepared_batch_reuse_count) > reused_after_first);
    // Font faces come from the persistent FontEngine cache on warm items.
    CHECK(second.timings.font_cache_misses.value_or(1) == 0);
}

TEST_CASE("warm daemon pool warmup is top-up only across items (fix 4)") {
    auto renderer = test::make_renderer_shared();
    REQUIRE(renderer);
    REQUIRE(renderer->framebuffer_pool());
    const auto composition = make_text_composition();

    runtime::RendererWarmupOptions warm{};
    warm.width = 640;
    warm.height = 360;
    warm.framebuffer_count = 4;
    warm.preallocate_framebuffers = true;
    warm.touch_memory = true;
    warm.render_dummy_frame = false;

    // Item 1: cold — the warmup materialises the pool surfaces.
    const auto first = runtime::warmup_renderer(*renderer, composition, warm);
    CHECK(first.framebuffers_created > 0);
    const auto bytes_after_first = renderer->framebuffer_pool()->current_bytes();

    // Item 2: warm — the same warmup must be a top-up no-op: no new surfaces,
    // no re-clear/re-touch of the already warm pool.
    const auto second = runtime::warmup_renderer(*renderer, composition, warm);
    CHECK(second.framebuffers_created == 0);
    CHECK(renderer->framebuffer_pool()->current_bytes() == bytes_after_first);
}
