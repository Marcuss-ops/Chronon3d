# CPU caller census

Snapshot: Chronon3d `main` after Camera V1 removal; see the paired submodule
pointer in the workspace root for the exact revision.

## Camera

ChrononMotion3D owns cinematic camera authoring and evaluation. Chronon3d keeps
`Camera2_5D` as renderer state plus projection, depth, optical pixel effects,
motion-blur accumulation, and compositing. Camera V1 descriptors, programs,
sessions, trajectories, shot timelines, and camera-specific preset sources are
absent from the production source/CMake tree. The two obsolete top-level
RenderPlan compiler copies were also removed after confirming they were not
referenced by CMake or source callers; the active split compiler is under
`src/render_plan/`.

The relevant production boundaries are:

- camera-state conversion: `render_plan_compiler_animation_utils.cpp` builds
  renderer camera state from the lowered RenderPlan;
- temporal evaluation: `render_plan_compiler_animation_utils.cpp` consumes
  complete CM3D-resolved `camera_samples`; it linearly interpolates full poses
  between offline frame samples for fractional evaluation. Runtime providers
  supply exact CM3D samples at requested shutter subframes;
- pixel rendering: `runtime/render_pipeline.cpp`, DOF/motion-blur passes, and
  render-graph projection helpers.

Therefore no projection or pixel-effect file is in the deletion set. The
remaining removal gate is the RenderPlan camera-track interpolation: replace
it with camera samples evaluated by CM3D at the renderer's actual shutter
times, then verify pixel parity before retiring the track evaluator.

## RenderPlan

The production decode graph is:

```text
render_plan::decode_render_plan()
  -> render_plan_decoder.cpp
     -> render_plan_decoder_support.cpp
     -> render_plan_decoder_layer.cpp
```

The production callers are `render_plan_preparation.cpp`, the C ABI boundary,
and the SDK render-plan detail adapter. `render_plan_validator.cpp` is the
validation authority and is not a second decoder. The command layer validates
and prepares; it does not decode an alternate plan representation.

## Text headers

`text_unicode_utils.hpp` is now only a compatibility facade over the focused
`text_unicode_utf8.hpp` and `text_unicode_grapheme.hpp` headers. The direct
consumer census found no production include of the compatibility facade.

`text_render_resources.hpp` remains the intentional aggregator/facade. The
value/forward-declaration contracts now live in
`text_resource_contracts.hpp`, while font, scratch, UTF-8 and grapheme
surfaces are focused. The gate records 22 direct aggregator consumers after
removing nine unused registry includes, and ratchets that count at 22. The
glyph-atlas/raster-cache ownership remains one `TextRenderResources` authority.

## Reproduction commands

```text
rg -l '#include .*text_render_resources\\.hpp' include src apps tests
rg -l '#include .*text_unicode_utils\\.hpp' include src apps tests
rg -l 'decode_render_plan|RenderPlanDecoder|render_plan_decoder' src apps include
```

The text header gate also reports
`text_render_resources_fanout_ratchet=22`.
