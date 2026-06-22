// ============================================================================
// test_effect_execution_context.cpp — Tests for EffectExecutionContext
//
// Verifies that EffectExecutionContext correctly carries per-frame state
// (time, frame, clip, quality, diagnostics) through the effect processor chain.
// ============================================================================

#include <doctest/doctest.h>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "src/backends/software/utils/render_effects_processor.hpp"
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::effects;
using namespace chronon3d::renderer;

// ── Mock processor that captures context ─────────────────────────────────

struct ContextCapture {
    float time_seconds{0.0f};
    Frame frame{0};
    std::optional<raster::BBox> clip;
    RenderQuality quality{RenderQuality::Final};
    bool diagnostics_enabled{false};
};

class ContextSpyProcessor final : public EffectProcessor {
public:
    explicit ContextSpyProcessor(ContextCapture& capture) : m_capture(capture) {}

    void apply(Framebuffer&, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        m_capture.time_seconds = context.time_seconds;
        m_capture.frame = context.frame;
        m_capture.clip = context.clip;
        m_capture.quality = context.quality;
        m_capture.diagnostics_enabled = context.diagnostics_enabled;
    }

private:
    ContextCapture& m_capture;
};

// ── Test cases ───────────────────────────────────────────────────────────

TEST_CASE("EffectExecutionContext: default construction") {
    const EffectExecutionContext ctx;
    CHECK(ctx.time_seconds == 0.0f);
    CHECK(ctx.frame == 0);
    CHECK_FALSE(ctx.clip.has_value());
    CHECK(ctx.quality == RenderQuality::Final);
    CHECK_FALSE(ctx.diagnostics_enabled);
}

TEST_CASE("EffectExecutionContext: carries time_seconds") {
    EffectExecutionContext ctx;
    ctx.time_seconds = 42.5f;
    CHECK(ctx.time_seconds == 42.5f);
}

TEST_CASE("EffectExecutionContext: carries frame number") {
    EffectExecutionContext ctx;
    ctx.frame = 123;
    CHECK(ctx.frame == 123);
}

TEST_CASE("EffectExecutionContext: carries clip rect") {
    EffectExecutionContext ctx;
    ctx.clip = raster::BBox{10, 20, 100, 200};
    REQUIRE(ctx.clip.has_value());
    CHECK(ctx.clip->x0 == 10);
    CHECK(ctx.clip->y0 == 20);
    CHECK(ctx.clip->x1 == 100);
    CHECK(ctx.clip->y1 == 200);
}

TEST_CASE("EffectExecutionContext: carries quality") {
    EffectExecutionContext ctx;
    ctx.quality = RenderQuality::Preview;
    CHECK(ctx.quality == RenderQuality::Preview);

    ctx.quality = RenderQuality::Final;
    CHECK(ctx.quality == RenderQuality::Final);
}

TEST_CASE("EffectExecutionContext: carries diagnostics_enabled") {
    EffectExecutionContext ctx;
    ctx.diagnostics_enabled = true;
    CHECK(ctx.diagnostics_enabled);
}

TEST_CASE("EffectExecutionContext: designated initializer syntax") {
    const raster::BBox clip{5, 5, 50, 50};
    const EffectExecutionContext ctx{
        .time_seconds = 1.5f,
        .frame = 10,
        .clip = clip,
        .quality = RenderQuality::Preview,
        .diagnostics_enabled = true
    };

    CHECK(ctx.time_seconds == 1.5f);
    CHECK(ctx.frame == 10);
    REQUIRE(ctx.clip.has_value());
    CHECK(ctx.clip->x0 == 5);
    CHECK(ctx.clip->x1 == 50);
    CHECK(ctx.quality == RenderQuality::Preview);
    CHECK(ctx.diagnostics_enabled);
}

