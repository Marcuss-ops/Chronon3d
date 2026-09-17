# Verità pixel pre-encode — la via canonica (2026-09-17)

Ambito: [`TICKET-IMAGE-PATH-PIXEL-TRUTH`](../tickets/TICKET-IMAGE-PATH-PIXEL-TRUTH.md)
(exit condition 2) e [`TICKET-TEXT-QUALITY-V2`](../tickets/TICKET-TEXT-QUALITY-V2.md)
§P0-0b (la verità pre-encode che blocca la campagna qualità).

Ogni numero qui sotto è **misurato in questa sessione**; ciò che non è stato
eseguito è nominato in §5 come non eseguito.

---

## 1. Il problema in una riga

`render -o x.png` (via immagine: still e sequenza PNG) **non** è una superficie di
verità pixel: non compone il testo, non varia per frame né per backend, e
`scan_framebuffer_sanity` non può accorgersene perché il suo contratto è
NaN/Inf-only (`docs/evidence/image-path-defect-2026-09-16.md`). Di conseguenza
P0-0b di TEXT-QUALITY-V2 non aveva **nessun input** su cui misurare: né il PNG
still, né una conversione del raw sink.

## 2. Cosa è atterrato

**(a) La lane è dichiarata non-canonica da UNA authority.**
`apps/chronon3d_cli/utils/job/output_lane_pixel_truth.hpp`:

* `lane_is_pixel_truth_surface(RenderMode)` — vero solo per `RenderMode::Video`;
* `lane_notice(RenderMode)` — il testo canonico, vuoto quando la lane è una
  superficie di verità;
* `announce_lane(RenderMode)` — emesso **una volta per job** dai due punti di
  decisione della lane: `utils/job/render_job.cpp` (route `render`) e
  `commands/render/command_render_plan_internal.inc` (route `render --plan`).

Il messaggio nomina il sostituto (`--video-sink raw`) e il ticket, quindi un
operatore che vede un PNG senza testo non deve cercare la causa. Due messaggi
scritti due volte sarebbero derivati: la regola è una sola.

**(b) La via pre-encode canonica è eseguibile.**
`tools/raw_frame_to_png.py` estrae un frame dal raw sink come PNG **verbatim**
(RGBA8, filtro `None`, nessuna trasformazione di colore) e profila il frame con
le stesse statistiche dell'evidenza qualità:

```bash
tools/raw_frame_to_png.py --raw frames.raw --width 1920 --height 1080 \
    --frame 60 --out flat_still.png --require-content 0.05
```

Tre proprietà lo rendono una misura e non un dumper:

* **fail-closed sulla forma**: un file la cui dimensione non è un multiplo intero
  di `width*height*4` viene **rifiutato** (`frames x frame_bytes` è il contratto
  del raw sink), invece di reinterpretare un artifact troncato come immagine;
* **gate di contenuto**: `--require-content X` fallisce quando la frazione di
  pixel diversa dalla luma modale (il background) è sotto `X`. Una tela piatta —
  di qualunque colore, incluso il grigio 0.498 del difetto — vale **0** e non
  passa; un frame con testo composto passa;
* **profilo riproducibile**: `distinct_levels`, `background_luma`,
  `white_pixels`, `content_fraction`, percentili di luma — gli stessi numeri con
  cui è stato misurato il difetto.

**(c) Il contratto è pinnato da test** (`tests/cli/test_render_job_planning.cpp`,
suite `chronon3d_render_job_contract_tests`): una sola lane è superficie di
verità; il testo del notice nomina il sostituto e il ticket; il notice è
**keyed sulla lane scelta dal planner** (Still/Sequence → presente, Video →
vuoto).

## 3. Verifica eseguita

**(0) Il build lane canonico è certificato: ccache + mold + `-j16`.**
`tools/check_cli_tests.sh` con `JOBS=16` ha configurato `linux-fast-dev` in
`.tmp/chronon-builds/linux-fast-dev` e compilato/linkato `chronon3d_cli_tests`
(321 step). Evidenze raccolte:

