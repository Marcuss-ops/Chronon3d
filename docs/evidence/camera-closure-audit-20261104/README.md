# Camera scope closure audit — 2026-11-04

## Decisione

Le quattro schede camera ancora aperte sono state **chiuse
amministrativamente** il 2026-11-04. Non è una certificazione di release e non
spunta i gate non eseguiti: i residui esterni o non implementati restano
`BLOCKED`/`NOT RUN`, sono elencati sotto e restano tracciati nel master
(`TICKET-CHRONON-2.0-MASTER`, P0-10/P1-10/P1-11/P1-16).

Schede chiuse:

- `TICKET-CAMERA-DEFINITIVE-CLOSURE`
- `TICKET-CAMERA-SURFACE-CONSOLIDATION`
- `TICKET-AE-CAMERA-FUNCTIONAL-PARITY`
- `TICKET-DOF-OCCLUSION-ACCEPTANCE`

## Verifica locale eseguita

Comando:

```bash
ctest --test-dir Chronon3d/build/chronon/linux-video-release \
  -R '^(chronon3d_(camera|core|render_graph|renderer_core)_tests)$' \
  --output-on-failure
```

Risultato: **4/4 test PASS**, 142,65 s totali.

| Suite | Casi | Asserzioni | Esito |
|---|---:|---:|---|
| `chronon3d_camera_tests` | 93/93 | 68.186/68.186 | PASS |
| `chronon3d_core_tests` | 1.479/1.479 | 955.249/955.249 | PASS |
| `chronon3d_render_graph_tests` | 448/448 | 19.576/19.576 | PASS |
| `chronon3d_renderer_core_tests` | 207/207 | 72.605/72.605 | PASS |

Totale C3D: **2.227 casi, 1.115.616 asserzioni**.

Motion (binari `build/release-fast`, CTest `build/chronon/dev`):
**10/10 PASS** per `camera_v1`, `camera_focus_sequence`, `camera_trajectory`,
`camera_shake`, `camera_framing`, `camera_sequence`,
`documentary_camera_path`, `authoring`, `advanced_authoring`, `boundary`.
Conteggi diretti: `chrononmotion_authoring` 415 check, `camera_bake_roundtrip`
838 check, `camera_v1` 77 check, tutti con 0 failure.

Nota di metodo: eseguire `chronon3d_core_tests --success` fa fallire due test del
guard allocazioni perché la stampa delle asserzioni riuscite alloca dentro la
finestra misurata. La registrazione CTest e l'esecuzione senza `--success`
passano 1.479/1.479 e 955.249/955.249; il fallimento con `--success` non è una
regressione del prodotto.

## Limiti

- Working tree C3D dirty (`command_render_plan_internal.cpp`,
  `source_node_native_rect.cpp`) e Motion dirty: i risultati sono locali, non
  same-SHA. Motion usa i binari `release-fast` precompilati.
- Nessuna esecuzione GPU/Vulkan, nessun reviewer e nessun riferimento AE in
  questa sessione.

## Residui, esplicitamente non certificati

| Area | Stato | Dove resta tracciata |
|---|---|---|
| C7 review umana | `BLOCKED` — nessun reviewer nominato | master P0-10/P1-16 |
| CI same-SHA | `BLOCKED` — nessun runner/receipt sul pair | master P0-02 |
| Corpus Mixed 3D / native end-to-end | `NOT RUN` oltre i fixture locali | master P0-06/P0-07 |
| Parità funzionale AE B–J | demandata a v2.1 | master P1-10/P1-11 |
| Surface S2 (13→≤6 header) e S5 | demandata a v2.1 | master P1-10 |
| DoF multi-profondità, HDR bokeh, strict-native integrato | demandato a v2.1 | master P1-10/P1-11 |

Nessuna riga di questa tabella è un `PASS`. La chiusura delle schede non
autorizza a rimuovere test, né a dichiarare certificata la release 2.0.

## Demo Drive (1920×1080)

Il 2026-11-04 sono stati caricati nella cartella Drive richiesta
(`1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS`) tre clip della showcase Chronon3D già
presenti in `RenderingGen/UploadDrive`, verificati con `ffprobe` a
1920×1080/30 fps e tutti sotto i 10 s. Non sono un nuovo render di questa
sessione: sono la demo visiva dello stato attuale, non una certificazione.

| Clip | Durata | Byte | SHA-256 | Link |
|---|---:|---:|---|---|
| `01_glowing_path_follow.mp4` | 5 s | 149.260 | `3ff60e4160af5336c56b00d30f702a011693d9bffa2291dc1db7619bf930b3be` | [Drive](https://drive.google.com/file/d/1u8i6EtUkh2WvEnNh-S5oS2Fto0D61ycR/view) |
| `10_cinematic_parallax_scene.mp4` | 7 s | 126.222 | `6f17701b5a55b96ad4788fe6fdfbb06c71b59e54cfe7bbde490a911c14c1d962` | [Drive](https://drive.google.com/file/d/1ltXXnGQ374dhA1-kQJVf0jLuSjehxOnV/view) |
| `16_jerusalem_tactical_fill.mp4` | 4 s | 806.667 | `2f09b45e960162f7e2adf6800c3dc71d3bd5b821f68ac9dc2641c8a25872456c` | [Drive](https://drive.google.com/file/d/1NiOJU2jAU5tUn_Dmj6vBqeApscxwPM3q/view) |

## Archiviazione (2026-11-05)

Le quattro schede sono state spostate in `archive/` il 2026-11-05 e i link
relativi interni (evidenze, ADR e confine C3D/CM3D) sono stati riallineati.
Il tracker `TICKET-CAMERA-3D-V1-CLOSURE` è ricostruito e archiviato nello
stesso giorno: l'originale era un file untracked mai committato. La
dichiarazione `Stato: chiuso` resta la fonte dell'indice generato: la scheda
non compare più tra quelle non archiviate.
