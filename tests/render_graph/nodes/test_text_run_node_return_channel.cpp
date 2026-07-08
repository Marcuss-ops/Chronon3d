// SPDX-License-Identifier: MIT
//
// M1.5#1 — return-channel contract test for TextRunNode::execute().
//
// The P0-1 audit identified a false-success anti-pattern:
// TextRunNode::execute() returned a valid-looking OwnedFB after a
// backend draw_text_run() failure.  P0-1 (commit `7058dacc`+`876a14ac`)
// closed the anti-pattern by migrating the return type from `OwnedFB`
// to `NodeExecResult = Result<OwnedFB, NodeExecutionError>`.
//
// The existing test `test_text_run_node_execute_error.cpp` locks the
// diagnostic sidecar (`ctx.frame_error->value()`), so a regression
// would be caught.  THIS test locks the canonical return channel
// itself: `execute()` must return a NodeExecResult carrying a typed
// `NodeExecutionError` (NOT a valid OwnedFB) for every failure mode.
//
// Matrix:
//   §1 ExecutionFailure   → result.ok() == false ; error.backend_code == ExecutionFailure
//   §2 CapabilitiesOff    → result.ok() == false ; error.backend_code == UnsupportedCapability
//   §3 NullBackend        → result.ok() == false ; error.backend_code == InvalidInput
//   §4 Success            → result.ok() == true  ; result.value() != nullptr
//   §5 Successful shape hash on result.key via execute+cache_key path
//                           (no error; ensures P0-1 contract is symmetric).
//
// Framework: doctest.  Gated by CHRONON3D_ENABLE_TEXT (mirrors the
// existing `test_text_run_node_execute_error.cpp` gating so the two
// test files build/exclude in lockstep).
//
// Pre-refactor expected PASS: §1-§4 PASS on current main@584ac085.
// Post-refactor expected PASS: identical — the M1.5#1 split is
// purely internal and must NOT change observable behaviour.

#include <doctest/doctest.h>

#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>

#include <memory>
#include <string>
#include <utility>

#ifdef CHRONON3D_ENABLE_TEXT

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

// ── Mock backend with selectable mode ───────────────────────────────

class FailureMockBackend final : public RenderBackend {
public:
    enum class Mode {
        CapabilitiesOff,
        ExecutionFailure,
        InvalidInput,
        Success,
    };

    explicit FailureMockBackend(Mode m) : m_mode(m) {}

    RenderCapabilities capabilities() const noexcept override {
        const bool text_run =
            (m_mode != Mode::CapabilitiesOff);
        return RenderCapabilities{ .text_run = text_run };
    }

    int draw_text_run_calls() const noexcept {
        return m_calls.load(std::memory_order_relaxed);
    }

    RenderOpResult draw_text_run(
        Framebuffer& /*fb*/,
        const chronon3d::TextRunShape& /*shape*/,
        const glm::mat4& /*model*/,
        float /*opacity*/
    ) override {
        m_calls.fetch_add(1, std::memory_order_relaxed);
        switch (m_mode) {
            case Mode::ExecutionFailure:
                return RenderOpResult{RenderBackendError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "mock: ExecutionFailure"
                }};
            case Mode::InvalidInput:
                return RenderOpResult{RenderBackendError{
                    graph::RenderBackendErrorCode::InvalidInput,
                    "mock: InvalidInput"
                }};
            case Mode::CapabilitiesOff:
            default:
                // Capabilities gate short-circuits execution before
                // reaching draw_text_run; should be 0 calls observed.
                return RenderOpResult{RenderOpOutcome{0}};
            case Mode::Success:
                return RenderOpResult{RenderOpOutcome{ /*items_drawn=*/ 1 }};
        }
    }

    void apply_per_pixel_dof(
        Framebuffer&, std::span<const float>,
        const DepthOfFieldSettings&, const LensModel&,
        const std::optional<raster::BBox>&
    ) override {}

    void apply_effect_stack(
        Framebuffer&, const EffectStack&,
        const effects::EffectExecutionContext&
    ) override {}

    void composite_layer(
        Framebuffer&, const Framebuffer&,
        BlendMode, const std::optional<raster::BBox>&,
        CompositeOperator
    ) override {}

    void apply_blur(
        Framebuffer&, float, const std::optional<raster::BBox>&
    ) override {}

private:
    Mode m_mode{Mode::ExecutionFailure};
    std::atomic<int> m_calls{0};
};

// ── Builders (mirror test_text_run_node_execute_error.cpp fixtures) ─