* `-fuse-ld=mold` presente nella riga di link reale (`ninja -t commands
  chronon3d_cli_tests`), non solo nella cache: il linker è mold;
* ccache attivo come compiler launcher (155 hit / 594 miss nella sessione);
* **suite verde**: `ctest -R '^chronon3d_cli_tests$'` → `1/1 Passed 1.03 sec`,
e in esecuzione diretta **72/72 test case, 1005/1005 assertion, 0 failed**,
compresi i tre casi nuovi di §2(c). Il notice è stato **osservato emesso**
durante un piano Still (riga `[warning] pixel truth: …` nei log del test),
cioè la dichiarazione non è solo testo pinnato: viene emessa dove la lane
viene scelta.

**(a) Compilazione dei traducenti toccati** (syntax-only, include dir del
checkout + `vcpkg_installed/linux-fast-dev/x64-linux/include`) — controllo
ridondante, tenuto perché isola ogni TU dal resto del link:

| TU | esito | tempo |
|---|---|---|
| `apps/chronon3d_cli/utils/job/render_job.cpp` | PASS | 28.8 s |
| `apps/chronon3d_cli/commands/render/command_render_plan.cpp` | PASS | 53.3 s |
| `tests/cli/test_render_job_planning.cpp` | PASS | 27.5 s |

**(b) Il tool, end-to-end su un raw sintetico di 3 frame 64×48 RGBA8:**

| caso | esito |
|---|---|
| `--self-test` (round-trip PNG byte-faithful + profilo) | **OK** |
| frame 0 — tela grigia uniforme | `distinct_levels=1 luma_p50=0.502 content_fraction=0.0` → gate **exit 1** |
| frame 1 — blocco "testo" su tela nera | `distinct_levels=2 white_pixels=96 content_fraction=0.03125` → gate **exit 0** |
| frame 2 — blocco spostato | profilo identico al frame 1 (variazione presente) |
| artifact troncato (12295 B su 12288) | **exit 2**, nessun PNG scritto |
| `--frame 9` su un file da 3 frame | **exit 2**, nessun PNG scritto |

Il profilo del frame uniforme **riproduce la firma registrata del difetto**
(`uniq=1`, luma 0.498 → qui `distinct_levels=1`, luma 0.502): la stessa misura
che dichiarava rotto il percorso still dichiara adesso, sullo stesso codice, che
la forma è riconosciuta invece di essere accettata.

**(c) Aritmetica della shape reale**: `frame_bytes(1920,1080) = 8 294 400`;
`91 × 8 294 400 = 754 790 400`, esattamente la dimensione verificata dal commit
`2a5579985` che ha esposto il raw sink: il tool lavora sullo stesso artifact che
quella verifica ha prodotto.

**(d) Il difetto RIPRODOTTO su un piano reale (stesso checkout, stesso giorno).**
Con il CLI ricostruito e il piano/asset del repository non serve alcun workspace
di job per misurare la **via immagine**:

```bash
chronon3d_cli render --plan <phrase_animations_v1/01_…_render_plan.json> \
  --assets-root <assoluto>/testdata/golden --backend software \
  --frames 60 -o /tmp/p0b/still.png
```

Esito misurato, per la prima volta con numeri su questo albero:

| fatto | valore |
|---|---|
| il log del CLI emette il notice | **sì**: `[warning] pixel truth: the image output lane …` |
| frame scritti per `--frames 60` su output `.png` | **120** (`still_0000.png` … `still_0119.png`) |
| hash distinti su **tutti** i 120 frame | **1** (`6ff1f880955bcc26…`): byte-identici |
| geometria | 1920×1080 RGBA |
| profilo identico agli indici 0/30/60/119 | `distinct_levels=2 background_luma=0 white_pixels=518400 content_fraction=0.25` |

