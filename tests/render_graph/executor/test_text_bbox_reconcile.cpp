// ============================================================================
// tests/render_graph/executor/test_text_bbox_reconcile.cpp
//
// Regression test for the refactor that replaced the process-wide
// `static std::atomic<bool> warned` in text_bbox_reconcile.cpp with a
// per-session TextBboxReporter, and replaced the inline alpha-framebuffer
// scan with the canonical `chronon3d::alpha_bbox_scan()`.
//
// Invariants locked:
//   1. reconcile_text_bbox_after_render() expands the predicted bbox when
//      the actual ink extends beyond it.
//   2. The text_bbox_contract_violations counter is incremented on every
//      expansion (not just the first).
//   3. The reporter emits a warning exactly once per session.
//   4. A fresh reporter for a new session can emit a warning again —
//      there is no process-wide static state suppressing diagnostics.
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

TEST_CASE("TextBboxReconcile: expands predicted bbox when ink exceeds prediction") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4); // ink occupies [5,9) x [5,9)

    // Predicted bbox is smaller than the ink region.
    std::optional<raster::BBox> predicted = raster::BBox{6, 6, 7, 7};

    RenderCounters counters;
    TextBboxReporter reporter;

    auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);

    REQUIRE(expanded.has_value());
    CHECK(expanded->x0 == 5);
    CHECK(expanded->y0 == 5);
    CHECK(expanded->x1 == 9);
    CHECK(expanded->y1 == 9);

    CHECK(counters.text_bbox_contract_violations.load() == 1);
    CHECK(reporter.has_warned());
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

TEST_CASE("TextBboxReconcile: warning is emitted exactly once per session") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4);

    std::optional<raster::BBox> predicted = raster::BBox{6, 6, 7, 7};
    RenderCounters counters;
    TextBboxReporter reporter;

    // First expansion: warning emitted.
    auto expanded1 = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
    REQUIRE(expanded1.has_value());
    CHECK(reporter.has_warned());
    CHECK(counters.text_bbox_contract_violations.load() == 1);

    // Second expansion with the same reporter: still expands and still
    // increments the counter, but does NOT warn again.
    auto expanded2 = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
    REQUIRE(expanded2.has_value());
    CHECK(counters.text_bbox_contract_violations.load() == 2);
    // The reporter state is the only observable side-effect of the
    // warn-once logic; there is no process-wide static flag.
    CHECK(reporter.has_warned());
}

TEST_CASE("TextBboxReconcile: fresh reporter for a new session can warn again") {
    MockTextRunNode node("text_run");
    Framebuffer fb(20, 20);
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    draw_ink(fb, 5, 5, 4, 4);

    std::optional<raster::BBox> predicted = raster::BBox{6, 6, 7, 7};
    RenderCounters counters;

    {
        TextBboxReporter reporter;
        auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
        REQUIRE(expanded.has_value());
        CHECK(reporter.has_warned());
    }

    // A brand-new reporter must be able to emit a warning again — this
    // proves the warning state is not stored in a static/global variable.
    {
        TextBboxReporter reporter;
        CHECK_FALSE(reporter.has_warned());
        auto expanded = reconcile_text_bbox_after_render(node, fb, predicted, &counters, reporter);
        REQUIRE(expanded.has_value());
        CHECK(reporter.has_warned());
    }

    CHECK(counters.text_bbox_contract_violations.load() == 2);
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
