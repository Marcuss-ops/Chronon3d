#pragma once
// ============================================================================
// apps/chronon3d_cli/register_compositions.hpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — composition registration surface, split
// into 3 layers per the user-spec verbatim §3 (isola runtime/dev):
//
//   1. register_runtime_compositions(CompositionRegistry&)
//      Production runtime surface.  Always called.  Registers:
//        - register_builtin_compositions (DarkGridBackground, CameraImageClip)
//      Does NOT include any test/golden/dev header.
//
//   2. register_content_compositions(CompositionRegistry&)
//      Content-module bridge.  Always called.  Calls
//      chronon3d::register_content_modules() to register all content-pack
//      compositions (cinematic showcases, examples, certifications, etc.).
//      Uses a stub ExtensionContext (the per-domain register functions
//      only consume ctx.compositions, not ctx.assets).
//
//   3. register_dev_compositions(CompositionRegistry&)
//      DEV/test compositions.  ONLY called when CHRONON3D_BUILD_CLI_DEV=ON.
//      Registers: PipelineParityCanary, AnimTypewriterGlowNoGlow,
//      CameraTruthTest, CameraTruthOrbit, AE_CAM_01..10; retired cinematic-
//      glow families are not registered.
//      ae_08_glow_pulse / ae_10_scale_pop / ae_12 / ae_14 / motion_blur_text,
//      AECameraTextParity (DIAGNOSTICS gated).
//
// Cat-3 minimal-surface: 3 free functions, no new SDK surface.
// ============================================================================

namespace chronon3d {

class CompositionRegistry;

/// Register production runtime compositions (always called).
/// Registers builtin compositions (DarkGridBackground, CameraImageClip).
void register_runtime_compositions(CompositionRegistry& registry);

/// Register content-module compositions (always called when content
/// or diagnostics is built).  Calls chronon3d::register_content_modules().
void register_content_compositions(CompositionRegistry& registry);

/// Register the canary pack when CHRONON3D_CLI_WITH_CANARY_REGISTRY is enabled
/// for the CLI executable.  The pack is one subfile per family under
/// apps/chronon3d_cli/canary/ (motion3d, text contract, Final3D catalog, font
/// baselines, Apple V3 matrix, final cards); register_canary_compositions.cpp
/// is the orchestrator and each
/// subfile's header comment says why its compositions exist and what they are
/// evidence of.
/// Registers: Motion3DGlowOrbit, Motion3DSentenceOrbit,
/// Motion3DTextParentCamera, Motion3DSameMotionText, Motion3DSameMotionImage
/// (the compositions the Motion3D canary harness renders raw, hashes and
/// re-renders out of order), the chronon_text_* element canaries, the nine
/// Final3D production IDs, and the black_glow_* font/glow fleet.
void register_canary_compositions(CompositionRegistry& registry);

#ifdef CHRONON3D_BUILD_CLI_DEV
/// Register DEV/test compositions (only when CHRONON3D_BUILD_CLI_DEV=ON).
/// Per user spec: the DEV CLI is the only consumer of tests/visual/ factory
/// functions (PipelineParityCanary, AE_CAM_*, etc.).
void register_dev_compositions(CompositionRegistry& registry);
#endif

} // namespace chronon3d