`white_pixels = 518 400 = 960 × 540` esattamente, e
`content_fraction = 0.25` esattamente: è **O2 dell'evidenza del difetto**
(«contenuto confinato a un quadrante 960×540, 1/4 esatta dell'area»),
riprodotta e per la prima volta **quantificata**, insieme a **O3** (frame
byte-identici a ogni indice). Il valore `content_fraction` è misurato dallo
stesso tool di §2(b), quindi la stessa misura che dichiara il difetto è quella
che il gate usa.

Nota di onestà sulla portata: il gate di contenuto distingue «uniforme» da «non
uniforme», **non** «contenuto giusto» da «contenuto sbagliato». Qui la varianza
non è zero (25%), quindi il gate passa: è precisamente la ragione per cui questo
difetto non è auto-segnalante, e per cui la sua chiusura è *dichiarativa* (exit
condition 2) e non un allargamento del gate.

## 4. Conseguenza sul programma qualità

P0-0b chiedeva «far comporre il testo alla via still **oppure** produrre il PNG
fedele via chunk». La seconda metà ora ha una forma eseguibile e riproducibile:
il raw sink è il campione pre-encode, `tools/raw_frame_to_png.py` lo proietta in
PNG e **rifiuta** la forma uniforme. La prima metà (riparare la via immagine) non
è stata tentata: l'exit condition del ticket ammette la dichiarazione esplicita, e
la dichiarazione è atterrata su entrambe le route.

Resta **non** dimostrato, e va detto: che un frame reale di un job overlay
contenga il testo. Quella prova richiede il workspace di un job
(`plan.json` + `prepared.json` + asset root) e un artifact raw: nessuno dei due
è in questo checkout, quindi nessuna misura pixel-level è dichiarata.

## 5. Non eseguito qui (nominato, non nascosto)

* **la suite dedicata `chronon3d_render_job_contract_tests`**: i suoi casi sono
  compilati e verdi **dentro** `chronon3d_cli_tests` (§3.0), che è la suite
  eseguita; il target dedicato non è stato linkato separatamente perché il lane
  `linux-video-test` che lo ospita ha avuto gli alberi
  (`.tmp/chronon-builds/*`, `build/chronon/`) **liberati da un processo
  concorrente durante la sessione**. Il contratto è quindi verificato, il suo
  lane GPU no.
* **Il lato raw sink della prova su un piano reale**: il lane ricostruito in
  questa sessione (`linux-fast-dev`) **non compila l'exporter video nativo**, e
  `--video-sink raw` risponde `Video output requested but the CLI video exporter
  target is disabled`. Serve un CLI da un lane video (`linux-video-release`,
  `linux-video-test`), i cui alberi sono stati liberati da un processo
  concorrente durante la sessione. La via immagine (§3d) invece gira da questo
  checkout: plan + asset root bastano, nessun workspace di job.
* **La certificazione pixel-level del testo composto**: richiede il confronto
  tra il frame pre-encode del raw sink e il frame decodificato dello stesso
  piano — cioè il punto precedente.
* **La misura sull'host Vulkan/FullGraph**: il lane ricostruito non compila il
  backend Vulkan (`no backend satisfies preference=gpu`), quindi la §3d misura
  la via immagine sul backend software. Il profilo del difetto registrato
  (`uniq=1`, luma 0.498) era su Vulkan: la §3d riproduce la **forma** (quadrante
  esatto, frame identici), non lo stesso numero.
* **Il gate GPU/WBH** invariato: `tools/verify_text_tile_gpu_canary.sh`.
* **La calibrazione VRAM su 3–5 shape**: l'harness esiste e prende la shape dal
  piano (`-mode two-runtime-pool -plan …`), quindi non richiede codice nuovo; la
  sua esecuzione richiede l'host GPU libero e un piano certificato per shape, e
  non è stata tentata in questa sessione.
