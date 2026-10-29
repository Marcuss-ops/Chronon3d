# RenderPlan feature acceptance and blockers

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **Rerun focused gate sul tree corrente** — il batch locale del 2026-10-29 ha passato core/RenderPlan, CLI, effects, render graph e suite mirate; `chronon3d_c` shared-library link e `chronon3d_c_abi_tests` erano PASS (302 casi, 8,092 assertion, 278.26 s). Dopo quel batch sono stati aggiunti al sidecar per-job i contatori NV12 delta effettivi e la copertura serializer; ricostruire/rieseguire almeno il target CLI/test sidecar nel batch finale. Catalogo effetti: 57 righe, 51 native, 6 fail-closed. Evidenza precedente in `docs/CURRENT_STATUS.md` e nel batch locale del 2026-10-29 in `../TODO-RIPRESA-100X-E-TICKET-APER-TI.md`; non è un PASS del tree corrente.
- [ ] **Gate di accettazione più ampio** — logica: resta in `archive/TICKET-VULKAN-EFFECT-PARITY.md`, inclusa la certificazione same-SHA GPU-host. Dove: workflow CI GPU-host. Gate: run same-SHA green.
- [ ] **Criteri bloccati (non qui)** — logica: dynamic text/counters richiede una API coordinata RenderPlan V3 (dynamic text provider, cache identity, shaping per-frame); lens flare richiede un nuovo effetto + oracle software; full Vulkan parity resta nel ticket di parità. Mai patch decoder-only. Dove: `LayerPlan::text` in `include/chronon3d/render_plan/`, `include/chronon3d/effects/effect_catalog.def`. Gate: ticket dedicati.

## Implemented / verifiable acceptance criteria

- V3 `light_rays` effect contract carries normalized origin `[0,1]²`, length `[0,100000]`, density `[0,8]`, RGBA `[0,1]`, and decay `[0,1]` through schema, decoder, lowering and content fingerprint. V2 schema remains unchanged.
- Adjustment layers are effect-stack-only in V3: non-empty effects and only `id`, `type`, `effects`; direct `RenderPlan` compilation enforces the same rule, including non-default default-constructed fields.
- Fill color and gradient temporal tracks use bounded easing output and Hold step semantics; current unit/integration coverage includes base-to-first-key behavior.
- Built-in transition IDs/typed params remain in the canonical transition catalog; RenderPlan decoder/compiler paths already exercise the supported catalog.
- Existing Motion3D offline PCM analysis/driver baking can feed its existing MotionScene tracks and the explicit offline `lowerToRenderPlan()` bridge, which emits ordinary RenderPlan transform animation. The adapter regression is extended to cover this path.

## Blocked acceptance criteria (architectural scope)

### Dynamic text / counters

RenderPlan `LayerPlan::text` is a static string. `compile_render_plan()` resolves it, prepares/shapes it before frame evaluation, and captures immutable `PreparedText` in the scene callback. There is no dynamic text provider/expression/time-varying content contract, cache identity, or frame-local shaping/update protocol. Adding a counter safely requires a coordinated RenderPlan V3 API, compiler/render callback, shaping/layout and content fingerprint/cache behavior; it is not a decoder-only patch.

### Optical lens flare

The only nearby implemented effect is `LightRaysParams` (radial ray sampling from source luminance). It has no optical ghost elements, aperture/blade model, chromatic artifacts, or flare-specific compositing contract. Treating LightRays as lens flare would misrepresent capability; a true flare needs a new effect definition, software oracle and backend support.

### Full Vulkan effect parity

`VulkanBackend::apply_effect_stack()` remains explicitly unsupported as a direct backend API. `EffectStackNode` executes strict native stacks in authored order for the currently declared native routes, through graph-owned intermediate surfaces; local software/Vulkan pixel tests cover pointwise stages, Blur → Invert, and two-frame Echo stacks in both Echo → color and color → Echo order on llvmpipe. Strict `AdjustmentNode` stacks with fixed-extent native effects delegate to the same executor. AdjustmentNode Echo carries frame-addressed history, frame-variant caching and preroll metadata; two-frame Brightness → Echo matches the software oracle. Spatially expanding adjustment stacks still fail closed. Unsupported effects still fail closed for strict GPU input, and this does not establish full parameter parity or catalog-wide Vulkan support. The broader acceptance gate remains in [TICKET-VULKAN-EFFECT-PARITY](archive/TICKET-VULKAN-EFFECT-PARITY.md), including its same-SHA GPU-host certification requirement.

## Verification status

Prior local focused verification **PASS** (2026-10-29, dirty worktree based on
`a214fad27efdaa597ccf4e4a8ae34baa6677146e`): core/RenderPlan, CLI, effects,
render graph, focused backend suites and C ABI are recorded in the current
status and root execution checklist. A subsequent CLI sidecar metrics change
needs a fresh build and focused rerun before claiming current-tree PASS.
Same-SHA GPU CI/full parity remain unverified. Dynamic text/counters and a true
optical lens flare remain separate architectural work; no decoder-only
implementation was added.
