# ADR-038 — Text quality, canonical shadow and stroke semantics

| Field | Value |
|---|---|
| **Status** | Accepted — implemented 2026-11-09 |
| **Date** | 2026-11-09 |
| **Deciders** | Chronon3D text/runtime |
| **Tags** | `text`, `mtsdf`, `shadow`, `stroke`, `quality`, `fail-closed` |
| **Related** | `TICKET-TEXT-CLOSURE-20261109`, `TICKET-TEXT-QUALITY-V2`, `ADR-014`, `ADR-037` |

## Context

The text stack has three independent quality surfaces that were previously
implicit:

1. **Glyph generation** — the legacy 64-em MTSDF profile and the absence of a
   declared higher-quality policy.
2. **Run-level shadow** — the software rasterizer already builds a union mask,
   but blurs it with a tiered box filter; the native MTSDF shader composes
   per-glyph shadows with an alpha-max.
3. **Stroke** — the software rasterizer strokes the real outline with
   miter/bevel/round joins, while the native shader dilates the distance field.
   Those are different semantics, not two implementations of the same thing.

Treating any of these as a backend hint would let a plan ask for a mode the
native path cannot represent and silently receive the legacy result. This ADR
fixes the contract: the mode is part of the text identity, the CPU implements
what it can, and the GPU rejects what it cannot represent.

## Decision

### 1. Glyph quality is explicit

`TextQuality` is part of `TextPaint`, the prepared-text cache key, the text-run
hash and the layer-style fingerprint.

| Mode | Profile | Generation id |
|---|---|---|
| `Fast` (default) | canonical 64-em MTSDF, range 16, padding 24 | 1 |
| `High` | 128-em MTSDF, range 40, padding 60 | 4 |

`High` is opt-in. Production remains `Fast`. The pixel-parity corpus uses
`High` to separate generator quality from placement/antialias differences.

`msdfgen` is **not** added. A 256-em experiment did not improve glyph-edge
parity. The 128-em `High` profile scales distance range and padding with the em
grid so screen-space effect reach matches `Fast`. This corrects the earlier
profile's undersized shadow field but does not claim CPU/GPU pixel identity;
the full corpus gate remains open.

### 2. Shadow quality has two named modes

`TextShadowQuality` is part of the text identity.

| Mode | Semantics | Backend |
|---|---|---|
| `Fast` (default) | tiered box blur over the union mask; historical throughput path | CPU + GPU |
| `Canonical` | union mask → separable Gaussian (`sigma = radius / 3`) → color/opacity → source-over | CPU only |

The CPU implementation keeps the existing run-level union mask and changes only
the blur kernel and radius mapping. It does **not** create a second shadow
pipeline. Multiple shadows remain ordered passes over the same mask.

The native MTSDF route rejects `Canonical` when any shadow is enabled. It must
not fall back to the Fast shader: that would be a silent quality downgrade. A
run-level GPU pass (transient union surface + two compute blur passes) remains
unimplemented and is an explicit non-goal for this closure. The parity corpus
continues to use `Fast`.

### 3. Stroke semantics are explicit

`TextStrokeMode` is part of the text identity.

| Mode | Semantics | Software | Vulkan/MTSDF |
|---|---|---|---|
| `Auto` (default) | historical backend choice | vector outline | distance dilation |
| `Distance` | GPU SDF dilation; round joins, butt caps, miter limit not meaningful | rejected | supported |
| `Vector` | expanded outline with miter/bevel/round and miter limit | supported | rejected |

`Auto` is a compatibility mode, not a parity mode. A plan that needs parity must
choose `Distance` or `Vector` explicitly; today only the software side supports
`Vector`, so a CPU/GPU parity claim for `Vector` is forbidden. `Distance` on the
software path fails closed instead of silently using a vector stroke.

No `GlyphPaint` or `TextRunDynamic` field is added. `VectorStroke` remains on the
software outline path; the native coverage atlas described by the ticket is not
implemented in this closure.

### 4. Cache and fingerprint identity

The following are folded into `TextRunShape` hashes and the layer-style
fingerprint:

- `paint.quality`
- `paint.shadow_quality`
- `paint.stroke_mode`

The prepared GPU text cache key already includes the quality byte. Changing any
mode must invalidate prepared glyphs, glyph-atlas residency and the node cache.
`MtsdfProfile::generation_id` is `3` for `High`; `Fast` stays generation `1` so
existing atlases remain valid.

### 5. Validation

- Schema: `style.quality` (`fast|high`), `style.shadow_quality`
  (`fast|canonical`), `style.stroke_mode` (`auto|distance|vector`).
- Decoder: rejects unknown values and rejects all three fields on non-text
  layers.
- V2 projection: strips the V3-only fields before the frozen V2 validation.
- GPU lowering: `Canonical` + enabled shadow and `Vector` + enabled stroke are
  fail-closed. The render returns `UnsupportedCapability`, never a fallback.
- Software lowering: `Distance` + enabled stroke is `UnsupportedCapability`.

## Consequences

### Positive

- A plan can no longer request a quality or stroke mode and silently receive a
  different one.
- The CPU canonical shadow is a real, testable implementation with one kernel
  authority; `Fast` remains the default.
- The parity corpus can distinguish generator quality from backend differences.

### Negative / migration cost

- `Canonical` shadows are CPU-only; a production plan that requires both
  canonical shadow and native GPU rendering is not supported.
- `Vector` strokes are CPU-only for the same reason.
- Canonical Gaussian blur is O(radius) per pixel and allocates float scratch; it
  is intentionally opt-in and must be benchmarked before production use.

### Neutral

- No new dependency, registry, resolver or cache.
- No ABI change to `TextRunDynamic` (still 160 bytes).
- Production defaults are unchanged.

## Demolition Debt

| Field | Value |
|---|---|
| **Owner** | Chronon3D text/runtime |
| **Reason** | Native run-level canonical shadow and native vector-stroke coverage atlas are not implemented. |
| **Exit condition** | GPU `Canonical` produces the same union-mask Gaussian as CPU within the T0-01 thresholds, and GPU `Vector` produces outline joins within the same thresholds. |
| **Equivalence gate** | `verify_text_cpu_gpu_parity.py` on `effects`/`multi_shadow` with `shadow_quality=canonical` and `stroke_mode=vector`, plus a golden miter/bevel/round corpus. |
| **Removal scope** | Delete the fail-closed checks in `text_run_packing.cpp` / `gpu_text_run_animation.cpp` and the software `Distance` rejection; then remove `Auto` as a parity ambiguity. |
| **Status** | ACTIVE |

## References

- `Chronon3d/include/chronon3d/scene/model/shape/shape.hpp`
- `Chronon3d/src/backends/software/processors/text_run/text_run_processor/raster.cpp`
- `Chronon3d/src/backends/software/processors/text_run/text_run_processor/scratch.cpp`
- `Chronon3d/src/render_graph/nodes/text_run/text_run_packing.cpp`
- `Chronon3d/src/render_graph/nodes/text_run/gpu_text_run_animation.cpp`
- `Chronon3d/src/render_plan/render_plan_decoder_layer_style.cpp`
- `Chronon3d/schemas/json/chronon.render-plan.v3.extensions.schema.json`
- `Chronon3d/tools/verify_text_cpu_gpu_parity.py`
