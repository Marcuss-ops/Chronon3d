// ═══════════════════════════════════════════════════════════════════════════
// tests/render_graph/nodes/test_text_run_node_execute_error.cpp
//
//   P0-3 — audit hotspot #2 regression coverage.
//
// The audit identifies a STRUCTURAL bug: TextRunNode::execute() returns
// `OwnedFB` (no error channel).  When the backend's draw_text_run()
// returns a RenderBackendError, the node:
//   (a) logs spdlog::error with node name + error code name + message;
//   (b) falls through to `return fb` regardless.
// Step (b) means a backend failure produces an apparently valid frame
// that the executor cannot distinguish from a successful all-zeros
// render.  This is what the audit calls "the false success pattern."
//
// This file does NOT solve the structural problem (that needs a
// architectural shift to `Result<OwnedFB, NodeExecutionError>` or a
// failure-collector in the executor — both post-baseline-verde).  What
// it does is LOCK the CURRENT diagnostic contract: every backend
// failure path emits an observable spdlog error with the right
// content.  That machine-checkable surface proves the system is not
// silently swallowing the error today, and provides a regression
// lock against re-introducing silent failure during the freeze.
//
// Test matrix (must all PASS on current main@2989b91d):
//   §1 draw_text_run ExecutionFailure → spdlog error with
//       "draw_text_run failed" + node name + error code name.
//   §2 capabilities().text_run == false → spdlog error with
//       "does not support" + node name, throttled to ONE call.
//   §3 backend is null → spdlog error with
//       "backend is null" + node name, throttled to ONE call.
//
// Framework: doctest.  Gated by CHRONON3D_ENABLE_TEXT (TEXT requires
// RenderTextShape + text layout types transitively pulled by
// text_run_node.hpp).  Pattern precedent:
//   tests/render_graph/core/test_node_identity.cpp (CompileTestNode shape)
//   tests/render_graph/executor/test_framebuffer_lifetime.cpp (MockNode + pool)
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

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
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef CHRONON3D_ENABLE_TEXT

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

// ── Minimal capturing spdlog sink ───────────────────────────────────────

struct CapturedMessage {
    spdlog::level::level_enum level;
    std::string              payload;
};

/// Custom spdlog sink that captures every (level, payload) tuple into a
/// thread-safe vector.  Used by ScopedLogCapture to verify that the
/// execution path emitted the contractually required diagnostic.
class CaptureSink final : public spdlog::sinks::sink {
public:
    void log(const spdlog::details::log_msg& msg) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back({
            msg.level,
            std::string(msg.payload.begin(), msg.payload.end())});
    }
    void flush() override {}
    void set_pattern(const std::string& /*p*/) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter> /*f*/) override {}

    std::vector<CapturedMessage> snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<CapturedMessage> m_messages;
};

/// Installs a capturing logger as the spdlog default for the test's
/// lifetime and restores the previous default on destruction.
/// Used by every TEST_CASE in this file to capture the spdlog emit from
/// TextRunNode::execute() without contaminating other tests.
class ScopedLogCapture {
public:
    ScopedLogCapture() {
        m_previous = spdlog::default_logger();
        m_sink = std::make_shared<CaptureSink>();
        m_logger = std::make_shared<spdlog::logger>(
            "test_text_run_error_cap", m_sink);
        m_logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(m_logger);
    }
    ~ScopedLogCapture() {
        spdlog::set_default_logger(m_previous);
    }
    ScopedLogCapture(const ScopedLogCapture&) = delete;
    ScopedLogCapture& operator=(const ScopedLogCapture&) = delete;

    const CaptureSink* sink() const noexcept { return m_sink.get(); }

private:
    std::shared_ptr<spdlog::logger> m_previous;
    std::shared_ptr<CaptureSink>    m_sink;
    std::shared_ptr<spdlog::logger> m_logger;
};

// ── Mock backend with selectable failure mode ──────────────────────────

/// Mock RenderBackend exercising one of three failure surfaces of
/// TextRunNode::execute().  Each enum value selects a distinct
/// error path:
///   - CapabilitiesOff  → capabilities gate (line 282 of TextRunNode.cpp)
///   - ExecutionFailure  → draw_text_run returns ExecutionFailure (line 283-291)
///   - InvalidInput      → draw_text_run returns InvalidInput (line 283-291)
class FailureMockBackend final : public RenderBackend {
public:
    enum class Mode {
        CapabilitiesOff,
        ExecutionFailure,
        InvalidInput,
    };

    explicit FailureMockBackend(Mode m) : m_mode(m) {}

