# TICKET-SVG-GROUP-ISOLATION — Isolated group compositing for lowered SVG

## Stato
PARTIAL — software nested opacity scopes, animated group opacity, and per-
element clip/mask scopes pass the raw-pixel gate (2026-10-20). Vulkan strict-
native hardware certification and performance measurements remain open.

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

## Contratto (software + Vulkan)

- The SVG loader annotates each element with its ordered enclosing group
  scopes (`SvgStyleGroupRef`: id, opacity, optional opacity track) instead of
  folding group opacity into child paint alpha. Nested isolated scopes are
  supported through depth 16.
- The render-plan compiler propagates `ImportedSubpathPlan::svg_group`
  (id, opacity) without folding it into fill/stroke alpha; the element's own
  opacity stays folded (correct per-paint alpha). Validation: nonzero id,
  opacity in [0,1].
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
- [x] Historical V1 behavior: nested isolated groups failed closed on
  2026-10-17. Superseded by the nested scope support recorded in the
  2026-10-20 update below.
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

## Session update — 2026-10-19 (ADR-035 restore)

- The integration refactor (091bea87) had silently dropped the
  `builder.svg_group_isolation` call plus the loader→plan `svg_group`
  chain; re-running the gate showed the isolation compositing folding back
  into plain alpha over (137 instead of 188 constant factor). Restored the
  full ADR-035 chain end to end: loader annotation on every sibling of an
  isolating group, plan field re-added, compiler call at both emit points.
  The gate passed again, with the addition that the repeat render
  reproduces the mid-frame hash (determinism) and the animated endpoints
  differ (0 vs 30).
- Loader suite extended to guard the restored contract: 32 cases,
  228 assertions pass at that revision. Nested opacity-bearing groups were
  still fail-closed at the time; this limitation was removed in the
  2026-10-20 update below.
- Composite showcase re-run PASS on the updated video-test CLI: 31 raw
  frames in each run, all byte-identical across repeats, changing endpoints,
  and h264 128×128 MP4 with 31 frames. The gate no longer uses PNG output.
- Architecture gate PASS: 206/206 rules at that revision. The motion gate
  retains strict repeater endpoint/coverage assertions; the Lottie
  animated-operator loss was fixed, not waived as a known defect.
- Final local CTest regression PASS: both registered loader/C ABI suites
  passed (253.27s total). This checkout contains 277 C ABI cases; older
  300-case figures above describe earlier trees, not this run.
- Full 1080p30 SVG showcase: two 360-frame raw renders compared, all 360
  hashes identical. MP4 derived from raw has 360 frames / 12 seconds.
  Logs, CPU/RSS measurements, build provenance and remaining certification
  limits are recorded in TICKET-SVG-RENDER-PLAN; no clean-SHA/GPU claim.

## Open work

- [x] **Q2 — nested static group opacity**: ordered ancestor scope chain,
  multi-level isolated surfaces (depth limit 16), cache identity and raw
  software pixel coverage. Verified on 2026-10-20 with loader CTest,
  nested-opacity overlap oracle, and motion gate.
- [x] **Q2 — nested animated transform operations** (bounded sequence): the
  loader preserves each ancestor/descendant animateTransform with its own
  timing, and the renderer evaluates the ordered matrix chain. Motion gate
  covers a rotating parent with a translated child. Static transform plus
  animated transform and rotation pivots remain fail-closed.
- [x] **Q3 — per-element distinct clip scopes**: loader carries each
  element's inherited group and local clip/mask geometries independently;
  the software compositor intersects them per element. Pixel gate covers
  sibling paths with distinct child clips nested under a group clip.
- [x] Composite showcase gate re-run on the video-capable
  `linux-video-test` build: raw repeat comparison + MP4 PASS.
- [ ] Measure GPU time and peak memory for one, five and ten isolated groups
  at 1920x1080; the current intermediate and weighted blend cover full frames.
- [x] Architecture gate re-run on the corrected source tree: 206/206 PASS.
- [ ] Push + clean-SHA CI on the exact SHA (repo-level P0, managed in
  `TICKET-MAIN-TRUTH-STAGE-1-2`).

## Session update — 2026-10-20 (nested opacity and per-element clips)

- Carried all ancestor opacity scopes through loader → render plan → scene →
  multi-source items. Software rendering isolates each level and composites
  from inner to outer; supported depth is 16.
- Loader regression and end-to-end pixel oracle now accept nested opacity.
  The oracle checks overlapping siblings and sees `(231,0,124,255)` in the
  nested overlap. Existing single-group, animation endpoint and determinism
  checks pass.
- The same software path applies distinct group and child clip scopes per
  element. Loader CTest and the expanded raw pixel gate pass. Vector masks
  use the software compositor; GPU-native execution still needs separate
  certification.

## Session update — 2026-10-20 (nested animated transforms)

- Replaced the one-transform-per-path reduction with an ordered sequence of
  translate/scale/rotate tracks, retaining each track's timing and interpolation.
- Render-time sampling applies operations from descendant to ancestor, which
  yields the SVG parent × child matrix order. Cumulative scale also adjusts
  stroke width.
- The motion gate now renders the rotating-parent/translated-child fixture;
  its endpoints are both visible and differ. Loader CTest and the complete
  SVG motion gate pass on the software CLI.

## Session update — 2026-10-20 (animated nested group opacity)

- Direct group `<animate attributeName="opacity">` is retained as a group
  scope track and sampled on the composition frame before software isolation.
  A zero opacity scope remains valid and composites a transparent surface.
- The group and motion gates cover this scope alongside nested animated
  transforms; strict-native Vulkan remains separately unverified.

## Bounded out for architectural reasons

- **Animated group paint animations** (fill/stroke at group level): remain
  unsupported; the bounded native track vocabulary is per-path.
- **Native (GPU surface) group stage**: implemented using the existing Vulkan
  weighted compositor; keep the strict-native hardware gate open until a
  same-tree run passes on a real device (zero-readback doctrine stays).
- **Blend modes between groups (`mix-blend-mode`, `isolation`)**: no plan
  vocabulary; fail closed with typed diagnostics.

## Demolition debt

- **Owner**: render-plan/compiler.
- **Reason**: opacity 1 groups with no group-level effects need no isolation
  surface and continue through the legacy inherited path. This intentional
  no-op case does not affect animated or opacity-bearing scopes.
- **Exit condition**: all fixtures and certified scenes lower group
  opacity through `svg_group`; the loader's group-opacity inheritance
  branch is dead.
- **Equivalence test/gate**: `check_svg_motion_animations_gate.py`,
  `check_svg_group_isolation_gate.py`, loader + compiler fixtures.
- **Removal scope**: group-opacity inheritance branches in
  `svg_path_loader.cpp` and the group fold path in the lowering.
- **Status**: `RETAINED` (optimization contract; no parity gap).
