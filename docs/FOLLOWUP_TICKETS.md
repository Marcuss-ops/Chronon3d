# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettagli e prove nelle schede collegate.

## Open blockers (max 10; one line per item)

| Area | Pri | Stato | Scheda |
|---|---:|---|---|
| Main truth / CI same-SHA | P0 | PARTIAL — latest Actions allocated runners but the batch on `a214fad` was not green | [MAIN-TRUTH-STAGE-1-2](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) |
| WBH certification | P1 | PARTIAL — clean same-SHA machine qualification remains | [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| FFmpeg pipe demolition | P1 | BLOCKED — codec/routing same-SHA green gate remains; explicit diagnostic pipe stays | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) |
| GPU benchmark corpus | P1 | WAITING EXTERNAL — manifest and runner are ready; the active content pack is absent from this checkout | [GPU-BASELINE-CORPUS](tickets/TICKET-GPU-BASELINE-CORPUS.md) |
| Fused-storage/native-image parity | P1 | PARTIAL — broader overlay corpus parity remains after the focused native-image certificate | [SCALE-P0-ASSET-VRAM-GOP](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) |
| Text AE parity + legacy `Composition` migration | P1 | OPEN — text corpus 8/8 miss thresholds; typed-particle C2 software pixels pass; dead Composition FlatBuffer cache adapter retired, broad executor equivalence NOT RUN | [text closure](tickets/TICKET-TEXT-CLOSURE-20261109.md), [executor demolition](tickets/TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION.md) |
| Maintenance hotspots (W0–W5) | P1 | ACTIVE — umbrella: GPU lifetime/teardown, text hot-path fixes, certification-truthfulness decisions (B.1/B.2/F.2), effects-lane arch gate; coordinates C22/C23 and MAIN-TRUTH owners | [MAINTENANCE-HOTSPOTS-BONIFICATION](tickets/TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION.md) |
| Cache identity / invalidation | P1 | ACTIVE — all taxonomy entries mapped to identity owners/dependency axes; remaining V2 schemas, mutation fixtures and test execution remain | [CACHE-IDENTITY-KEYS](tickets/TICKET-CACHE-IDENTITY-KEYS.md) |
| Gel text surface | P1 | PARTIAL — supplied still matches; extreme tracking, depth/DoF, moving-footage and performance gates remain open or NOT RUN | [TEXT-COMPOSITE-GEL-SURFACE](tickets/TICKET-TEXT-COMPOSITE-GEL-SURFACE.md) |
| Render-stack hardening (C3D↔CM3D) | P0 | OPEN — W1 SyncVal audit and W2 same-SHA pair are the gates; W3/W4 closure; no new graphics stack | [RENDER-STACK-HARDENING](tickets/TICKET-RENDER-STACK-HARDENING-20261106.md) |

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
