#pragma once

// ---------------------------------------------------------------------------
// CLI Initialisation Hooks
//
// PR 2: Explicit registration — compositions are added directly to the
// CompositionRegistry via register_content_modules() and
// register_builtin_compositions() which now take a registry reference.
//
// PR 4: AssetRegistry de-singletonized — thread-local pointer set
// unconditionally so asset resolution works with or without content modules.
// ---------------------------------------------------------------------------

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>

// AE parity camera visual comparison scenes (10 compositions, always registered)
#include "tests/visual/ae_parity/ae_parity_scenes.hpp"
// TICKET-AE-PARITY-FLOOR-DASHBOARD — 5 new cinematic scene compositions
// (ae_08_glow_pulse, ae_10_scale_pop, ae_12_random_character_jitter,
//  ae_14_multiline_9_16, motion_blur_text) registered as CLI compositions
// so they can be rendered via `chronon3d_cli render <comp_id>` and show
// up in the telemetry dashboard.  Source: tests/text_golden/ae_parity/* +
// tests/text_golden/motion_blur_text/* (landed in commits 3ddbbdff/45be2b40).
#include "tests/visual/ae_parity/ae_parity_compositions.hpp"
// Camera 3D projection truth test
#include "tests/visual/camera_truth/camera_truth_test.hpp"
// Camera orbit truth test (OrbitMotion via CameraDescriptor)
#include "tests/visual/camera_truth/camera_truth_orbit.hpp"
// AE Camera Text Parity — 360-frame multi-segment stress test
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include "content/experimental/ae-parity/ae_camera_text_parity.hpp"
#endif

#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
#include <content/register_content_modules.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/authoring/detail/basic_registry.hpp>            // PR 3.5
#include <chronon3d/authoring/style_registry.hpp>                    // PR 3.5
#include <chronon3d/authoring/motion_registry.hpp>                    // PR 3.5
#endif

