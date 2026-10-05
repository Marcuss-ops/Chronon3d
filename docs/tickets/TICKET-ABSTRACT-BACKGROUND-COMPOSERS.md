# Abstract background composers — contract and gated acceptance

## Status

**Contract defined; implementation BLOCKED by the active feature freeze.** This
is a design/acceptance ticket only. It does not claim that the 43 reference
frames are present, that any frame has been recreated, or that software/Vulkan
parity exists. Do not start product implementation until Gate 0 is satisfied.

The supplied analysis describes 43 references (16 folded gradients, 8 neon
lights, 19 cinematic leaks), but its ZIP/CSV/contact sheet are not in this
checkout. Those counts are planning input only; individual palettes, crops,
parameters, and expected outputs cannot be certified from the pasted summary.

## Repository facts verified for this contract

- `include/chronon3d/render_plan/effect_presets.hpp` already provides
  `paper_fold`, `LightLeakProfile` / `LightLeakResolver`, deterministic
  `AnimatedLightLeakEmitter`, `cinematic_soft_blob`, `cinematic_edge_burn`,
  `cinematic_diagonal_streak`, `cinematic_bezier_ribbon`, and the
  `abstract_background` recipes.
- `FoldWarp` and `CreaseShading` are separate ordered effect records. Their
  current parameters describe one crease each; they do not define a multi-fold
  field, fold count/phase model, or a `LayeredFoldField` RenderPlan node.
- `ShapeContentPlan` already carries ordinary gradient, scalar `Field2D` plus
  `field_ramp`, `GradientMesh`, and path/stroke data. `StrokeStyle` supports
  cubic path commands, gradient strokes, brush profiles and taper. These are
  ordinary composition inputs, not a new background renderer.
- `LightLeakProfile` supports `Edge`, `Corner`, `Sweep`, `Radial`, and `Band`
  shapes, `Screen`/`Add`/`SoftLight` blend, position, width, softness, spread,
  chroma split, seeded grain and explicit flicker. It has no `LightCone` shape
  and does not accept a `Field2D` displacement/mask.
- V3 effect records include `DisplacementMap` fields, but that alone does not
  establish an authorable `Field2D -> LightLeak` connection or native Vulkan
  parity for such a composition.
- `ChrononTemplate/catalog/motion_catalog.v1.json` already publishes
  `abstract_backgrounds` (16 recipe ids) and `tech_backgrounds` (6 recipe ids);
  `catalog/tech_background_v1.json` describes six existing tech looks. These
  are not a verified catalog of the supplied 43 references. Do not create a
  second background registry.
- `docs/CURRENT_STATUS.md`, `docs/CHRONON_PLAN.md`, and
  `docs/ROADMAP.md` keep new feature work frozen until exact-main-SHA CI is
  green and the project explicitly lifts the freeze. The Vulkan effect ticket
  also requires unsupported strict-native work to fail closed.

## Architecture decisions

1. **No new background engine, effect type, RenderPlan wire node, or registry
   for the first pass.** A candidate helper is an authoring-time composer that
   lowers to ordinary V3 shape/layer/effect records and then follows the
   existing schema, decoder, compiler, graph, and backend paths.
2. **`LayeredFoldField`: composer candidate, not an effect primitive.** Its
   contract is palette + bounded repeated-fold recipe + deterministic seed,
   lowered as explicit gradient/field fill and ordered `FoldWarp` /
   `CreaseShading` pairs. No independent pixel loop, hidden time source, or
   second fold evaluator. It is not a V3 wire type. The helper must state the
   actual fold count/order and reject unsupported combinations rather than
   imply one existing `FoldWarp` represents many folds.
3. **`LightCone`: composer candidate, not a `LightLeakProfile` extension.**
   Start with bounded path/shape geometry, ordinary gradient fill, and existing
   light/bloom effects. Define origin, direction, spread, length, inner/outer
   colors, intensity, edge softness and radial falloff in canvas-normalized
   units (colors in the existing SDR authoring range). A static cone recipe
   must not claim volumetric scattering. If the required look needs a true
   field-driven volumetric kernel, split that into a later effect contract with
   its own CPU reference and strict-Vulkan parity gate.
4. **`GlowPath`: composer over existing path/stroke primitives.** Use ordered
   MoveTo/LineTo/QuadraticTo/CubicTo/Close commands, existing gradient/brush
   stroke, trim/taper/pressure controls where supported, and the ordinary
   ordered Bloom/Glow/ChromaticAberration effect stack. The fixed one-cubic
   `cinematic_bezier_ribbon` is a recipe, not evidence of a multi-segment,
   variable-width, along-path color API beyond the actual existing stroke
   contract. Do not add a second path evaluator.
5. **`WarpedLightLeak`: not approved as a first-pass composer.** Current
   `LightLeakResolver` cannot consume a `Field2D`, and rendering a separate
   field layer beside a leak is not equivalent to warping the leak mask. First
   prove a real need from a supplied reference and audit the graph's existing
   displacement inputs. If a shared field-driven mask/deformation is required,
   write a separate typed contract covering graph dependencies, units, bounded
   sampling, alpha/edge behavior, fingerprints, temporal determinism and
   backend capability before proposing any V3/API change. Until then use the
   current leak recipes or explicitly defer the look; do not label a layered
   approximation `WarpedLightLeak`.
