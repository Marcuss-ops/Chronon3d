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
| Motion lowering Z convention (`planPosition` vs `planAnimationPosition`) | P2 | OPEN (F3, ChrononMotion3D) | [CAMERA-DEFINITIVE-CLOSURE §5](tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md) |
| Camera/video execution parity | P1 | OPEN — camera/DoF matrix and remaining legacy `Composition` executor migration; pixel equivalence NOT RUN | [AE-CAMERA-FUNCTIONAL-PARITY](tickets/TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md), [COMPOSITION-LEGACY-EXECUTOR-DEMOLITION](tickets/TICKET-COMPOSITION-LEGACY-EXECUTOR-DEMOLITION.md) |
| Camera surface consolidation (CM3D+C3D) | P2 | PARTIAL — S0/S1/S4 and S3 legacy DoF wire removal landed 2026-10-31; S2 preset/applier collapse and S5 producer migration remain open | [CAMERA-SURFACE-CONSOLIDATION](tickets/TICKET-CAMERA-SURFACE-CONSOLIDATION.md) |
| Maintenance hotspots (W0–W5) | P1 | ACTIVE — umbrella: GPU lifetime/teardown, text hot-path fixes, certification-truthfulness decisions (B.1/B.2/F.2), effects-lane arch gate; coordinates C22/C23 and MAIN-TRUTH owners | [MAINTENANCE-HOTSPOTS-BONIFICATION](tickets/TICKET-MAINTENANCE-HOTSPOTS-BONIFICATION.md) |

Camera ownership migration, the current daemon admission policy, and node
memory instrumentation are closed for the present product scope. The optional
T17/T18 showcase certification was retired with its ticket (removed 2026-10-27)
and does not block release.

## Removed from this index

Resolved, landed, and deferred items stay in their owning tickets/evidence; this
index lists only active blockers. The unused spatial motion-path family removal
is documented in git history (ticket retired after its scope was removed).
