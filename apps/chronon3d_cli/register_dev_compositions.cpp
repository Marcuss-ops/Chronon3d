// ============================================================================
// apps/chronon3d_cli/register_dev_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — DEV-only composition registration.
// ENTIRE FILE gated under #ifdef CHRONON3D_BUILD_CLI_DEV.  When
// CHRONON3D_BUILD_CLI_DEV=OFF (production default), this file is empty
// and the dev compositions are not registered.
//
// Per user-spec verbatim §3 (isola runtime/dev), the DEV CLI is the ONLY
// consumer of tests/visual/ factory functions.  The production CLI does
// NOT compile tests/visual/*.cpp (see apps/chronon3d_cli/CMakeLists.txt).
//
// Registers:
//   - PipelineParityCanary                  (tests/text/pipeline_parity_canary.hpp)
//   - AnimTypewriterGlowNoGlow              (tests/visual/glow_ab/glow_ab_compositions.hpp)
//   - CameraTruthTest                       (tests/visual/camera_truth/camera_truth_test.hpp)
//   - CameraTruthOrbit                      (tests/visual/camera_truth/camera_truth_orbit.hpp)
//   - AE_CAM_01..10                         (tests/visual/ae_parity/ae_parity_scenes.hpp)
//   - ae_08_glow_pulse / ae_10_scale_pop /  (tests/visual/ae_parity/ae_parity_compositions.hpp)
//     ae_12_random_character_jitter /
//     ae_14_multiline_landscape /
//     motion_blur_text
//   - ChrononGlowFinalAE_NoGlow             (content/compositions/chronon_glow_final.hpp)
//   - AECameraTextParity                    (content/experimental/ae-parity/, DIAGNOSTICS gated)
//
// Step 8 §A: `chronon-glow-final-portrait` was MOVED to PRODUCTION as
// `ChrononGlowFinalAEPortrait` (the canonical name) — see
// apps/chronon3d_cli/register_runtime_compositions.cpp.
// ============================================================================

#ifdef CHRONON3D_BUILD_CLI_DEV

#include "register_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

// Header-only inline factory — no .cpp needed.
#include "tests/text/pipeline_parity_canary.hpp"
// .cpp-compiled factories (compiled into chronon3d_cli_dev per CMakeLists).
// Azione 17 — `make_ae_cam_01..10_*()` factory functions relocated from
// tests/visual/ae_parity/ae_parity_scenes.hpp to content/ae_parity/ae_cam_scenes.hpp
// (production-side namespace, DEV-only gating per TICKET-CLI-ISOLATE-RUNTIME-DEV).
#include "content/ae_parity/ae_cam_scenes.hpp"
#include "tests/visual/ae_parity/ae_parity_compositions.hpp"
#include "tests/visual/glow_ab/glow_ab_compositions.hpp"
#include "tests/visual/camera_truth/camera_truth_test.hpp"
#include "tests/visual/camera_truth/camera_truth_orbit.hpp"

// TICKET-CHRONON-GLOW-FINAL — portrait + NoGlow siblings (production
// header-only factory; available everywhere).
#include "content/compositions/chronon_glow_final.hpp"

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
// TICKET-AE-PARITY-360 — 360-frame multi-segment stress test.
#include "content/experimental/ae-parity/ae_camera_text_parity.hpp"
#endif

