# TICKET-SVG-GROUP-ISOLATION — Isolated group compositing for lowered SVG

## Stato
PARTIAL — software group stage certified 2026-10-17; Vulkan weighted-surface
stage and strict-native checks are implemented 2026-10-18. Hardware gate,
performance measurements, Q2 (nested animated groups) and Q3 (per-element
clip scopes) remain open.

## Obiettivo
Make a lowered SVG group honor the SVG isolated-group alpha contract: the
group's children render at full paint alpha onto an offscreen surface, and
the surface is composited once with the group's opacity. This removes the
documented double-darkening of overlapping children inside an opacity group
and unlocks nested animated groups (Q2) and per-element clip scopes (Q3) on
the same machinery.

Decisione architetturale: `docs/adr/ADR-035-svg-isolated-group-compositing.md`
(no new node kind; bounded group branch in `MultiSourceNode`; Vulkan uses the
existing native weighted compositor and fails closed if native execution is
unavailable).

## Contratto (V1 — software + Vulkan)

- The SVG loader annotates each element with the innermost enclosing group
  (`SvgStyleGroupRef`: id, opacity) instead of folding the group's opacity
  into child paint alpha. Nested isolated groups inside the isolated region
  stay fail-closed in V1 through the certified malformed-animation sentinel.
- The render-plan compiler propagates `ImportedSubpathPlan::svg_group`
  (id, opacity) without folding it into fill/stroke alpha; the element's own
  opacity stays folded (correct per-paint alpha). Validation: nonzero id,
  opacity in (0,1].
- `LayerBuilder::svg_group_isolation(id, opacity)` tags the node (predented
  `pin_to`-style last-node retarget); the source pass moves the spec into
  `MultiSourceItem::group` with the combined post-composite alpha
  (group_opacity * placement alpha).
- `MultiSourceNode` executes group items in a bounded group stage: children
  draw at full alpha onto a pooled intermediate framebuffer, cleared per
  group; the surface composites once with the group alpha onto the layer
  framebuffer `out = src*g + dst*(1 - src.a*g)` (premultiplied). Groups
  flush at vector-order boundaries; non-grouped items keep the historical
  per-item draw-time fold (text and legacy shapes untouched).
- Vulkan lane: when the backend owns native surfaces, the intermediate group
  framebuffer is initialized on GPU and composed once with
  `composite_surface_weighted()`. Under `gpu_native_required`, missing native
  surfaces or compositor failure aborts the frame; there is no CPU fallback.
  Text runs cannot join an isolated group (`svg_group_text_run_unsupported`).
- Software lane: retains the pixelwise premultiplied source-over oracle.
- Cache keys fold the group id and group opacity per item
  (`MultiSourceNode::cache_key()`), so frames with different group opacity
  never share a stale entry.

## Acceptance gates

- [x] ADR-035 records the decision, the oracle and the demolition-debt card
  (2026-10-17).
- [x] Loader annotation coverage: single-level group opacity annotates
  `group_id`/`group_opacity` on each element (element opacity stays 1);
  distinct sibling groups get distinct non-zero ids. Loader regression:
  `chronon3d_svg_path_loader_tests` 37/37 cases, 216/216 assertions
  (`Status: SUCCESS!`, SVG-enabled software build).
- [x] Nested isolated groups inside an isolated region fail closed: loader
  sentinel consumes the document through the certified diagnostic. CLI
  probe (2026-10-17): plan preparation fails with
  `SVG animation is malformed or outside the supported bounded subset`
  instead of rendering the wrong alpha.
- [x] Node group stage implementation in `MultiSourceNode::execute()`
  (full-alpha children, single post-composite, cache-key fold, native
  fail-closed). Tree rebuilds clean: `ninja -j14` exit 0 (600/600 targets,
  SVG-enabled software build).
