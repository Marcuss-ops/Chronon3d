// ============================================================================
// tests/render_graph/executor/test_text_bbox_reconcile.cpp
//
// Regression test for the refactor that replaced the process-wide
// `static std::atomic<bool> warned` in text_bbox_reconcile.cpp with a
// per-session TextBboxReporter, and replaced the inline alpha-framebuffer
// scan with the canonical `chronon3d::alpha_bbox_scan()`.
//
// POST_RENDER_EXPAND has been removed: the predicted bbox is now computed
// from FreeType outline bboxes (see src/backends/text/font_engine.cpp and
// src/text/text_run_geometry.cpp) and is the single source of truth.  This
// file now locks the no-op contract of reconcile_text_bbox_after_render().
//
// Invariants locked:
//   1. reconcile_text_bbox_after_render() always returns std::nullopt.
//   2. No expansion, counter increment, or warning is emitted.
//   3. Reporter state remains untouched.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include "src/render_graph/executor/text_bbox_reconcile.hpp"
#include "src/render_graph/executor/text_bbox_reporter.hpp"

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

class MockTextRunNode : public RenderGraphNode {
public:
    explicit MockTextRunNode(std::string name)
        : RenderGraphNode(frame_variant_cache("mock"))
        , m_name(std::move(name)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }
    [[nodiscard]] RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::TextRun; }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext&) const override { return {}; }

    NodeExecResult execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>) override
    {
        return NodeExecResult(NodeExecutionError{RenderBackendErrorCode::ExecutionFailure, "mock"});
    }

private:
    std::string m_name;
};

// Fill a small region of the framebuffer with opaque red.
void draw_ink(Framebuffer& fb, int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            fb.set_pixel(x, y, Color{1.0f, 0.0f, 0.0f, 1.0f});
        }
    }
}

} // namespace

TEST_CASE("TextBboxReconcile: no longer expands predicted bbox when ink exceeds prediction") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4); // ink occupies [5,9) x [5,9)

    // Predicted bbox is smaller than the ink region.
    std::optional<raster::BBox> predicted = raster::BBox{6, 6, 7, 7};

    RenderCounters counters;
    TextBboxReporter reporter;

    auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);

    CHECK_FALSE(expanded.has_value());
    CHECK(counters.text_bbox_contract_violations.load() == 0);
    CHECK_FALSE(reporter.has_warned());
}

TEST_CASE("TextBboxReconcile: no expansion when ink fits inside predicted bbox") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 2, 2); // ink occupies [5,7) x [5,7)

    // Predicted bbox fully contains the ink.
    std::optional<raster::BBox> predicted = raster::BBox{0, 0, 20, 20};

    RenderCounters counters;
    TextBboxReporter reporter;

    auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);

    CHECK_FALSE(expanded.has_value());
    CHECK(counters.text_bbox_contract_violations.load() == 0);
    CHECK_FALSE(reporter.has_warned());
}

TEST_CASE("TextBboxReconcile: warning is never emitted after POST_RENDER_EXPAND removal") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4);

    std::optional<raster::BBox> predicted = raster::BBox{6, 6, 7, 7};
    RenderCounters counters;
    TextBboxReporter reporter;

    auto expanded1 = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
    CHECK_FALSE(expanded1.has_value());
    CHECK_FALSE(reporter.has_warned());
    CHECK(counters.text_bbox_contract_violations.load() == 0);

    auto expanded2 = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
    CHECK_FALSE(expanded2.has_value());
    CHECK_FALSE(reporter.has_warned());
    CHECK(counters.text_bbox_contract_violations.load() == 0);
}

TEST_CASE("TextBboxReconcile: fresh reporter stays clean after POST_RENDER_EXPAND removal") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4);

    std::optional<raster::BBox> predicted = raster::BBox{6, 6, 7, 7};
    RenderCounters counters;

    {
        TextBboxReporter reporter;
        auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
        CHECK_FALSE(expanded.has_value());
        CHECK_FALSE(reporter.has_warned());
    }

    {
        TextBboxReporter reporter;
        CHECK_FALSE(reporter.has_warned());
        auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
        CHECK_FALSE(expanded.has_value());
        CHECK_FALSE(reporter.has_warned());
    }

    CHECK(counters.text_bbox_contract_violations.load() == 0);
}

TEST_CASE("TextBboxReconcile: empty predicted bbox is a no-op") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4);

    RenderCounters counters;
    TextBboxReporter reporter;

    auto expanded = reconcile_text_bbox_after_render(
        node, fb, std::nullopt, &counters, reporter);

    CHECK_FALSE(expanded.has_value());
    CHECK(counters.text_bbox_contract_violations.load() == 0);
    CHECK_FALSE(reporter.has_warned());
}

TEST_CASE("TextBboxReconcile: transparent framebuffer is a no-op") {
    MockTextRunNode node("text_run");
    Framebuffer fb(10, 10);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});

    std::optional<raster::BBox> predicted = raster::BBox{0, 0, 10, 10};
    RenderCounters counters;
    TextBboxReporter reporter;

    auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);

    CHECK_FALSE(expanded.has_value());
    CHECK(counters.text_bbox_contract_violations.load() == 0);
    CHECK_FALSE(reporter.has_warned());
}
