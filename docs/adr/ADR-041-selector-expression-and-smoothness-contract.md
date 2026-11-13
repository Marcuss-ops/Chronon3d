# ADR-041 — Closed selector expression profile and Smoothness contract

| Field | Value |
|---|---|
| **Status** | Proposed — implementation complete 2026-11-12; CI promotion pending |
| **Date** | 2026-11-12 |
| **Deciders** | Chronon3D text/runtime |
| **Tags** | `text`, `selector`, `expression`, `smoothness`, `3d` |
| **Related** | [`TICKET-AE-SELECTOR-PARITY-20261112`](../tickets/TICKET-AE-SELECTOR-PARITY-20261112.md), [`ADR-040`](ADR-040-text-parity-product-exception.md) |

## Context

The AE selector-parity closure had three open-ended statements rather than
enforced contracts:

1. Selector expressions used the full `ExpressionParser` surface, but host
   objects such as `thisComp`, `thisLayer`, `layer()`, `loopOut` and
   `valueAtTime` returned placeholder values instead of failing. That made the
   supported expression subset ambiguous and could silently change an authored
   animation.
2. Smoothness used a smoothstep edge feather, but the ticket only said it was
   "not byte-identical" to AE's proprietary falloff. The repository needed a
   precise internal contract and property coverage.
3. The ticket still listed "MTSDF limits" for 3D glyph projection even though
   P2-01 had already replaced the tan/shear clamp with the exact orthographic
   projection for per-glyph X/Y rotations.

## Decision

### 1. Selector expressions are a closed, validated profile

`math::ExpressionProfile::Selector` accepts exactly:

- variables: `textIndex`, `textTotal`, `selectorValue`, `time`, `frame`, `fps`,
  plus the constants `PI`, `E`, `true`, `false`;
- host-free built-ins already implemented by `math::ExpressionParser`
  (arithmetic, trig, `abs`, `sqrt`, `exp`, `log`, `pow`, `min`, `max`, `clamp`,
  degrees/radians helpers, `random`, `seedRandom`, `wiggle`, `linear`, `ease`,
  `easeIn`, `easeOut`).

Host-dependent forms — `thisComp`, `thisLayer`, `thisProperty`, `layer()`,
`loopIn`, `loopOut`, `valueAtTime`, `toComp`, `fromComp`, `posterizeTime`,
`posterize` — are parse errors in this profile. Property and bracket access is
also rejected.

`chronon3d::validate_selector_expression()` is the single profile validator.
The V3 decoder and `compile_selector()` both call it, so an unsupported
expression fails at decode/compile instead of silently evaluating to `0`. The
runtime evaluator uses the same profile, so validation and execution cannot
drift.

This is a selector-expression contract, not a claim that Chronon implements the
entire AE host expression runtime. Layer references and keyframe-time sampling
remain outside selector scope and are rejected explicitly.

### 2. Smoothness is the canonical smoothstep edge contract

`smoothness` remains a percentage in `[0, 100]`. For an active window
`[start, end]` of width `W`:

```text
feather = W * smoothness / 100
mask(p) = min(smoothstep((p - start) / feather),
              smoothstep((end - p) / feather))
weight(p) = base_shape(p) * mask(p)
```

`smoothness = 0` preserves the hard window exactly. The mask is bounded,
symmetric around the window centre, and monotone from each edge toward the
centre. Property tests pin those invariants. The repository does not claim
byte- or pixel-identity with AE's proprietary falloff; the production pixel
corpus remains governed by ADR-040.

### 3. Per-glyph X/Y 3D uses the exact orthographic projection

P2-01 is the authoritative implementation: at or beyond the 89-degree
threshold, `gpu_uses_exact_3d_rotation()` selects the exact orthographic matrix
instead of the former `tan()` shear clamp. Edge-on 90 degrees is accepted and
collapses to zero coverage; 120 degrees is covered by the pivot and placement
tests. Non-finite rotations still fail closed. There is no remaining
"beyond-MTSDF rotation limit" residual for X/Y; only the ADR-040 production
pixel diff remains outside this contract.

## Consequences

### Positive

- Unsupported AE host expressions fail with a deterministic decode/compile
  error instead of silently producing a different selector weight.
- Smoothness has one tested mathematical contract.
- The 3D residual list no longer contradicts P2-01.

### Negative / accepted boundaries

- Expressions using `thisComp`, `thisLayer`, `layer()` or keyframe helpers must
  be rewritten using the six selector variables or evaluated outside the
  selector.
- Smoothness is not claimed to match AE's proprietary pixel falloff.
- ADR-040's GPU pixel corpus remains 1/8 and is not changed by this ADR.

## Alternatives considered

- **Keep the full parser surface and return 0 for host objects.** Rejected:
  that is silent semantic drift, not a closed profile.
- **Implement the full AE host expression runtime.** Rejected for selector
  scope: it requires layer graphs and keyframe samplers that the per-glyph
  selector context does not own.
- **Copy AE's Smoothness falloff without reference frames.** Rejected: an
  unverifiable claim would be worse than an explicit internal contract.
- **Reintroduce tan/shear for 3D.** Rejected: P2-01's exact projection already
  covers edge-on and beyond-90 cases.

## References

- [`TICKET-AE-SELECTOR-PARITY-20261112`](../tickets/TICKET-AE-SELECTOR-PARITY-20261112.md)
- [`ADR-040`](ADR-040-text-parity-product-exception.md)
- `include/chronon3d/math/expression.hpp`
- `include/chronon3d/text/selector_expression.hpp`
- `src/text/glyph_selector_compile.cpp`
- `src/render_plan/render_plan_decoder_text.cpp`
- `tests/text/test_selector_evaluate.cpp`
- `tests/c_abi/test_render_plan_decoder_features.cpp`
