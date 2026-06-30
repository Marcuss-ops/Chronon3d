// tests/install_consumer/main.cpp
//
// ── Step 5 — Strict (A) manifest-only consumer ──
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// P3-5 (after user's "Strict (A) rewrite" choice):
//   The consumer references ONLY types reachable from the V0.1 manifest
//   (cmake/Chronon3DPublicHeaders.cmake) via the umbrella
//   chronon3d/chronon3d.hpp + transitives documented in ADR-012 as
//   "manifest scope" (FrameContext, Scene, Composition, SceneFunction,
//   CompositionSpec, RenderEngine, save_png, Framebuffer).
//
//   Specifically:
//     - NO `s.layer("bg", ...)` / `s.layer("title", ...)` / `s.camera(...)`
//       SceneBuilder wiring (removed; these are OPP-internal authoring
//       surface).
//     - NO `l.grid_background(...)` / `l.text(...)` (removed; same).
//     - NO active camera / non-zero rotation / non-zero zoom (removed).
//     - NO kMinNonZeroPixels > 0 threshold (removed; an empty SceneFn
//       yields a near-empty framebuffer; the all-black result is the
//       architecturally-correct outcome of strict-A).
//
//   The seal-discussion test surface therefore REDUCES from
//   "renders a meaningful test PNG with text/grid/camera" (Fase-6 spec,
//   2026-06-23) to "demonstrates that an installed Chronon3D package can
//   be linked, a Composition-compatible object constructed, a render
//   call invoked, and a PNG emitted to disk." Pixel-count/identity is
//   no longer the gate — install-link + boundary-marker-emitted is.
//
// Wired into CTest via the top-level CMakeLists.txt option
// CHRONON3D_BUILD_INSTALL_CONSUMER_TEST (enabled by the linux-ci
// preset).  Orchestrator: tools/install_consumer_test.sh runs the full
// configure -> build -> install -> consume -> run chain. Note: gate 10
// currently FAILs upstream of the consumer build on the OPP-side
// CMP0077 INTERFACE include leak (deferred Commit C from Step 3); that
// blocker is INDEPENDENT of the consumer's surface discipline asserted
// here.

#include <chronon3d/chronon3d.hpp>   // single direct include — manifest entry 1 (per ADR-012 umbrella doctrine)

#include <cstddef>
#include <cstdio>
#include <filesystem>

int main() {
    // ── 1. Minimal Composition (manifest-reachable via umbrella) ──────
    //
    // CompositionSpec + SceneFunction are reachable transitively from
    // chronon3d.hpp → composition.hpp. The SceneFn parameter is
    // mandatory for chronological API stability. The body returns an
    // EMPTY Scene — strict (A) intent: no authored layers, no camera,
    // no text. The render will produce a near-empty framebuffer; this
    // is the architecturally-correct outcome of the strict rewrite.
    chronon3d::Composition comp{
        chronon3d::CompositionSpec{
            .name     = "StrictAMinimalConsumer",
            .width    = 640,
            .height   = 360,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration = 1,
            .assets_root = "",
        },
        // SceneFunction: empty, returns default Scene{}. The Scene
        // class is OPP-internal (scene/model/core/scene.hpp) but
        // transitively reachable per ADR-012's "manifest scope"
        // doctrine. The lambda must accept a FrameContext parameter
        // (also transitively reachable).
        [](const chronon3d::FrameContext&) -> chronon3d::Scene {
            return chronon3d::Scene{};
        },
    };

    // ── 2. Render ───────────────────────────────────────────────────
    //
    // chronon3d::RenderEngine::render(comp, frame) returns
    // std::shared_ptr<Framebuffer> (OPP-side; SDK alternative is
    // chronon3d::sdk::RenderEngine returning Result<RenderOutput,
    // RenderError>). Both are manifest-reachable; OPP-side picked
    // here for shared_ptr ergonomics (no Result wrapper unwrap).
    chronon3d::RenderEngine engine;
    auto fb = engine.render(comp, 0);
    if (!fb) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] render_frame returned null\n");
        return 1;
    }

    // ── 3. Save PNG (chronon3d::save_png helper; manifest-reachable) ──
    const std::filesystem::path output_path = "sdk_consumer_output.png";
    if (!chronon3d::save_png(*fb, output_path.string())) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] save_png failed\n");
        return 1;
    }

    // ── 4. File-level pre-condition (cheap, kept for forensics) ──────
    if (!std::filesystem::exists(output_path)) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG not found: %s\n",
                     output_path.c_str());
        return 1;
    }
    const auto file_size = std::filesystem::file_size(output_path);
    if (file_size == 0) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG is empty\n");
        return 1;
    }

    // ── 5. Boundary marker ───────────────────────────────────────────
    //
    // No pixel-count check: strict (A) expects a near-empty/all-black
    // render by design (empty SceneFn → no layers → no compositor
    // activity → blank output). File size > 0 proves a non-trivial PNG
    // was emitted (PNG header + zero data area roundtrip through the
    // SDK's save_png helper).
    std::printf("[BOUNDARY-OK] SDK consumer (strict-A minimal) rendered "
                "%dx%d PNG (%zu bytes); empty SceneFn, near-black "
                "framebuffer is the expected outcome.\n",
                fb->width(), fb->height(),
                static_cast<size_t>(file_size));
    return 0;
}
