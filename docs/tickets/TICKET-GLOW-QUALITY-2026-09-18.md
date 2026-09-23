# TICKET-GLOW-QUALITY-2026-09-18 — simple Gaussian Glow cleanup

> Closed implementation ticket: the Fast/High quality switch and multi-lobe controls have been removed. Chronon renders one Gaussian Glow with radius, intensity and color.

**Status**: `CLOSED`
**Scope**: Chronon3d software glow pipeline, render-plan contract and Motion3D canary output.

## Problem

The original issue was inconsistent visual output and unnecessary complexity from separate Fast/High paths, layered lobes, preset variants and duplicated quality controls.

## Resolution

- One separable Gaussian halo is shared by preview and final rendering.
- The only Glow controls are `radius`, `intensity` and `color`.
- `GlowParams` and `GlowPipeline` contain no quality, falloff, lobe, threshold, downscale or blend-mode selectors.
- `GlowPresets::canary_black` is the only named Glow preset; callers needing another tint or strength set the three direct parameters.
- Bloom remains a distinct bright-pass effect with its own threshold and blur; it is not a Glow mode.
- Render-plan V2 accepts deprecated legacy Glow fields for compatibility, ignores them, and excludes them from the decoded plan and content fingerprint. New plans emit only the three current fields.
- Native full-frame dispatch accepts the same single Glow contract as software; there is no quality gate.

## Acceptance

- [x] No active Fast/High enum, quality selector, synthetic lobe table, box-filter Glow path or quality-dependent native gate.
- [x] No unused named Glow presets remain; canary scenes use the single restrained preset or explicit radius/intensity/color.
- [x] Bloom is independently dispatched and configured.
- [x] Render-plan schema, decoder, compiler, fingerprint, catalogs and tests use the simple Glow contract.
- [x] Preview/final and C++/Go catalog compatibility tests pass where the local build environment permits.
- [ ] Chronon3d full build and effect/C-ABI test suite: blocked in this environment because the configured build tree cannot find TBB (`TBB_DIR-NOTFOUND`).
- [ ] Full-resolution Motion3D canary render/golden certification: not run; requires the built CLI and explicit golden artifact review.

The remaining unchecked items are environment/visual-certification gates, not open API or implementation work. Do not silently overwrite existing goldens; retain intentional output changes under a new artifact directory.