6. **Keep ownership one-way.** ChrononTemplate owns recipe identity and
   composition parameters; ChrononMotion owns time-varying tracks when motion
   is needed; Chronon3D owns pixels and rendering. Recipe ids extend the
   existing authored/emitted catalog only after its schema and emitter contract
   are checked. No parallel registry or renderer is introduced.

## Proposed bounded helper contracts

These are the concrete first-pass bounds to review against the source frames;
they are authoring-composer contracts, not new RenderPlan schema fields. A
review may narrow a limit for fidelity or resource safety, but must not widen it
without recording why and adding boundary tests.

| Input | `LayeredFoldField` | `LightCone` | `GlowPath` |
|---|---|---|---|
| Palette / colors | 2–32 ordered SDR gradient stops; stop position `[0,1]`, nondecreasing; RGBA channels `[0,1]` | inner/outer SDR RGBA channels `[0,1]` | optional existing stroke gradient, 2–32 ordered SDR stops; positions `[0,1]`, nondecreasing |
| Geometry count | 1–16 folds | one cone outline; no generated ray count | 1–64 connected quadratic/cubic/line segments; at most 66 commands including initial MoveTo and optional Close |
| Fold controls | per fold axis `{horizontal,vertical}`, position `[0,1]`, amount `[-1,1]`, radius `[0.01,1]`, falloff `[0.1,4]`, perspective `[0,0.5]`; crease width `[1,256]` px, shadow/highlight `[0,1]`, softness `[0,1]` | origin coordinates `[-4,5]` in normalized canvas units; finite nonzero direction normalized by the composer; spread `(0,170]` degrees; length `(0,4]` times the larger canvas dimension | one MoveTo followed by connected drawable segments; finite normalized canvas coordinates `[-4,5]`; no implicit joins or extra subpaths |
| Width / edge | optional existing scalar field warp only, with finite frequency `(0,4096]` and domain-warp amount `[0,0.25]` | edge softness `[0,1]`; falloff exponent `[0.1,8]` | constant stroke width `[1,1024]` px; existing taper or brush pressure curve only, with at most 64 ordered points (position and pressure `[0,1]`); reject a width profile that cannot be represented by those controls |
| Energy / time | explicit `uint32` seed; no hidden clock; phase is static or supplied through existing tracks | intensity `[0,4]`; blend only existing Screen/Add/SoftLight; no claim of volume/scattering | core uses the ordinary path stroke; halo uses existing ordered Bloom/Glow records; optional chroma split `[0,16]` px; no hidden clock |
| Work budget | at most 16 ordered fold/crease pairs; no per-pixel or private fold loop | one bounded path/gradient shape and existing effects | at most 66 path commands and 64 width-profile points; no second path evaluator |

All helper inputs and canvas dimensions must be finite; canvas width/height
must satisfy the existing `LightLeakResolver` bounds `(0,1,000,000]`. Equal
inputs, including ids and seed, must lower to byte-equivalent authored plan
values. Reject duplicate generated layer ids. Every output-affecting input must
change the ordinary plan fingerprint when its lowered output changes. The
stated limits do not certify that every operation is accepted by strict Vulkan;
that is tested independently per exact effect/shape combination.

`WarpedLightLeak` has no first-pass parameter schema because it is deferred. It
must not be represented as “leak plus adjacent field layer”; a later proposal
must first specify a shared mask/deformation input and its graph, alpha, edge,
fingerprint, determinism, resource-budget, and backend contracts.

## Acceptance checklist

### Gate 0 — prerequisite (BLOCKED; owner: repository maintainers)

- [ ] Restore runner allocation and obtain green Chronon3D CI on the exact
  reviewed `main` SHA.
- [ ] Reconcile the current worktree with that SHA and explicitly lift the
  feature freeze under the canonical project process.
- [ ] Confirm the edited source tree is the reviewed baseline; preserve all
  unrelated existing work. Local unit tests alone do not satisfy this gate.

### Gate 1 — reference and recipe inventory (BLOCKED on missing inputs)

- [ ] Add the actual 43 reference PNGs, contact sheet, and per-reference CSV to
  an approved repository/local input path, or record their stable accessible
  location and hashes. Do not commit generated reference artifacts unless
  project policy approves them.
- [ ] Validate exactly 16 `wave_*`, 8 unique `neon_*`, and 19 `cinematic_*`
  source records; record stable ids, source-frame/crop provenance, dominant
  palette, composition, suggested existing primitives, and unsupported visual
  requirements. Montage quadrants must not be counted as individual full-frame
  outputs unless explicitly identified as unique references.
- [ ] Map each source record to existing recipe primitives and one of
  `composer`, `existing recipe`, or `blocked new primitive`. Record unsupported
  features without estimating percentage fidelity.