namespace chronon3d::cli {

// cli_asset_registry() REMOVED — the global mutable static AssetRegistry
// was a concurrency hazard (daemon, watch mode, parallel render jobs).
// AssetRegistry is now created in main() and threaded through CliContext.
// Per-renderer asset mounting happens in create_renderer() via
// renderer->runtime().assets().mount(cwd) + resolver().mount(cwd).

/// PR 3.5 — returns the CLI-wide static StyleRegistry + MotionRegistry.
/// Created once, shared between authoring-time text builders. Host code
/// populates these via dedicated API when ready; the default factories
/// register nothing so unintended wildcards never resolve.
///
/// Conditionally compiled: the inline definitions reference
/// `authoring::StyleRegistry` / `authoring::MotionRegistry`, which are
/// only forward-visible when `CHRONON3D_BUILD_CONTENT` or
/// `CHRONON3D_BUILD_DIAGNOSTICS` is defined (the same #if that pulls in
/// `style_registry.hpp` / `motion_registry.hpp` / `basic_registry.hpp`
/// above).  Without the macros the type names are unknown and the
/// pre-existing inline bodies fail to compile, so the definitions are
/// guarded by the same #if.  The functions are unused on builds that
/// don't define either macro (no caller path exists in those TUs), so
/// the conditional definition is safe.
#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
inline authoring::StyleRegistry&  cli_style_registry() {
    static authoring::StyleRegistry  reg;
    return reg;
}
inline authoring::MotionRegistry& cli_motion_registry() {
    static authoring::MotionRegistry reg;
    return reg;
}
#endif

/// Register built-in content compositions and built-in compositions
/// into the given registry.  Safe to call multiple times.
inline void init_compositions(CompositionRegistry& registry, AssetRegistry& assets) {

#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
    static ExtensionCatalog content_catalog;
    // Build a minimal ExtensionContext — only compositions is used here.
    // graph_nodes, effects, assets are not needed for composition registration.
    // PR 3.5 — populate the ambient authoring registries so the CLI-wide
    // chronon3d::authoring façades can resolve `.style(id)` / `.motion(id)`
    // ambient.  Default-empty; composition packs (or test harnesses) can
    // register their own style/motion ids via the cli_*_registry() handles.
    static graph::GraphNodeCatalog             dummy_nodes;
    static effects::EffectCatalog              dummy_effects;
    static authoring::StyleRegistry&           styles  = cli_style_registry();
    static authoring::MotionRegistry&          motions = cli_motion_registry();
    ExtensionContext ctx{registry, dummy_nodes, dummy_effects, assets,
                          &styles, &motions};
    chronon3d::register_content_modules(content_catalog, ctx);
#endif
    // Register non-content built-in compositions (DarkGridBackground,
    // CameraImageClip).
    chronon3d::register_builtin_compositions(registry);

    // Register AE parity camera visual comparison scenes (AE_CAM_01–AE_CAM_10).
    // Inlined here only when content is off — when CHRONON3D_BUILD_CONTENT=ON
    // they arrive via register_content_modules() above (content/ae_parity
    // is compiled into chronon3d_content, so manual registration would
    // cause a duplicate).
#ifndef CHRONON3D_BUILD_CONTENT
    registry.add("AE_CAM_01_static_grid",   [](const CompositionProps&) { return test::make_ae_cam_01_static_grid(); });
    registry.add("AE_CAM_02_zoom_fov",      [](const CompositionProps&) { return test::make_ae_cam_02_zoom_fov(); });
    registry.add("AE_CAM_03_two_node_poi",  [](const CompositionProps&) { return test::make_ae_cam_03_two_node_poi(); });
    registry.add("AE_CAM_04_parent_null",   [](const CompositionProps&) { return test::make_ae_cam_04_parent_null(); });
    registry.add("AE_CAM_05_orbit",         [](const CompositionProps&) { return test::make_ae_cam_05_orbit(); });
    registry.add("AE_CAM_06_dolly_zoom",    [](const CompositionProps&) { return test::make_ae_cam_06_dolly_zoom(); });
    registry.add("AE_CAM_07_gatefit",       [](const CompositionProps&) { return test::make_ae_cam_07_gatefit(); });
    registry.add("AE_CAM_08_dof",           [](const CompositionProps&) { return test::make_ae_cam_08_dof(); });
    registry.add("AE_CAM_09_motion_blur",   [](const CompositionProps&) { return test::make_ae_cam_09_motion_blur(); });
    registry.add("AE_CAM_10_near_clip",     [](const CompositionProps&) { return test::make_ae_cam_10_near_clip(); });
#endif  // !CHRONON3D_BUILD_CONTENT

    // Camera 3D projection truth test
    registry.add("CameraTruthTest", [](const CompositionProps&) { return test::make_camera_truth_test(); });

    // Camera orbit truth test (OrbitMotion via CameraDescriptor, yaw 0→90°)
    registry.add("CameraTruthOrbit", [](const CompositionProps&) { return test::make_camera_truth_orbit(); });

#ifndef CHRONON3D_BUILD_DIAGNOSTICS
    // AE Camera Text Parity — 360-frame multi-segment stress test
    // (static / dolly-zoom / orbit / rack-focus / whip-pan+motion-blur / stress)
    // When CHRONON3D_BUILD_DIAGNOSTICS is ON, this composition is already
    // registered via register_content_modules() → register_anim_compositions()
    // in content/animation_compositions.cpp (guarded by the same macro).
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    registry.add("AECameraTextParity", [](const CompositionProps&) { return chronon3d::content::anims::ae_camera_text_parity(); });
#endif
#endif

    // TICKET-AE-PARITY-FLOOR-DASHBOARD — 5 new cinematic scene
    // compositions (Phase 1 scenes 08/10/12/14 + motion_blur_text).  Each
    // derives per-frame state (opacity/scale/jitter/blur) from
    // `ctx.frame.integral() % 30` inside the runtime lambda, matching the
    // 0/15/30 snapshot buckets used by the test files.  CLI invocations:
    //   chronon3d_cli render ae_08_glow_pulse -o /tmp/ae_08.png --frame 15
    //   chronon3d_cli render ae_10_scale_pop -o /tmp/ae_10.png --frame 15
    //   chronon3d_cli render ae_12_random_character_jitter -o /tmp/ae_12.png --frame 15
    //   chronon3d_cli render ae_14_multiline_landscape -o /tmp/ae_14.png --frame 0
    //   chronon3d_cli render motion_blur_text -o /tmp/mb.png --frame 10
    // Each render produces 1 row in render_runs SQLite with
    // git_commit_short = 3ddbbdff or 45be2b40 (the SHAs that landed the
    // matching test code), visible in http://149.56.131.97:5173/.
    registry.add("ae_08_glow_pulse",               [](const CompositionProps& p) { return test::make_ae_08_glow_pulse(p); });
    registry.add("ae_10_scale_pop",                [](const CompositionProps& p) { return test::make_ae_10_scale_pop(p); });
    registry.add("ae_12_random_character_jitter",  [](const CompositionProps& p) { return test::make_ae_12_random_character_jitter(p); });
    registry.add("ae_14_multiline_landscape",     [](const CompositionProps& p) { return test::make_ae_14_multiline_landscape(p); });
    registry.add("motion_blur_text",               [](const CompositionProps& p) { return test::make_motion_blur_text(p); });
}

} // namespace chronon3d::cli
