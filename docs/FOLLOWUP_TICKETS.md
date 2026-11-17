# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettagli e prove nelle schede collegate.

## Open blockers (max 10; one line per item)

| Area | Pri | Stato | Scheda |
|---|---:|---|---|
| Main truth / CI same-SHA | P0 | PARTIAL — latest Actions allocated runners but the batch on `a214fad` was not green | [MAIN-TRUTH-STAGE-1-2](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) |
| Render-stack hardening (C3D↔CM3D) | P0 | OPEN — W1 SyncVal audit and W2 same-SHA pair are the gates; W3/W4 closure; no new graphics stack | [RENDER-STACK-HARDENING](tickets/TICKET-RENDER-STACK-HARDENING-20261106.md) |
| WBH certification | P1 | PARTIAL — clean same-SHA machine qualification remains | [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| FFmpeg pipe demolition | P1 | BLOCKED — codec/routing same-SHA green gate remains; explicit diagnostic pipe stays | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) |
| SVG CLI render lane (path + operator animation) | P1 | OPEN — six path/motion gates fail on the current CLI lane: imported SVG subpaths are not materialized and operator animations are baked at frame 0; eight text gates pass (2026-11-16) | [SVG-CLI-LANE](tickets/TICKET-SVG-CLI-RENDER-PLAN-LANE-20261116.md) |
| Maintenance hotspots (W0–W5) | P1 | ACTIVE — umbrella: GPU lifetime/teardown, text hot-path fixes, certification-truthfulness decisions (B.1/B.2/F.2), effects-lane arch gate; coordinates C22/C23 and MAIN-TRUTH owners | [MAINTENANCE-HOTSPOTS-BONIFICATION](tickets/TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION.md) |
| Shadow map / PCF graph integration | P1 | PARTIAL — backend PCF PASS on RTX A4000 (2026-11-16); light-space depth pass not consumed by the render graph, production audit and same-SHA CI open | [SHADOW-MAP-PCF](tickets/TICKET-SHADOW-MAP-PCF-20261106.md) |
| Fused-storage/native-image parity | P1 | PARTIAL — broader overlay corpus parity remains after the focused native-image certificate | [SCALE-P0-ASSET-VRAM-GOP](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) |
| GPU benchmark corpus | P1 | PARTIAL — manifest and runner are ready; the active content pack is absent from this checkout (external) | [GPU-BASELINE-CORPUS](tickets/TICKET-GPU-BASELINE-CORPUS.md) |
| Gel text surface | P1 | PARTIAL — supplied still matches; extreme tracking, depth/DoF, moving-footage and performance gates remain open or NOT RUN | [TEXT-COMPOSITE-GEL-SURFACE](tickets/TICKET-TEXT-COMPOSITE-GEL-SURFACE.md) |

Text AE parity is no longer an active blocker: it closed on 2026-11-10 by
product exception [ADR-040](adr/ADR-040-text-parity-product-exception.md); 1/8
frozen cases pass and the residual is documented, not PASS.

This table is the canonical 10-row blocker index; [`CURRENT_STATUS.md`](CURRENT_STATUS.md)
mirrors it row-for-row. Update both in the same change.

Engine gaps for the faceless shorts formats (2026-11-09) are not a separate
renderer: they are registered in
[TICKET-CM3D-ENGINE-FACELESS-GAPS](../../ChrononMotion3D/docs/tickets/TICKET-CM3D-ENGINE-FACELESS-GAPS-20261109.md)
(E1–E12). E1 is the Vulkan inline-mesh material gap and belongs to the
render-stack row above; E3 (rigged GLB → RenderPlan) and E2 (face rig/viseme,
optional) live in ChrononMotion3D.

Camera: the four camera sheets were closed administratively on 2026-11-04.
C7/CI, AE parity, S2/S5 and advanced DoF are tracked in the
[master](tickets/TICKET-CHRONON-2.0-MASTER.md) (P0-10/P1-10/P1-11/P1-16) and
are not `PASS`.

Camera ownership migration, the current daemon admission policy, and node
memory instrumentation are closed for the present product scope. The optional
T17/T18 showcase certification was retired with its ticket (removed 2026-10-27)
and does not block release.

## Removed from this index

Resolved, landed, and deferred items stay in their owning tickets/evidence; this
index lists only active blockers. The unused spatial motion-path family removal
is documented in git history (ticket retired after its scope was removed).