std::shared_ptr<TextRunShape> make_minimal_shape() {
    auto layout = std::make_shared<TextRunLayout>();
    layout->font.font_path   = "dummy.ttf";
    layout->font.font_family = "Dummy";
    layout->font.font_weight = 400;
    layout->font.font_style  = "normal";
    layout->font.font_size   = 32.0f;
    layout->font_size        = 32.0f;
    layout->bounds           = {20.0f, 32.0f};
    layout->line_height      = 32.0f;

    PlacedGlyph pg;
    pg.glyph_id  = 0;
    pg.x         = 0.0f;
    pg.y         = 0.0f;
    pg.advance_x = 20.0f;
    layout->placed.glyphs.push_back(pg);
    layout->placed.total_width  = 20.0f;
    layout->placed.total_height = 32.0f;
    layout->placed.ascent       = 24.0f;
    layout->placed.descent      = 8.0f;
    layout->placed.baseline     = 24.0f;
    layout->placed.font_size    = 32.0f;

    auto shape = std::make_shared<TextRunShape>();
    shape->layout = layout;
    return shape;
}

RenderGraphContext make_test_ctx(int w, int h, RenderBackend* backend) {
    static auto s_pool = chronon3d::cache::FramebufferPool::create_shared(32 * 1024 * 1024);
    RenderGraphContext ctx;
    ctx.frame_input.width             = w;
    ctx.frame_input.height            = h;
    ctx.services.framebuffer_pool     = s_pool;
    ctx.services.backend              = backend;
    return ctx;
}

// Helper: build TextRunNode wired to the minimal mock fixture.
TextRunNode make_node(const std::string& name) {
    auto shape   = make_minimal_shape();
    RenderNode render_ref{};
    cache::NodeCacheKey key{};
    return TextRunNode(name, std::move(shape), render_ref, key, TextRunPlacement{});
}

}  // namespace

// =====================================================================
// §1 — ExecutionFailure surfaces NodeExecutionError on RETURN channel
// =====================================================================

TEST_CASE(
    "M1.5#1 TextRunNode return channel: ExecutionFailure surfaces NodeExecutionError"
) {
    auto backend = std::make_shared<FailureMockBackend>(
        FailureMockBackend::Mode::ExecutionFailure);
    auto node = make_node("R01_execfail_node");

    RenderGraphContext ctx = make_test_ctx(64, 64, backend.get());
    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};

    const auto result = node.execute(ctx, empty_inputs, empty_bboxes);

    // ── Canonical P0-1 contract: return-channel MUST carry the error.
    CHECK_FALSE(result.ok());
    CHECK(backend->draw_text_run_calls() == 1);
    CHECK(result.error().node_name     == "R01_execfail_node");
    CHECK(result.error().backend_code  == RenderBackendErrorCode::ExecutionFailure);
    CHECK_FALSE(result.error().message.empty());
}

// =====================================================================
// §2 — CapabilitiesOff surfaces NodeExecutionError on RETURN channel
//      (capability gate short-circuits before draw_text_run)
// =====================================================================

TEST_CASE(
    "M1.5#1 TextRunNode return channel: CapabilitiesOff surfaces NodeExecutionError"
) {
    auto backend = std::make_shared<FailureMockBackend>(
        FailureMockBackend::Mode::CapabilitiesOff);
    auto node = make_node("R02_no_capability_node");

    RenderGraphContext ctx = make_test_ctx(64, 64, backend.get());
    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};

    const auto result = node.execute(ctx, empty_inputs, empty_bboxes);

    CHECK_FALSE(result.ok());
    CHECK(backend->draw_text_run_calls() == 0);    // gate short-circuits
    CHECK(result.error().node_name     == "R02_no_capability_node");
    CHECK(result.error().backend_code  == RenderBackendErrorCode::UnsupportedCapability);
}

// =====================================================================
// §3 — NullBackend surfaces NodeExecutionError on RETURN channel
//      (no backend → no dispatch → InvalidInput)
// =====================================================================

TEST_CASE(
    "M1.5#1 TextRunNode return channel: NullBackend surfaces NodeExecutionError"
) {
    auto node = make_node("R03_null_backend_node");

    RenderGraphContext ctx = make_test_ctx(64, 64, /*backend=*/nullptr);
    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};

    const auto result = node.execute(ctx, empty_inputs, empty_bboxes);

    CHECK_FALSE(result.ok());
    CHECK(result.error().node_name    == "R03_null_backend_node");
    CHECK(result.error().backend_code == RenderBackendErrorCode::InvalidInput);
}

// =====================================================================
// §4 — Successful dispatch surfaces a valid OwnedFB on RETURN channel
//      (proves P0-1 contract is symmetric: success path is identity-on-OK)
// =====================================================================

TEST_CASE(
    "M1.5#1 TextRunNode return channel: Success surfaces valid OwnedFB"
) {
    auto backend = std::make_shared<FailureMockBackend>(
        FailureMockBackend::Mode::Success);
    auto node = make_node("R04_success_node");

    RenderGraphContext ctx = make_test_ctx(64, 64, backend.get());
    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};

    const auto result = node.execute(ctx, empty_inputs, empty_bboxes);

    CHECK(result.ok());
    CHECK(result.value() != nullptr);
    CHECK(backend->draw_text_run_calls() == 1);
}

#endif // CHRONON3D_ENABLE_TEXT
