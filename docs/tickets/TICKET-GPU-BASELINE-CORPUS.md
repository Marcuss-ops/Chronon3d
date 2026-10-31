# TICKET-GPU-BASELINE-CORPUS — Official GPU baseline runner

## Stato

PARTIAL / RUNNER-AND-MANIFEST-GATED / CONTENT-PACK-AND-NATIVE-FRAME-CERTIFICATION-BLOCKED (2026-10-30)

## Acceptance checklist — aggiornato 2026-10-30

- [x] Manifest versionato con 17 ID/casi canonici, default Vulkan/native NVENC strict, acceptance esplicita e B09 a 18.000 frame.
- [x] Runner fail-closed su JSON/sidecar incompleti, attribution <95%, frame count timeline/sidecar/output discordanti, reread diverso da zero, fallback software/pipe, handoff non diretto, backend non Vulkan e contatori readback/copie mancanti o non-zero.
- [x] Provenienza registrata nel report: commit/stato source, SHA-256 CLI e manifest, ID content pack e identità host/GPU/FFmpeg. Un run `--case` resta diagnostico, mai certificazione corpus.
- [x] Preflight del corpus prima del rendering; tutte le 17 scene devono risultare registrate, altrimenti produce 17 righe BLOCKED e lancia zero render.
- [x] Test automatici del manifesto e dei pass/fail del runner con sidecar synthetic; comando e risultati nel closeout evidence.
- [ ] **Rendere verdi i casi su una build/fixture corretta e certificare 17/17.** Bloccato: l’host locale è `CHRONON3D_BUILD_CONTENT=OFF`, non contiene `content/` e nessuna delle 17 scene canoniche risulta registrata. Il preflight 2026-10-30 ha restituito 17 `composition_not_registered`, senza render.
- [ ] **Risoluzione discrepanza native frame count e smoke B00.** Sul `DarkGridBackground` reale a 120 frame il file passa `ffprobe` (120 frame, 30 fps), ma sidecar indica solo 1 native surface + 1 native NVENC frame (`execution_reason=direct_yuv_capability_rejected`); il gate rigetta correttamente il caso. Il render diagnostico a 2 frame ha inoltre fallito la verifica container: 60 fps osservati contro 30 richiesti. Entrambi i risultati sono registrati come FAIL, non come baseline.

## Criteri di certificazione (tutti richiesti per ogni caso)

1. Una riga/caso per ciascun ID canonico e scena registrata; render riuscito e output validato.
2. `frames_total`, numero di `frame_times_ms`, finestra `steady_state.frame_count` e conteggio encoded via `ffprobe` uguali al workload del manifest.
3. Attribution wall time ≥95%; bucket temporali obbligatori numerici, finiti; finestre prewarm/steady presenti.
4. `job.full_file_reread_count = 0`; `job.execution_path=full_graph_native`; backend Vulkan effettivo e handoff diretto.
5. Per native encode: `gpu_native_surface_frames` e `gpu_native_encode_frames` devono uguagliare esattamente i frame attesi; fallback software e pipe, GPU surface-copy, CPU readback e encoder staging-copy devono essere zero.
6. Report con commit/build binario/manifest/pack/GPU/FFmpeg identificati; source tree pulito. I report diagnostici possono avere `case_gate_pass=true`, ma `certification_eligible=false` se il manifest completo, la provenienza o l’host non soddisfano i gate.
7. Run di tutti i 17 casi sotto un solo manifest, incluso B09 da 18.000 frame. Nessun caso saltato o sostituito.

Vedi [`gpu-corpus-runner-closeout-20261030.md`](../evidence/gpu-corpus-runner-closeout-20261030.md) per comandi, host, output dei test, report preflight e i due smoke B00 bloccati.

## Contract

`tools/benchmark_gpu_baseline.sh` is the single runner for reproducible GPU
baseline measurements. It invokes the canonical `render` command with fixed
backend, encoder, frame count and framebuffer warm-up options. It reads only
the authoritative `*.timing.json` sidecar for Chronon timings and retains the
process log only for diagnostics.

The versioned case list is
`docs/benchmarks/gpu-corpus-v1.json`. Pass it with `--corpus` to run the
complete B00–B11 + GPU_TEXT_01–05 set under one shared parameter set; use
repeatable `--case ID=SCENE` only for an explicit smoke or content-pack
override. The manifest names are resolved by the active content pack; it does
not add a second composition registry or fabricate fixtures.

Each case records the exact render command, output and sidecar paths, exit
status, frame counts from the frame list/sidecar/encoded file, execution path,
native surfaces/encodes, fallback and copy/readback counters, startup/prepare/
encode/finalize timings, prewarm/steady-state windows, and canonical
`phase_attribution` with accounted percentage. The report also records source,
binary, manifest, content-pack and host identity.

The authoritative `job.full_file_reread_count` field reports whether mux
finalization had to reread the completed file after a container seek/backpatch.
Certified append-only runs must report zero; non-zero values are an explicit
container fallback and cannot be mistaken for an incremental-hash result.

The runner rejects a case when the sidecar or required metrics are missing,
frame counts differ, an execution/copy/fallback criterion fails, or less than
95% of process wall time is attributed. It checks every canonical scene is
registered before starting any render. A fail-closed report prevents partial
or non-GPU results from becoming a baseline.

## Usage

```bash
CHRONON3D_CONTENT_PACK_ID=<immutable-pack-revision-or-sha> \
bash tools/benchmark_gpu_baseline.sh \
  --out artifacts/gpu-baseline-<commit> \
  --corpus docs/benchmarks/gpu-corpus-v1.json \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --assets-root <pack-assets-root>
```

The runner refuses dirty source trees by default. `CHRONON3D_ALLOW_DIRTY=1` is
only for local diagnostics and always makes the report ineligible for
certification. Manifest-wide frame/warmup parameters are authoritative; do not
override them on a corpus run.

The typewriter and B00–B11 names are supplied by the active content pack;
Chronon does not register fake benchmark scenes. With no `--case`, the
runnable built-in smoke case is `B00=DarkGridBackground`, but it is diagnostic
only and cannot certify the canonical B00 content scene.

## Evidence

### Historical evidence (2026-10-26)

The versioned JSON contains 17 cases with unique, non-empty IDs and scene
names. `tests/test_gpu_corpus_manifest.py` passes locally. Runtime certification
remains blocked because this checkout does not contain the B00–B11/GPU_TEXT
content pack and the local CLI does not register those scenes.

Historical same-SHA hardware smoke is recorded in
[`gpu-b00-native-smoke-a16c86de2`](../evidence/gpu-b00-native-smoke-a16c86de2.md);
it is not evidence for the current checkout. Current-run details and outcome
are recorded in
[`gpu-corpus-runner-closeout-20261030.md`](../evidence/gpu-corpus-runner-closeout-20261030.md).
The local 120-frame built-in smoke produced a valid 120-frame, 30 fps file
and achieved 98.4976% wall attribution with zero readback/copy/software/pipe
fallback, but only 1/120 native surfaces/encodes; the runner correctly rejects
it. Full B00–B11 + GPU_TEXT certification is blocked by the absent external
content pack.

On 2026-09-11 the complete 17-case manifest was invoked against the current
`linux-video-test` build. All cases returned before producing a timing
sidecar because this checkout is configured with
`CHRONON3D_BUILD_CONTENT=OFF` and does not contain the external content
module. The runner records this as `command_failed_before_sidecar` with the
exit code and log path, rather than misclassifying it as a wall-time
attribution failure. This is a host/content-pack gate, not a baseline result.