- [ ] Obtain a deterministic expected-output policy for each id: canvas, frame,
  color space, alpha/background, seed, and software/Vulkan execution lane.

### Gate 2 — helper contracts (no wire/API expansion by default)

- [ ] Review and adopt or amend the exact proposed bounds in
  `Proposed bounded helper contracts`; add boundary cases for every inclusive
  and exclusive endpoint and each malformed-value rejection.
- [ ] Specify how `LayeredFoldField` maps palette/field fills and each of its
  1–16 ordered folds to ordinary V3 plan members; verify output ids and
  fingerprints account for every recipe parameter. Do not introduce hidden
  animation: phase changes use ordinary existing tracks or remain static.
- [ ] Verify that `LightCone` lowers its bounded outline/gradient and existing
  effects with deterministic alpha/blend/order. Keep the initial path/gradient
  interpretation non-volumetric.
- [ ] Verify that `GlowPath` lowers its connected bounded path through existing
  stroke/brush/gradient and effect contracts; reject malformed topology,
  duplicate ids, unsupported stroke combinations, non-finite/out-of-bounds
  geometry and over-budget paths. Do not add a private path or width evaluator.
- [ ] Keep `WarpedLightLeak` deferred until a concrete reference cannot be
  represented by the existing profile/recipes. If it is required, open a
  separate design ticket and prove single-layer field-mask/deformation
  semantics before implementation; do not quietly expand this ticket's API.
- [ ] For each accepted composer, document an authored example and a lowered
  ordinary V3 plan, including exact supported and rejected cases. Any proposed
  schema, decoder, fingerprint, public-header, graph input, effect catalog, or
  backend change requires its own reviewed impact/compatibility analysis.

### Gate 3 — implementation after Gate 0 (not started)

- [ ] Implement only approved authoring helpers in the existing
  `render_plan/effect_presets.hpp` composition surface or the repository's
  established equivalent; reuse existing effect, shape, field, gradient,
  stroke, catalog, and resolver authorities.
- [ ] Add no new RenderPlan schema/effect id unless Gate 2 proves ordinary V3
  composition cannot express required semantics and the additive wire/API
  design is reviewed.
- [ ] Add focused unit contracts for valid boundary values, malformed inputs,
  deterministic lowering, stable layer/effect order, unique ids, and sensitivity
  to every output-affecting parameter.
- [ ] Run schema validation, RenderPlan decode/compile/prepared execution, and
  CLI validation on emitted V3 examples through the same interface users use.
- [ ] Render software reference frames for each accepted preset; pin pixel/
  golden checks for palette, fold/beam/path geometry, alpha, safe bounds,
  clipping, and deterministic repeated/random-access frames.
- [ ] For strict Vulkan use, add capability accounting and software/Vulkan pixel
  oracles for the exact operations used. Unsupported operations must fail
  closed; prove no CPU fallback/readback for a strict-native request. On a
  non-GPU host report these checks `NOT RUN`, never PASS.
- [ ] Add and validate every supplied reference as one stable recipe id: 16
  waves + 8 neon + 19 cinematic = 43, with duplicate detection and a stable
  generated catalog artifact. Re-run the canonical emitter and fail if the
  checked-in output drifts.
- [ ] Render through the project CLI / normal ChrononTemplate-to-Chronon3D
  path at each approved delivery canvas. Verify frame count, dimensions,
  color/alpha contract, decoded output, and evidence manifest; no preview-only
  result is a completion claim.
- [ ] Run focused effects, RenderPlan/C ABI, Template catalog and renderer
  suites plus the relevant full same-SHA gates. Record each result as `PASS`,
  `FAIL`, `PARTIAL`, or `NOT RUN`; do not infer Vulkan parity from software
  renders.

## Next actionable step

Unblock Gate 0 first. In parallel, make the original ZIP/CSV/contact sheet
available and complete Gate 1's inventory without changing product code. Once
both prerequisites are satisfied, review the first three composer contracts
against concrete references and decide whether the deferred `WarpedLightLeak`
needs its own typed design at all.

## Progress

- **Contract v1:** repository interfaces and the active feature-freeze gate
  were inspected; the primitive/composer boundary and blocked acceptance
  sequence above are documented. No product implementation or parity claim was
  made. The source ZIP/CSV/contact sheet are absent from this checkout.
- **Reference-inspired render set:** `ChrononTemplate/tools/render_background_recreation_v1.py`
  generated 43 distinct family recipes and rendered them through the Chronon3D
  software CLI as 5-second, 1920x1080, 30 fps H.264 clips (16 wave, 8 neon,
  19 cinematic). All 43 outputs passed per-file ffprobe and decoded-motion
  checks; the generated contract suite passed 7/7. Contact sheet, JSON manifest,
  and CSV index are in `ChrononTemplate/out/background_recreation_v2/`.
  These are original reconstructions from the written family descriptions;
  they are not pixel-verified against the missing source frames, do not satisfy
  Gate 1's source inventory, and do not implement or certify the three composer
  helpers or Vulkan parity. Gate 0 and Gate 1 remain blocked; Gate 3 remains
  unstarted under the active feature freeze.
