# TICKET — Copy lane: la dichiarazione del producer diventa un confine verificato

> **Stato:** LANDED — verificato con test CLI (69 casi / 991 assertion, 0 failed), architecture gate 191/191,
> gate selftest 27 assertion, link delle due configurazioni (video-on e video-off).
> **Scope:** `apps/chronon3d_cli/` (render + job), `apps/chronon3d_cli/daemon/`, `src/render_graph/pipeline/`,
> `src/compiler/graph_compiler/`, `tests/cli/`.
> **Metodo:** il percorso reale (non la doc) è stato letto fino al punto in cui il piano decodificato e il job
> normalizzato si incontrano; il rifiuto vive lì, una sola volta, per CLI e daemon.

---

## 1. I due buchi (output sbagliato, exit 0)

La lane copy pubblica i packet della sorgente compressa **verbatim** per ogni intervallo che il chiamante non
ha dichiarato come cambiato, e renderizza/encoda solo quelli dichiarati. Questo rende la dichiarazione
**load-bearing** — e nessuno la confrontava con il piano.

| # | Caso | Prima | Conseguenza | Ora |
|---|---|---|---|---|
| A | `--gop-copy-only` (o `gop_copy_only` nel payload IPC) su un piano che disegna layer | `validate_video_job` controllava solo che `gop_source` non fosse vuoto | base copiato, overlay assenti, exit 0 | rifiuto nominando il flag e il primo layer colpevole |
| B | `changed_intervals` che non coprono la finestra attiva di un layer | `inspect_gop_source` usava la dichiarazione senza confrontarla col piano | i packet del base finiscono nei frame in cui l'overlay doveva essere impresso, exit 0 | rifiuto con la finestra scoperta (`da-a`) e il rimedio |

Entrambi sono la stessa failure: i pixel dichiarati dal piano e i pixel pubblicati dalla lane non
coincidono e il job riporta successo.

---

## 2. Fix A/B — l'authority: `copy_lane_coverage`

- `apps/chronon3d_cli/utils/video/copy_lane_coverage.hpp|.cpp` — funzione pura
  `validate_copy_lane_coverage(plan, gop_copy_only, changed_intervals)`.
- Call site unico: `execute_render_plan` in `apps/chronon3d_cli/commands/render/command_render_plan_internal.inc`,
  subito dopo `prepare_render_plan` — è l'unico punto attraversato sia dalla CLI sia dal daemon, dove piano
  decodificato e job normalizzato coesistono.
- Additività: ogni layer non-video è additivo; il **primo** layer video è la base copiata (caso passthrough),
  un secondo layer video (picture-in-picture) no. Un layer che non può disegnare (opacità 0, testo vuoto,
  finestra che non tocca la timeline) non è additivo.
- Finestre: `duration_frames` assente **o 0** significa illimitato, non zero-lunghezza — è la semantica del
  render path (`direct_yuv_program.cpp`: `duration > 0 && frame >= from + duration`). Leggere 0 come
  "non disegna" avrebbe lasciato passare un copy-only con un layer che copre tutto l'export.
- `trim` non licenzia mai una copia: cambia la finestra pubblicata, non i pixel.
- Timeline o frame rate non provabili ⇒ **fail closed**: nessuna dichiarazione finita può essere dimostrata
  coprente, e il messaggio dice quale campo del piano manca.

**Cosa NON verifica** (dichiarato, per non spacciare il confine per più di quello che è): che gli intervalli
copiati siano quelli *giusti*, che il layer video di base sia davvero la sorgente compressa, e nulla sugli
intervalli `trim`.

---

## 3. Fix C — vocabolario chiuso del payload IPC

L'ingresso del piano era validato con uno schema `additionalProperties: false`; il payload `RENDER_JOB` no.
Lo **stesso** refuso veniva rifiutato su un confine e silenziosamente sostituito col default sull'altro:
il job girava con un valore che il chiamante credeva di aver impostato.

- `apps/chronon3d_cli/daemon/daemon_service_ipc.cpp`: `reject_unknown_render_job_keys` valida radice,
  `output_spec`, `execution_requirements` e le voci di `changed_intervals`; ogni chiave fuori vocabolario è un
  `BadRequest` che la nomina. Un contenitore presente ma del tipo sbagliato (`"output_spec": "nope"`) non è più
  "nessuna output spec": è errore.
- Il vocabolario è la **unione verificata** di ciò che il client RenderingGen invia
  (`renderinggen/internal/chronon/client.go`: `renderJobPayload`, `OutputSpec`, `ExecutionRequirements`) e delle
  chiavi legacy di parità CLI. `apps/chronon3d_cli/daemon/daemon_render_job_payload.hpp` dichiara che il
  vocabolario è chiuso e che aggiungere una chiave significa aggiungerla al decoder.

