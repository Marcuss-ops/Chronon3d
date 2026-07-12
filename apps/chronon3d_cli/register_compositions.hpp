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
//        - ChrononGlowFinalAE (the canonical user-spec cinematic-glow)
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
//      CameraTruthTest, CameraTruthOrbit, AE_CAM_01..10,
//      chronon-glow-final-portrait, ChrononGlowFinalAE_NoGlow,
//      ae_08_glow_pulse / ae_10_scale_pop / ae_12 / ae_14 / motion_blur_text,
//      AECameraTextParity (DIAGNOSTICS gated).
//
// Cat-3 minimal-surface: 3 free functions, no new SDK surface.
// ============================================================================

namespace chronon3d {

class CompositionRegistry;

/// Register production runtime compositions (always called).
/// Registers: builtin compositions (DarkGridBackground, CameraImageClip) +
/// ChrononGlowFinalAE (the canonical user-spec cinematic-glow landscape).
void register_runtime_compositions(CompositionRegistry& registry);

/// Register content-module compositions (always called when content
/// or diagnostics is built).  Calls chronon3d::register_content_modules().
void register_content_compositions(CompositionRegistry& registry);

#ifdef CHRONON3D_BUILD_CLI_DEV
/// Register DEV/test compositions (only when CHRONON3D_BUILD_CLI_DEV=ON).
/// Per user spec: the DEV CLI is the only consumer of tests/visual/ factory
/// functions (PipelineParityCanary, AE_CAM_*, etc.).
void register_dev_compositions(CompositionRegistry& registry);
#endif

} // namespace chronon3d
