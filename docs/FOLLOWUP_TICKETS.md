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
| Native NVENC + software backend (wrong YUV planes) | P1 | OPEN (F1) — workaround: `--encoder-backend pipe --pipe-pixfmt nv12` | [CAMERA-DEFINITIVE-CLOSURE §5](tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md) |
| Software DoF performance (~30 s/frame 1080p) | P2 | OPEN (F2) | [CAMERA-DEFINITIVE-CLOSURE §5](tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md) |
| Motion lowering Z convention (`planPosition` vs `planAnimationPosition`) | P2 | OPEN (F3, ChrononMotion3D) | [CAMERA-DEFINITIVE-CLOSURE §5](tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md) |

Camera ownership migration, the current daemon admission policy, and node
memory instrumentation are closed for the present product scope. The optional
T17/T18 showcase certification was retired with its ticket (removed 2026-10-27)
and does not block release.

## Removed from this index

Resolved, landed, and deferred items stay in their owning tickets/evidence; this
index lists only active blockers. The unused spatial motion-path family removal
is documented in git history (ticket retired after its scope was removed).