---

## 4. Fix D — i log per-frame dietro il gate

`spdlog::set_level` non è mai chiamato in produzione (solo nei test) e `render_args.hpp` imposta `info` come
default: i due `spdlog::info` di `build_or_reuse_graph` in `src/render_graph/pipeline/graph_cache_coordinator.cpp`
venivano formattati **una volta per frame** per tutto l'export (~29k righe per un job 10 min @24 fps, moltiplicate
per N lingue). Ora sono dietro `diagnostics_enabled`, lo stesso gate di `log_graph_cache_diagnostics` che sta
nello stesso file. Gli altri log per-frame erano già limitati (gate diagnostics o `frame <= 2`) o throttled
(`should_log_pipe_progress`): verificato, non toccati. Il livello di default resta `info`: le righe che restano
sono per-job e servono.

---

## 5. Fix E — scratch del fused pixel program limitato

`transformed_scratch()` era `static thread_local` e cresceva fino a `4·pixels` float **per thread, senza mai
restringersi**: 132 MiB a 4K per worker, e il pool usa `hardware_concurrency()`
(`src/core/scheduler/execution_scheduler.cpp`) con `parallel_tiles` attivo di default. Ora il buffer è
mantenuto solo entro `kMaxRetainedScratchBytes` (8 MiB); sopra quella soglia è di proprietà della chiamata e
viene rilasciato con essa. Resta azzerato, che è parte del contratto (il kernel 3x4 non scrive ogni canale).

---

## 6. Fix F — regressione di link introdotta da `50a602208`

`cli::hardware_nvenc_requested` / `normalize_hardware_encoder_field` erano definite in
`apps/chronon3d_cli/utils/video/encoder_config_resolution.cpp` (libreria `chronon3d_cli_video_export`), ma i
chiamanti stanno in `chronon3d_cli_render` (comando render e job planner), che **non** linka quella libreria:
in configurazione video-off il link falliva con `undefined symbol: cli::hardware_nvenc_requested`. Sono ora
`inline` nell'header, come delegazione a `chronon3d::media` (l'authority effettivamente usata da daemon ed
encoder). È il motivo per cui il gate di build deve coprire **entrambe** le configurazioni.

---

## 7. Evidenza riproducibile

| Cosa | Comando | Esito |
|---|---|---|
| Casi CLI (inclusi i nuovi) | `build/chronon/linux-fast-dev/tests/chronon3d_cli_tests` | `69 passed / 0 failed`, 991 assertion |
| Architettura | `python3 tools/check_architecture.py` | `GATE_PASS: 191/191` |
| Gate selftest | `bash tools/architecture_gate_selftest.sh` | `PASSED (27 assertions)` |
| Link video-off | `ninja chronon3d_cli_tests` (linux-fast-dev) | link OK (prima: undefined symbol, vedi §6) |
| Link video-on | `ninja chronon3d_cli` (linux-video-release) | link OK |
| Rifiuto A end-to-end | `chronon3d_cli render --plan plan.json --gop-copy-only --gop-source plan.json -o out.mp4` | `[video] refusing copy-lane job: gop_copy_only publishes the compressed source verbatim, but plan 'copy-lane-smoke' draws 1 layer(s) ...` |
| Rifiuto B end-to-end | `chronon3d_cli render --plan plan.json --gop-source plan.json --changed-interval 5:7:content -o out.mp4` | `declared changed intervals do not cover color layer 'banner': active 5.000000s to 8.000000s, undeclared 7.000000s-8.000000s.` |
| Controllo negativo | stesso piano con `--changed-interval 5:8:content` | nessun rifiuto copy-lane; il job prosegue nell'encoder |

---

## 8. Residuo dichiarato

- La copertura è verificata **solo** quando il chiamante dichiara intervalli. Con lista vuota vale la
  semantica legacy (tutto cambiato ⇒ niente copiato) e non c'è nulla da coprire: è intenzionale, ed è il
  motivo per cui una dichiarazione *assente* non è più pericolosa di prima.
- Il rifiuto avviene sul job, non sulla lane: la decisione di lane resta del resolver. Il costo è un rifiuto
  conservativo se in futuro una lane diversa imparasse a rispettare la dichiarazione.
- `copy_gop_source()` resta come lane whole-clip e come oracle del gate (già tracciato come residuo in
  `TICKET-SCALE-P0-ASSET-VRAM-GOP.md`).