    RenderCapabilities capabilities() const noexcept override {
        const bool text_run = (m_mode != Mode::CapabilitiesOff);
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
                    "deterministic mock: ExecutionFailure"
                }};
            case Mode::InvalidInput:
                return RenderOpResult{RenderBackendError{
                    graph::RenderBackendErrorCode::InvalidInput,
                    "deterministic mock: InvalidInput"
                }};
            case Mode::CapabilitiesOff:
            default:
                // Should never be called — capabilities gate short-circuits.
                return RenderOpResult{RenderOpOutcome{0}};
        }
    }

    // ── Concrete overrides for the remaining pure virtuals
    // Required so std::make_shared<FailureMockBackend>(...) can instantiate
    // the class.  P0-3 exercises only TextRunNode::execute(), so these
    // paths are unreachable; no-op stubs are safe and add no runtime cost.
    void apply_per_pixel_dof(
        Framebuffer& /*fb*/,
        std::span<const float> /*depth*/,
        const DepthOfFieldSettings& /*dof*/,
        const LensModel& /*lens*/,
        const std::optional<raster::BBox>& /*clip*/
    ) override {
        // no-op: FailureMockBackend never exercises DOF.
    }

    void apply_effect_stack(
        Framebuffer& /*fb*/,
        const EffectStack& /*effects*/,
        const effects::EffectExecutionContext& /*ctx*/
    ) override {
        // no-op: FailureMockBackend never exercises effect stacks.
    }

    void composite_layer(
        Framebuffer& /*dest*/,
        const Framebuffer& /*src*/,
        BlendMode /*mode*/,
        const std::optional<raster::BBox>& /*clip*/,
        CompositeOperator /*op*/
    ) override {
        // no-op: FailureMockBackend never composites.
    }

    void apply_blur(
        Framebuffer& /*fb*/,
        float /*radius*/,
        const std::optional<raster::BBox>& /*clip*/
    ) override {
        // no-op: FailureMockBackend never blurs.
    }

private:
    Mode m_mode{Mode::ExecutionFailure};
    std::atomic<int> m_calls{0};
};

// ── Builders ────────────────────────────────────────────────────────────

/// Build a minimal valid TextRunShape that satisfies the early
/// execute() guards (layout non-null, glyphs non-empty, font_path
/// non-empty, framebuffer non-zero, bbox intersecting framebuffer)
/// without requiring a real FontEngine or font cache.
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
    shape->layout = layout;  // implicit shared_ptr<TextRunLayout> → shared_ptr<const TextRunLayout>
    return shape;
}

/// Build a RenderGraphContext that funnels acquire_owned_fb() through a
/// real FramebufferPool.  The pool is shared across the 3 tests in
/// this TU (defined at function scope using `static` so it's
/// constructed once on first call and reused across TEST_CASEs).
RenderGraphContext make_text_run_ctx(int w, int h, RenderBackend* backend) {
    static auto s_pool = chronon3d::cache::FramebufferPool::create_shared(32 * 1024 * 1024);
    RenderGraphContext ctx;
    ctx.frame_input.width       = w;
    ctx.frame_input.height      = h;
    ctx.services.framebuffer_pool = s_pool;
    ctx.services.backend        = backend;
    // diagnostics_enabled stays false by default to avoid debug-log noise.
    return ctx;
}

} // namespace


// ═══════════════════════════════════════════════════════════════════════
// §1 — draw_text_run returning ExecutionFailure surfaces an error log.
// ═══════════════════════════════════════════════════════════════════════
//
// PROVES: when the backend's draw_text_run() returns an error result,
// the node DOES emit a spdlog::error containing the failure code name +
// node name + the "draw_text_run failed" signature.  This is the
// diagnostic surface that, even without an architectural fix,
// demonstrates the failure is observable rather than silent.

TEST_CASE("TextRunNode: draw_text_run ExecutionFailure surfaces diagnostic (P0-3 audit #2)") {
    ScopedLogCapture capture;
    auto backend = std::make_shared<FailureMockBackend>(
        FailureMockBackend::Mode::ExecutionFailure);

    auto shape   = make_minimal_shape();
    RenderNode render_ref{};
    cache::NodeCacheKey key{};
    TextRunNode node("P03_execfail_node", shape, render_ref, key);

    RenderGraphContext ctx = make_text_run_ctx(64, 64, backend.get());

    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};
    OwnedFB fb;
    // P0-1 — seed the frame_error slot so the node can surface the
    // backend failure.  Without this, the node's ctx.frame_error is
    // null (no-op), and the test only validates the spdlog diagnostic.
    ctx.frame_error = std::make_shared<std::optional<NodeExecutionError>>();
    REQUIRE_NOTHROW(fb = node.execute(ctx, empty_inputs, empty_bboxes));

    // Locked invariant: the dispatch reached the backend exactly once.
    CHECK(backend->draw_text_run_calls() == 1);

    // Locked invariant: at least one spdlog error with the full
    // "draw_text_run failed" + node name + code name signature.
    int found_fail_msg = 0;
    bool found_node_name = false;
    bool found_code_name = false;
    for (const auto& m : capture.sink()->snapshot()) {
        if (m.level < spdlog::level::err) continue;
        const bool has_failed   = m.payload.find("draw_text_run failed")   != std::string::npos;
        const bool has_nodename = m.payload.find("P03_execfail_node")      != std::string::npos;
        const bool has_codename = m.payload.find("ExecutionFailure")      != std::string::npos;
        if (has_failed) ++found_fail_msg;
        if (has_nodename) found_node_name = true;
        if (has_codename) found_code_name = true;
    }
    CHECK(found_fail_msg >= 1);
    CHECK(found_node_name);
    CHECK(found_code_name);

    // P0-1 — locked invariant: the node MUST surface the backend
    // failure through the frame_error slot so the executor can
    // propagate it to frame-level failure.
    REQUIRE(ctx.frame_error);
    REQUIRE(ctx.frame_error->has_value());
    CHECK(ctx.frame_error->value().node_name == "P03_execfail_node");
    CHECK(ctx.frame_error->value().backend_code == RenderBackendErrorCode::ExecutionFailure);
}


