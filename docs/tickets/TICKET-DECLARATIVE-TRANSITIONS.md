# TICKET-DECLARATIVE-TRANSITIONS — Render-plan transition exposure

## Stato
COMPLETE — v3 layer transitions lower to the existing scene transition APIs; schema, lifetime, fingerprint, graph hash and native MP4 smoke gates pass.

## Contratto
Schema v3 exposes optional `transition_in` and `transition_out` on each
`LayerPlan`. Each descriptor has `id`, `kind`, `duration_frames`, optional
`delay_frames`, `easing`, optional `direction`, and a closed `params` object.
The first implementation maps descriptors to the existing
`LayerTransitionSpec` and `LayerBuilder::transition_in/transition_out` API;
duration and delay are converted from frames to seconds with the canvas
timebase. `kind` uses catalog ids already registered by the single
`LayerTransitionCatalog`; unknown kinds fail decode/compile.

The accepted catalog vocabulary is `none`, `crossfade`, `slide`, `wipe_linear`,
`smooth_wipe`, `circle_iris`, `flash`, `procedural_remotion`, and `remotion`.
Existing typed parameter structs remain authoritative. Unsupported or
ill-typed parameters fail closed. Easing uses the existing enum mapping and
transition parameters continue to be hashed by `hash_layer_transition_spec`.
No second animation engine or transition node is introduced.

## Scope
- Add v3 `LayerTransitionPlan` data and strict schema validation.
- Decode frame durations/delays, kind, direction, easing and typed parameters.
- Validate duration is at least one frame and fits inside the layer lifetime.
- Lower both transition slots to existing builder APIs in plan-to-scene.
- Include every field in render-plan fingerprint; retain graph transition hashing.
- Keep transition definitions/catalog dispatch in the existing catalog.

## Acceptance gates
- [x] Decoder fixtures cover every exposed catalog kind, valid parameters and invalid duration/kind/parameter cases.
- [x] Existing catalog and scene transition tests pass with the registered start/end behavior and parameter handling.
- [x] Native MP4 smoke renders a 12-frame `wipe_linear`; decoded RGBA hashes differ at all frames, including 0/6/11.
- [x] Changing transition data changes both the plan fingerprint and the existing `hash_layer_transition_spec` graph key.
- [x] `render --plan` lowers a declared transition into the scene; its real MP4 output varies across the transition.
- [x] Targeted C ABI, scene and CLI test suites pass.

## Demolition Debt
Reuse the existing `LayerTransitionSpec`, catalog, transition node and layer
builder hooks. Do not add a second transition executor or animation system.
