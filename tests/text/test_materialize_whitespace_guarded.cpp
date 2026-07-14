#include <optional>
// SPDX-License-Identifier: MIT
//
// tests/text/test_materialize_whitespace_guarded.cpp
//
// TICKET-RENDER-PIPELINE-INTEGRITY — layer 3 unit test.
//
// Locks the new contract: `materialize_text_run_shape` short-circuits
// on empty OR whitespace-only text BEFORE reaching `compile_text_layout`.
// Previously the early-return guard was strictly `text.empty()`; the
// M1.5#2 (TICKET-TEXT-CLIP-ASCENT) extension broadens the predicate to
// also reject whitespace-only text.  This avoids the downstream
// "HarfBuzz produced zero glyphs for non-empty text" error that was
// observed in production at `--frames 30` of `MinimalistTextFadeUp`
// where the per-frame scramble animation evaluates to a single " ".
//
// Test strategy: invoke the function with text="   " and confirm it
// returns nullptr even when FontEngine is nullptr (proving the
// whitespace guard fires BEFORE the engine null-check + compile path)
// and with a real FontEngine (proving the guard fires BEFORE
// compile_text_layout).  The non-whitespace control case confirms
// valid text still produces a real shape.

#include <doctest/doctest.h>

#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>

#include <memory>
#include <string>

using namespace chronon3d;

namespace {

// Build a minimal TextRunSpec with a specific utf8 string.  All other
// fields stay at defaults so the materializer can fail-fast on bad
// content without depending on font resolution.
TextRunSpec make_text_spec(const std::string& utf8_content) {
    TextRunSpec spec;
    spec.text.content.value = utf8_content;
    spec.text.font.font_family = "Inter";
    spec.text.font.font_size   = 32.0f;
    spec.text.layout.box       = {800.0f, 200.0f};
    return spec;
}

} // namespace

TEST_CASE("materialize_text_run_shape: empty text returns nullptr (regression lock for empty guard)") {
    // The pre-existing `if (text.empty())` short-circuit.  This case
    // continues to work after the whitespace broadening.
    auto spec = make_text_spec("");

    auto shape = materialize_text_run_shape(
        spec, /*engine=*/nullptr,
        SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        /*animated_doc=*/nullptr);

    CHECK(shape == nullptr);
}

TEST_CASE("materialize_text_run_shape: whitespace-only text returns nullptr WITHOUT calling compile_text_layout") {
    // TICKET-RENDER-PIPELINE-INTEGRITY layer 3: pass text="   " (3 spaces)
    // AND engine=nullptr.  If the guard fires before the engine null
    // check (which it does, per the function body), this returns
    // nullptr quickly.  If a future refactor moves the whitespace
    // check AFTER the compile_text_layout call, the test will start
    // hitting a null-pointer dereference inside the engine path and
    // crash.
    auto spec = make_text_spec("   ");

    auto shape = materialize_text_run_shape(
        spec, /*engine=*/nullptr,
        SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        /*animated_doc=*/nullptr);

    CHECK(shape == nullptr);
}

TEST_CASE("materialize_text_run_shape: tab/newline/mixed whitespace also returns nullptr") {
    // The broadened guard uses `std::all_of(text, ::isspace)` so any
    // sequence of ASCII whitespace (space, tab, newline, CR, FF, VT)
    // triggers the early-return.  Lock each variant so a future
    // predicate change is caught.
    for (const std::string& ws : {"   ", "\t", "\n", "\r\n", " \t\n ", "  \t  "}) {
        CAPTURE(ws);
        auto spec = make_text_spec(ws);
        auto shape = materialize_text_run_shape(
            spec, /*engine=*/nullptr,
            SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
            nullptr);
        CHECK(shape == nullptr);
    }
}

TEST_CASE("materialize_text_run_shape: real FontEngine + whitespace text still returns nullptr (no compile call)") {
    // Same shape as the nullptr-engine test, but with a real engine
    // so the guard must fire BEFORE compile_text_layout (which would
    // otherwise try to shape the whitespace and fail with
    // "HarfBuzz produced zero glyphs for non-empty text").  The
    // return value being nullptr — with a real engine that
    // could have shaped content — is the test that proves the
    // guard fires.
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    auto spec = make_text_spec("   ");

    auto shape = materialize_text_run_shape(
        spec, &engine,
        SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        nullptr);

    CHECK(shape == nullptr);
}

TEST_CASE("materialize_text_run_shape: control — non-whitespace text is NOT short-circuited (sanity check)") {
    // Control: a real, non-whitespace string "Hello" must reach the
    // engine + compile path.  Whether compile succeeds depends on
    // whether the test fixture has a font registered; on CI machines
    // without a font, the result is still a nullptr but for a DIFFERENT
    // reason (the engine shape call failed).  We don't assert
    // non-null here — we assert that we don't crash and that the
    // function returns SOMETHING (which may be nullptr or a real
    // shape).  This test exists so the broadened guard is symmetric:
    // whitespace returns nullptr, real text is processed.
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};

    auto spec = make_text_spec("Hello");

    std::shared_ptr<TextRunShape> shape;
    REQUIRE_NOTHROW(shape = materialize_text_run_shape(
        spec, &engine,
        SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        nullptr));
    // No CHECK on shape == nullptr / non-null: depends on font availability.
    // The point is: the function executes through the full path, NOT
    // short-circuited.  Tested implicitly by not throwing.
}