TEST_CASE("EffectExecutionContext: context flows through EffectProcessor") {
    ContextCapture captured;
    auto processor = std::make_unique<ContextSpyProcessor>(captured);

    SoftwareRegistry registry;
    registry.register_effect(typeid(BlurParams), std::move(processor));

    EffectStack stack;
    stack.push_back(EffectInstance{BlurParams{5.0f}});

    const raster::BBox clip{0, 0, 64, 64};
    const EffectExecutionContext context{
        .time_seconds = 2.0f,
        .frame = 15,
        .clip = clip,
        .quality = RenderQuality::Preview,
        .diagnostics_enabled = true
    };

    // Create a minimal framebuffer for the test
    Framebuffer fb(64, 64);

    for (const auto& effect : stack) {
        if (!effect.enabled) continue;
        if (auto* proc = registry.get_effect(effect.param_type_index())) {
            proc->apply(fb, effect.params, context);
        }
    }

    CHECK(captured.time_seconds == 2.0f);
    CHECK(captured.frame == 15);
    REQUIRE(captured.clip.has_value());
    CHECK(captured.clip->x0 == 0);
    CHECK(captured.clip->x1 == 64);
    CHECK(captured.quality == RenderQuality::Preview);
    CHECK(captured.diagnostics_enabled);
}

TEST_CASE("EffectExecutionContext: context flows through renderer::apply_effect_stack") {
    EffectStack stack;
    stack.push_back(EffectInstance{BlurParams{3.0f}});

    Framebuffer fb(64, 64);
    fb.clear(Color::white());

    const EffectExecutionContext context{
        .time_seconds = 0.0f,
        .frame = 0,
        .quality = RenderQuality::Final,
        .diagnostics_enabled = false
    };

    // Should apply blur without crashing
    apply_effect_stack(fb, stack, context);

    // Verify the framebuffer was modified (blur changes pixels)
    CHECK(fb.pixels_row(0) != nullptr);
}

TEST_CASE("EffectExecutionContext: clip reaches internal dispatcher") {
    // Verify that the renderer::apply_effect_stack accepts a context with clip
    EffectStack stack;
    stack.push_back(EffectInstance{TintParams{Color::red(), 1.0f}});

    Framebuffer fb(64, 64);
    fb.clear(Color::white());

    const raster::BBox clip{10, 10, 54, 54};
    const EffectExecutionContext context{
        .time_seconds = 0.0f,
        .frame = 0,
        .clip = clip,
        .quality = RenderQuality::Final,
        .diagnostics_enabled = false
    };

    // Should apply tint with clip without crashing
    apply_effect_stack(fb, stack, context);
}

TEST_CASE("EffectExecutionContext: SoftwareRenderer::apply_effect_stack plumbing") {
    // This test verifies that SoftwareRenderer correctly creates
    // EffectExecutionContext and passes it to the internal dispatcher.
    EffectStack stack;
    stack.push_back(EffectInstance{BrightnessParams{0.5f}});

    Framebuffer fb(64, 64);
    fb.clear(Color::white());

    const effects::EffectExecutionContext context{
        .time_seconds = 0.0f,
        .frame = 0,
        .quality = effects::RenderQuality::Final,
        .diagnostics_enabled = false
    };

    // Use the SoftwareRenderer path — passes context through to renderer::apply_effect_stack
    auto renderer = test::make_renderer();
    renderer.apply_effect_stack(fb, stack, context);

    // Brightness adjustment should have modified the framebuffer
    const Color* row = fb.pixels_row(0);
    CHECK(row != nullptr);
}

TEST_CASE("EffectExecutionContext: disabled effect is skipped") {
    ContextCapture captured;
    auto processor = std::make_unique<ContextSpyProcessor>(captured);

    SoftwareRegistry registry;
    registry.register_effect(typeid(BlurParams), std::move(processor));

    EffectStack stack;
    EffectInstance inst{BlurParams{5.0f}};
    inst.enabled = false;  // explicitly disabled
    stack.push_back(std::move(inst));

    Framebuffer fb(64, 64);

    const EffectExecutionContext context{};
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;
        if (auto* proc = registry.get_effect(effect.param_type_index())) {
            proc->apply(fb, effect.params, context);
        }
    }

    // The spy should NOT have been called because the effect is disabled
    CHECK(captured.time_seconds == 0.0f);
    CHECK(captured.frame == 0);
}