// ═══════════════════════════════════════════════════════════════════════
// §2 — capabilities().text_run == false → diagnostic error emitted.
//      (Fase A6: m_backend_warned throttle removed — node returns
//       NodeExecutionError immediately, so the error only fires once.)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunNode: absent text_run capability is diagnosed once per node (P0-3)") {
    ScopedLogCapture capture;
    auto backend = std::make_shared<FailureMockBackend>(
        FailureMockBackend::Mode::CapabilitiesOff);

    auto shape   = make_minimal_shape();
    RenderNode render_ref{};
    cache::NodeCacheKey key{};
    TextRunNode node("P03_no_capability_node", shape, render_ref, key);

    RenderGraphContext ctx = make_text_run_ctx(64, 64, backend.get());

    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};

    // P0-1 — seed frame_error slot so the node can surface the failure.
    ctx.frame_error = std::make_shared<std::optional<NodeExecutionError>>();

    // First call → diagnostic fires.
    {
        OwnedFB fb;
        REQUIRE_NOTHROW(fb = node.execute(ctx, empty_inputs, empty_bboxes));
    }
    // Second call → still returns error (Fase A6: no throttle —
    // node returns NodeExecutionError immediately each time).
    {
        OwnedFB fb;
        REQUIRE_NOTHROW(fb = node.execute(ctx, empty_inputs, empty_bboxes));
    }

    // Locked invariant: capability gate prevented draw_text_run dispatch.
    CHECK(backend->draw_text_run_calls() == 0);

    // Locked invariant: exactly ONE diagnostic error matching the
    // "does not support" + node name signature (Fase A6: node returns
    // NodeExecutionError immediately — no throttle, but error only logs
    // once per execute call which aborts the frame).
    int support_err_count = 0;
    bool contains_node_name = false;
    for (const auto& m : capture.sink()->snapshot()) {
        if (m.level < spdlog::level::err) continue;
        if (m.payload.find("does not support") != std::string::npos &&
            m.payload.find("P03_no_capability_node") != std::string::npos) {
            ++support_err_count;
            contains_node_name = true;
        }
    }
    CHECK(support_err_count == 1);
    CHECK(contains_node_name);

    // P0-1 — locked invariant: the node MUST surface the capability
    // failure through frame_error.
    REQUIRE(ctx.frame_error);
    REQUIRE(ctx.frame_error->has_value());
    CHECK(ctx.frame_error->value().node_name == "P03_no_capability_node");
    CHECK(ctx.frame_error->value().backend_code == RenderBackendErrorCode::UnsupportedCapability);
}


// ═══════════════════════════════════════════════════════════════════════
// §3 — backend is null → diagnostic error emitted.
//      (Fase A6: m_backend_warned throttle removed — node returns
//       NodeExecutionError immediately.)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunNode: null backend is diagnosed once per node (P0-3)") {
    ScopedLogCapture capture;

    auto shape   = make_minimal_shape();
    RenderNode render_ref{};
    cache::NodeCacheKey key{};
    TextRunNode node("P03_null_backend_node", shape, render_ref, key);

    RenderGraphContext ctx = make_text_run_ctx(64, 64, /*backend=*/nullptr);

    std::span<const FramebufferRef> empty_inputs{};
    std::span<const std::optional<raster::BBox>> empty_bboxes{};

    // P0-1 — seed frame_error slot.
    ctx.frame_error = std::make_shared<std::optional<NodeExecutionError>>();

    {
        OwnedFB fb;
        REQUIRE_NOTHROW(fb = node.execute(ctx, empty_inputs, empty_bboxes));
    }
    {
        OwnedFB fb;
        REQUIRE_NOTHROW(fb = node.execute(ctx, empty_inputs, empty_bboxes));
    }

    int null_err_count = 0;
    bool contains_node_name = false;
    for (const auto& m : capture.sink()->snapshot()) {
        if (m.level < spdlog::level::err) continue;
        if (m.payload.find("backend is null") != std::string::npos &&
            m.payload.find("P03_null_backend_node") != std::string::npos) {
            ++null_err_count;
            contains_node_name = true;
        }
    }
    CHECK(null_err_count == 1);
    CHECK(contains_node_name);

    // P0-1 — locked invariant: null backend MUST be surfaced through
    // frame_error.
    REQUIRE(ctx.frame_error);
    REQUIRE(ctx.frame_error->has_value());
    CHECK(ctx.frame_error->value().node_name == "P03_null_backend_node");
    CHECK(ctx.frame_error->value().backend_code == RenderBackendErrorCode::InvalidInput);
}

#endif // CHRONON3D_ENABLE_TEXT
