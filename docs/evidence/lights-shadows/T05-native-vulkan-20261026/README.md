# T05 native Vulkan shadow pass — 2026-10-26

## Implementation

`ShadowNode` now executes natively when `RenderGraphPolicy::require_native_gpu`
is enabled. The pass promotes CPU-authoritative caster pixels only when needed,
then uses the backend's native transform, color transform, separable blur, and
surface compositing operations. GPU-only sources do not trigger a CPU fallback.
Temporary surfaces are released on both success and error paths.

The focused regression uses a half-alpha caster with a nonzero framebuffer
origin and checks the projected shadow's alpha/tint, GPU-authoritative output,
and exact repeatability.

## Verification

- `chronon3d_backend_registry_tests`: **passed**, 1/1 CTest target, 38.79 s.
- The new focused test contains 15 assertions.
- `chronon3d_cli` and the registry test target were already up to date in the
  configured Vulkan build before the CTest run.

## Production render status

The existing T05 1920×1080 RenderPlan was attempted through the strict native
CLI route. Its export preflight stopped because the FFmpeg capability probe in
the CLI build reports no NVENC encoder. The host's system FFmpeg can encode with
NVENC, but that does not enable NVENC in the CLI's linked FFmpeg. No new
production image was generated, so this evidence does not approve shadow
contrast or close the T05 visual quality gate.

T05 remains **PARTIAL** pending a visible strict-native render and visual A/B
review. The renderer feature test is green; the encoder/build integration and
shot approval are still outstanding.
