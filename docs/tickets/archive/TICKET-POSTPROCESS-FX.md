# TICKET-POSTPROCESS-FX — Post-process effects family (V1)

## Stato
RESOLVED (2026-10-20) — le quattro famiglie (`stylize.scanlines`,
`stylize.retro_tv`, `light.halation`, `light.anamorphic_streak`) sono nel
catalogo effetti e coperte da `tests/effects/test_postprocess_fx.cpp` (mapping
catalogo, range dei parametri, identità/determinismo, fail-closed GPU).
## Goal

Implement the modern motion-design post-process effects requested in the
"Uber-PostProcess Pass" spec as first-class effects of the canonical Chronon3d
engine effect system: `stylize.scanlines`, `stylize.retro_tv`,
`light.halation`, `light.anamorphic_streak`.

The pasted spec's nine-effect inventory resolves against the repository as
follows:

| Spec effect (pasted text) | Repository state | Action |
|---|---|---|
| Subtle TV scanlines | missing | **new effect 49** `stylize.scanlines` |
| Blur + grid (glassmorphism tech backdrop) | already canonical as `backdrop_filter` (layer blur/brightness/saturation/refraction + rim + chromatic dispersion) and the `grid_background` procedural shape | documented recipe, no new effect |
| Chromatic aberration (radial RGB split) | **exists** — `stylize.chromatic_aberration` with `mode: radial`, quadratic `center`-based offsets | none |
| 80s / Retro TV (tape jitter + YUV bleed) | missing | **new effect 50** `stylize.retro_tv` |
| Film halation | missing | **new effect 51** `light.halation` |
| Lens distortion + edge softening | **exists** — `distort.lens_distortion` (barrel/pincushion + scale); edge softening is an authored recipe (distortion → gaussian_blur masked edges) | documented recipe |
| Anamorphic streak / directional bloom | missing | **new effect 52** `light.anamorphic_streak` |
| Temporal posterization (stepped FPS) | **exists** — per-effect `time.posterize_fps` driver + `posterizeTime()` expression builtin | none |
| Non-linear film grain | **exists** — `generate.noise` with `shadow_response` / `midtone_response` / `highlight_response` bell weighting | none |

## Uber-PostProcess pass mapping

The spec's Pass 0-3 fusion schema maps to existing engine subsystems; no new
pass manager is introduced (no-duplication invariant):

- **Pass 1-2 (highlight extraction + separable blur)** — `light.bloom`,
  `light.glow`, `light.luminance_response` and the `self.blur` paths already
  implement bright-pass + ping-pong separable blur on the software and native
  backends. `halation` / `anamorphic_streak` each own their internal
  highlight-mask + box-blur scratch passes for the CPU route.
- **Pass 3 (uber single-sample combine)** — the engine's fusibility contract
  (`fusible: true` into the ColorPipeline) is the canonical single-read/write
  fusion path for pointwise color effects. The FX in this family are either
  spatial resamples (RetroTv) or multi-pass scratch composites
  (Halation, AnamorphicStreak), so they are registered `fusible: false`,
  matching the policy of every other spatial effect in the catalog.
- Scanlines is pure per-pixel color math (`EffectDomain::Color`) and therefore
  re-runs correctly inside any fused color pipeline once flagged; it stays
  `fusible: false` in V1 to avoid changing fusion-class semantics for existing
  programs (fingerprints stay stable).

## Shader-math contracts (CPU software route)

- **Scanlines** (`apply_scanlines`): triangle luminance dip
  `factor = 1 - amount * tri((line + phase * pitch) / pitch)` on the repeated
  axis (`axis 0` = horizontal lines repeating along Y); alpha untouched;
  `amount` default `0.08` keeps the modern 3-8% modulation band.
- **Retro TV** (`apply_retro_tv`): deterministic tape jitter — band hash
  `hash(time_quantum, row, seed)` drives a clamped horizontal row offset
  (occasional 2.5x "skip" bands when the second hash exceeds 0.82); luma keeps
  full spatial resolution, chroma is horizontally box-averaged (Rec-601
  YCbCr roundtrip) and blended by `bleed_strength`; seeded per-pixel luma
  interference noise. Temporal: `true` (pure function of `time_seconds`).
- **Film halation** (`apply_halation`): smoothstep luma knee
  (`threshold`, `softness`) → mask; separable box blur `radius_px`; screen
  composite of `tint * intensity` (default warm `1, 0.25, 0.05`).
- **Anamorphic streak** (`apply_anamorphic_streak`): same smooth highlight
  mask; horizontal-only box blur (`streak_length_px`, 1 px vertical); additive
  composite of `tint * intensity` (default cool `0.55, 0.75, 1`).

All four kernels are deterministic; identical inputs reproduce identical
frames (splitmix64-derived hashes, quantized tape time).

## Touchpoints (canonical registry, no parallel system)

- Catalog: `effect_catalog.def` (types 49-52), `effect_ids.hpp`,
  `effect_params.hpp` (+ validated range structs), `effect_param_anims.def`.
- Graph hashing: `render_graph_hashing.hpp` cases for all four types.
- Plan: `EffectKindPlan` + per-kind `EffectPlan` fields, decoder
  (`render_plan_decoder_effects.cpp`), preparation
  (`render_plan_effect_preparation.cpp`), fingerprint
  (`fingerprint_layer.cpp` twice, root + render_plan), kind coverage array
  (`effect_kind_coverage.hpp`).
- Software backend: `effects/postprocess/postprocess_fx.{hpp,cpp}` kernels,
  processors + factories in `software_effect_processors_motion.cpp`,
  registration in `builtin_processors.cpp`, CMake `add_subdirectory(postprocess)`.
- Wire contract: render-plan v3 schema — both canonical-effect id enums and
  one `additionalProperties: false` def per effect kind with bounded params.

## Scope boundary (disclosed, not hidden)

V1 ships **CPU software routes only**. The strict native-Vulkan capability
authority (`effect_gpu_capability.hpp`) reports `Unsupported` for the four new
types, which fails closed under the strict native-GPU policy instead of
silently falling back; CPU execution remains the certified route (same state
the catalog already had for `stylize.roughen_edges` and other post-44 entries
before their own native batches). Compute-kernel parity for
`generate_bulk.comp` / warp bulk is **follow-up work**, not in this ticket's
acceptance; the effect-path matrix tool output reflects this honestly.

Bounds policy is `Preserve` for all four: halation/streak bleed is sampled
from the full layer framebuffer, so nothing expands the layer bbox (halo
rendering is clipped at layer bounds). This avoids touching the
margin-plumbing paths that `Glow`/`Bloom` expansion regimes own.

## Tests

`tests/effects/test_postprocess_fx.cpp` (registered in
`tests/renderer_tests.cmake` → `chronon3d_effects_tests`):

- catalog ids + Type/Params bidirectional mapping + stable numeric values,
- parameter range validation (all four structs),
- scanlines modulation identity/amount=0/alpha preservation,
- retro TV: zero-strength identity, determinism, row jitter, chroma smear NaN
  guard,
- halation: warm bleed near highlights, far-field invariance, sub-threshold
  no-op,
- streak: horizontal elongation, vertical invariance, intensity clamp,
- GPU capability rows fail closed with a diagnostic,
- animatable-parameter whitelist membership.

**Stato finale:** ARCHIVIATO (2026-10-23) — RESOLVED (2026-10-20), nessun lavoro aperto.