- [x] Overlap oracle (relational, color-space agnostic): with a 0.5-opacity
  group over an opaque red background and green/blue overlapping children,
  (a) the overlap pixel reproduces the top child exactly (observed
  (188,0,188) == blue-only (188,0,188)); (b) both children share one
  constant group factor (R = 188 for both); (c) the lower sibling's channel
  is absent from the overlap (green channel 0 — no fold double-darkening);
  (d) the background stays pure red. Rendered through the canonical CLI
  software path from `golden/svg_group_isolation_gate/`.
- [x] Certification gate `scripts/ci/check_svg_group_isolation_gate.py`:
  visible frames, relational overlap oracle, animated translated child
  endpoint hashes differ (0 vs 30), repeat render reproduces the mid-frame
  hash. Observed run: `SVG_GROUP_ISOLATION_GATE PASS` (5 PASS checks) on
  the SVG-enabled software CLI build.
- [ ] Vulkan strict-native overlap oracle, animated endpoint hashes and
  repeat-frame hash through an SVG+Vulkan CLI build. The test uses
  `CHRONON_GPU_CLI` and fails on native-composite errors instead of accepting
  CPU fallback.
- [x] No regression on the existing SVG gates after the group stage:
  `check_svg_cli_frame_gate.py` PASS (endpoint hashes unchanged:
  f35d0772… / 55bf88db…) and `check_svg_motion_animations_gate.py` PASS
  (six base animations + animated repeater, 17 checks).
- [x] Full C ABI regression: `chronon3d_c_abi_tests` 300/300 cases,
  7720/7720 assertions (`Status: SUCCESS!`).

## Open work

- [ ] **Q2 — nested animated groups** (bounded depth) on the group pass:
  Loader/GroupTransformPlan tree model, per-level transform/opacity
  composition, CLI coverage.
- [ ] **Q3 — per-element distinct clip scopes** via sub-layer fan-out on
  the group machinery: design diagram inside this contract; compound
  nested-group clip scopes fail closed until then.
- [ ] Composite showcase gate re-run on the video-capable build
  (`linux-video-fast-dev`) after the group stage — is being verified on
  this tree; record the result before closing.
- [ ] Measure GPU time and peak memory for one, five and ten isolated groups
  at 1920x1080; the current intermediate and weighted blend cover full frames.
- [ ] Architecture gates re-run on the final tree (206 rules) before
  canonical status update.
- [ ] Push + clean-SHA CI on the exact SHA (repo-level P0, managed in
  `TICKET-MAIN-TRUTH-STAGE-1-2`).

## Bounded out for architectural reasons

- **Nested animated groups beyond V1** and animated-group paint animations
  (fill/stroke at group level): fail closed; the native track vocabulary is
  per-path and Q2 will extend it with its own gates.
- **Native (GPU surface) group stage**: implemented using the existing Vulkan
  weighted compositor; keep the strict-native hardware gate open until a
  same-tree run passes on a real device (zero-readback doctrine stays).
- **Blend modes between groups (`mix-blend-mode`, `isolation`)**: no plan
  vocabulary; fail closed with typed diagnostics.

## Demolition debt

- **Owner**: render-plan/compiler.
- **Reason**: the paint-alpha fold for the group's own opacity was removed
  for opacity < 1 groups, but opacity == 1 groups still lower through the
  legacy inherited-opacity path (no group stage, zero overhead). Q2 must
  decide whether opacity==1 groups lower through `svg_group` as well before
  the fold branch can be deleted.
- **Exit condition**: all fixtures and certified scenes lower group
  opacity through `svg_group`; the loader's group-opacity inheritance
  branch is dead.
- **Equivalence test/gate**: `check_svg_motion_animations_gate.py`,
  `check_svg_group_isolation_gate.py`, loader + compiler fixtures.
- **Removal scope**: group-opacity inheritance branches in
  `svg_path_loader.cpp` and the group fold path in the lowering.
- **Status**: `ACTIVE`.
