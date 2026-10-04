# RenderPlan feature acceptance and blockers

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

`VulkanBackend::apply_effect_stack()` remains explicitly unsupported as a direct backend API. `EffectStackNode` executes strict native stacks in authored order for the currently declared native routes, through graph-owned intermediate surfaces; local software/Vulkan pixel tests cover pointwise stages, Blur → Invert, and two-frame Echo stacks in both Echo → color and color → Echo order on llvmpipe. Strict `AdjustmentNode` stacks with fixed-extent native effects delegate to the same executor; spatially expanding and temporal adjustment stacks fail closed. Unsupported effects still fail closed for strict GPU input, and this does not establish full parameter parity or catalog-wide Vulkan support. The broader acceptance gate remains in [TICKET-VULKAN-EFFECT-PARITY](TICKET-VULKAN-EFFECT-PARITY.md), including its same-SHA GPU-host certification requirement.

## Verification status

Pending focused schema validation, C ABI/render-plan tests, effects tests, and targeted build. This ticket records scope and blockers, not a PASS result.