namespace chronon3d {

void register_dev_compositions(CompositionRegistry& registry) {
    // (1) PipelineParityCanary — real pipeline parity canary for
    // tests/text/test_pipeline_parity_real.cpp to compare SDK/CLI/video.
    registry.add(CompositionDescriptor{
        .id = "PipelineParityCanary",
        .factory = [](const CompositionProps& p) { return test::make_pipeline_parity_canary(p); }});

    // (2) CameraTruthTest + CameraTruthOrbit — 3D camera projection truth
    // scenes (TICKET-CAMERA-TRUTH-ROT regression lock).
    registry.add(CompositionDescriptor{
        .id = "CameraTruthTest",
        .factory = [](const CompositionProps&) { return test::make_camera_truth_test(); }});
    registry.add(CompositionDescriptor{
        .id = "CameraTruthOrbit",
        .factory = [](const CompositionProps&) { return test::make_camera_truth_orbit(); }});

    // (3) AE_CAM_01..10 — AE parity camera visual comparison scenes
    // (TICKET-CAMERA-AE-PARITY closure).  These are DEV-only per the
    // user-spec verbatim §3 (isola runtime/dev) — production CLI does
    // NOT have these compositions.  The `register_ae_parity_compositions`
    // call in content/register_content_modules.cpp was REMOVED in this
    // chore; these are now registered exclusively here in DEV.
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_01_static_grid",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_01_static_grid(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_02_zoom_fov",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_02_zoom_fov(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_03_two_node_poi",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_03_two_node_poi(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_04_parent_null",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_04_parent_null(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_05_orbit",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_05_orbit(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_06_dolly_zoom",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_06_dolly_zoom(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_07_gatefit",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_07_gatefit(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_08_dof",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_08_dof(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_09_motion_blur",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_09_motion_blur(); }});
    registry.add(CompositionDescriptor{
        .id = "AE_CAM_10_near_clip",
        .factory = [](const CompositionProps&) { return chronon3d::content::ae_parity::ae_cam_scenes::make_ae_cam_10_near_clip(); }});

    // (4) TICKET-AE-PARITY-FLOOR-DASHBOARD — 5 cinematic scene compositions
    // (ae_08_glow_pulse / ae_10_scale_pop / ae_12 / ae_14 / motion_blur_text).
    // Test/floor-dashboard, not real product.  DEV-only.
    registry.add(CompositionDescriptor{
        .id = "ae_08_glow_pulse",
        .factory = [](const CompositionProps& p) { return test::make_ae_08_glow_pulse(p); }});
    registry.add(CompositionDescriptor{
        .id = "ae_10_scale_pop",
        .factory = [](const CompositionProps& p) { return test::make_ae_10_scale_pop(p); }});
    registry.add(CompositionDescriptor{
        .id = "ae_12_random_character_jitter",
        .factory = [](const CompositionProps& p) { return test::make_ae_12_random_character_jitter(p); }});
    registry.add(CompositionDescriptor{
        .id = "ae_14_multiline_landscape",
        .factory = [](const CompositionProps& p) { return test::make_ae_14_multiline_landscape(p); }});
    registry.add(CompositionDescriptor{
        .id = "motion_blur_text",
        .factory = [](const CompositionProps& p) { return test::make_motion_blur_text(p); }});

    // (5) BUG 2 / TICKET-TEXT-GLOW-DARKENING — A/B test sibling
    // (glow_intensity=0.0).  Additive to production AnimTypewriterGlow
    // (which is in content/ and unchanged).  DEV-only per user spec.
    test::glow_ab::register_glow_ab_compositions(registry);

    // (6) Step 8 §A: REMOVED `chronon-glow-final-portrait` from DEV.
    // The portrait variant is now `ChrononGlowFinalAEPortrait` in PRODUCTION
    // (registered via register_runtime_compositions.cpp).  The DEV-only
    // alias was removed per Cat-3 anti-dup (single source of truth: the
    // canonical name lives in the production registry).

    // (7) ChrononGlowFinalAE_NoGlow — A/B acceptance sibling
    // (TICKET-GLOW-CERTIFICATION Azione 1).  Identical to ChrononGlowFinalAE
    // except glow_enabled=false.  Used by tools/check_glow_ab.py + the 6
    // acceptance TEST_CASEs in tests/visual/glow_ab/glow_ab_acceptance.cpp.
    // Production factory (header-only); registration is DEV-only per user spec.
    registry.add(CompositionDescriptor{
        .id = "ChrononGlowFinalAE_NoGlow",
        .factory = [](const CompositionProps&) -> Composition {
            ChrononGlowProps p = chronon3d::content::glow_final::default_landscape_props();
            p.glow_enabled = false;
            return chronon3d::content::glow_final::make_chronon_glow_final(p);
        }});

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    // (8) AECameraTextParity — 360-frame multi-segment stress test
    // (DIAGNOSTICS gated).  Calls chronon3d::content::anims::ae_camera_text_parity()
    // which is defined in content/experimental/ae-parity/ae_camera_text_parity.cpp
    // (compiled into chronon3d_content under CHRONON3D_BUILD_DIAGNOSTICS).
    registry.add(CompositionDescriptor{
        .id = "AECameraTextParity",
        .factory = [](const CompositionProps&) {
            return chronon3d::content::anims::ae_camera_text_parity();
        }});
#endif
}

} // namespace chronon3d

#endif // CHRONON3D_BUILD_CLI_DEV
