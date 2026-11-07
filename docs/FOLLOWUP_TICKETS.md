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
| Legacy `Composition` executor migration | P1 | OPEN — pixel equivalence NOT RUN | [COMPOSITION-LEGACY-EXECUTOR-DEMOLITION](tickets/TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION.md) |
| Maintenance hotspots (W0–W5) | P1 | ACTIVE — umbrella: GPU lifetime/teardown, text hot-path fixes, certification-truthfulness decisions (B.1/B.2/F.2), effects-lane arch gate; coordinates C22/C23 and MAIN-TRUTH owners | [MAINTENANCE-HOTSPOTS-BONIFICATION](tickets/TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION.md) |
| Cache identity / invalidation | P1 | ACTIVE — all taxonomy entries mapped to identity owners/dependency axes; remaining V2 schemas, mutation fixtures and test execution remain | [CACHE-IDENTITY-KEYS](tickets/TICKET-CACHE-IDENTITY-KEYS.md) |
| Gel text surface / AE-style parity | P1 | OPEN — composed TextRun surface, glossy shading and effect compositing remain unverified | [TEXT-COMPOSITE-GEL-SURFACE](tickets/TICKET-TEXT-COMPOSITE-GEL-SURFACE.md) |
| Render-stack hardening (C3D↔CM3D) | P0 | OPEN — W1 SyncVal audit and W2 same-SHA pair are the gates; W3/W4 closure; no new graphics stack | [RENDER-STACK-HARDENING](tickets/TICKET-RENDER-STACK-HARDENING-20261106.md) |

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
