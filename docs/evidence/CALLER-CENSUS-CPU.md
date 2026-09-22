# CPU caller census

Snapshot: `main@63f2bd151` plus the working-tree changes for this task.

## Camera

The production graph has one camera state model (`Camera2_5D`) and one
evaluation authority (`camera_v1::CameraProgram` / `CameraSession`). The
references to `camera_2_5d.hpp` are projections, render consumers, CLI mapping,
or public compatibility types; the census did not find a second production
camera evaluator to remove.

The relevant production boundaries are:

- authoring and CLI mapping: `scene/builders/camera_api.cpp` and the CLI
  command/mapping layer;
- canonical evaluation: `scene/camera/camera_v1/camera_program_*.cpp` and
  `camera_session_cache.cpp`;
- render consumption: `runtime/render_pipeline.cpp`, software renderer, and
  render-graph projection helpers.

Therefore no camera file was deleted. The safe action is to keep this census
as the removal gate until a caller-level duplicate, rather than a header-level
duplicate, is demonstrated.

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
surfaces are focused. The gate records 33 direct aggregator consumers across
software, registry tests, runtime, and render-graph code. The glyph-atlas /
raster-cache ownership is deliberately still one `TextRenderResources`
authority, so E1 is not claimed complete merely because the headers compile.

## Reproduction commands

```text
rg -l '#include .*text_render_resources\\.hpp' include src apps tests
rg -l '#include .*text_unicode_utils\\.hpp' include src apps tests
rg -l 'decode_render_plan|RenderPlanDecoder|render_plan_decoder' src apps include
```
