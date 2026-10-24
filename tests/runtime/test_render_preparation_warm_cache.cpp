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
//
// Capability boundary, pinned explicitly below: BUILDING a prepared MTSDF run
// requires a native-surface atlas page (the GPU lanes); on the pure software
// backend `GpuGlyphAtlas::page_for_glyph` refuses ("native surfaces are not
// supported") and no batch is built. The reuse side — the fix-4 change in
// `SoftwareRenderer::preflight_fonts` — is backend-independent and is pinned
// here with the cache seeded exactly as a GPU-lane build would populate it.
// The build side is exercised by the native lanes (its evidence is the
// `gpu_text_prepared_batch_*` counters in the golden sidecars) and is
// documented here as a no-op on software instead of being silently assumed.
// ===========================================================================

#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
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

// Builds the warm-cache text composition. When `captured_layout` is non-null
// the builder records the TextRunLayout the preflight walk keys on, so the
// test can seed the prepared-run cache with the exact fingerprint the
// production code will look up.
Composition make_text_composition(
    std::shared_ptr<const TextRunLayout>* captured_layout = nullptr) {
    return Composition(
        CompositionSpec{
            .name = "warm-cache-text",
            .width = 640,
            .height = 360,
            .frame_rate = FrameRate{30, 1},
            .duration = Frame{10}},
        [captured_layout](const FrameContext& ctx) {
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
            auto scene = s.build();
            if (captured_layout != nullptr) {
                for (const auto& layer : scene.layers()) {
                    for (const auto& node : layer.nodes) {
                        if (node.shape.type() != ShapeType::TextRun) continue;
                        const auto& handle = node.shape.text_run_shape_handle();
                        if (handle.value && handle.value->layout) {
                            *captured_layout = handle.value->layout;
                            return scene;
                        }
                    }
                }
            }
            return scene;
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
    REQUIRE(renderer->execution_context().text.prepared_cache);
    auto* counters = renderer->counters();

    std::shared_ptr<const TextRunLayout> layout;
    const auto composition = make_text_composition(&layout);

    // Item 1: cold prepare on the warm renderer. On the software lane no
    // prepared run can be BUILT (native-surface capability boundary, pinned in
    // the case below), but the walk records the layout it keys on.
    const auto first = runtime::prepare_render(renderer.get(), composition, {});
    REQUIRE(first.ok());
    REQUIRE(layout);

    // Seed the runtime-owned prepared-run cache exactly as a GPU-lane build
    // would: same immutable layout fingerprint, one ready prepared run. This
    // is the state a warm daemon carries from item 1 into item 2.
    auto seeded = std::make_shared<runtime::GpuPreparedTextRun>();
    seeded->ready = true;
    renderer->execution_context().text.prepared_cache->store(*layout, seeded);

    // Item 2: identical layout. The preflight walk must serve it from the
    // cache — a REUSE, and never a second build attempt.
    const auto build_before = counter(counters->gpu_text_prepared_batch_build_count);
    const auto reuse_before = counter(counters->gpu_text_prepared_batch_reuse_count);
    const auto second = runtime::prepare_render(renderer.get(), composition, {});
    REQUIRE(second.ok());
    CHECK(counter(counters->gpu_text_prepared_batch_build_count) == build_before);
    CHECK(counter(counters->gpu_text_prepared_batch_reuse_count) == reuse_before + 1);
    // Font faces come from the persistent FontEngine cache on warm items.
    CHECK(second.timings.font_cache_misses.value_or(1) == 0);
}

TEST_CASE("software lane documents the MTSDF prepared-run capability boundary (fix 4)") {
    // Without native surfaces the glyph atlas cannot hold pages, so a cache
    // miss must NOT report a built prepared run: the software lane documents
    // its boundary instead of silently claiming a GPU-lane cache build.
    auto renderer = test::make_renderer_shared();
    REQUIRE(renderer);
    REQUIRE(renderer->counters());
    const auto composition = make_text_composition();
    auto* counters = renderer->counters();

    const auto build_before = counter(counters->gpu_text_prepared_batch_build_count);
    const auto first = runtime::prepare_render(renderer.get(), composition, {});
    REQUIRE(first.ok());
    CHECK(counter(counters->gpu_text_prepared_batch_build_count) == build_before);
    CHECK(renderer->execution_context().text.prepared_cache->size() == 0);
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
