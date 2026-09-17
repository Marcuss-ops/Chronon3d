# Calibration suite VRAM — eseguibile, con la matrice dichiarata (2026-09-17)

Ambito: [`TICKET-DAEMON-JOB-LIFECYCLE`](../tickets/TICKET-DAEMON-JOB-LIFECYCLE.md)
§5, l'exit condition della domanda «si può togliere il mutex?». Host: RTX A4000
16 GB, driver 595.91.07.

## 1. Cosa mancava, esattamente

L'harness a due runtime esisteva e funzionava **per una shape**: un comando
manuale, un piano, un numero. L'exit condition però non chiede un numero: chiede
**3–5 shape, ≥3 run ciascuna, con l'attribuzione per processo**, e chiede che i
risultati alimentino `derive_vram_working_set`. Un ciclo eseguito a mano non può
dimostrare nessuna delle tre cose: nessuno può provare *dopo* che ogni shape è
stata ripetuta, né che i run appartengono alla stessa revisione.

## 2. Cosa è atterrato

**(a) Una matrice versionata** — `renderinggen.vram-calibration-suite.v1`:

```json
{
  "schema": "renderinggen.vram-calibration-suite.v1",
  "shapes": [
    {"label": "overlay-only-1080p", "plan": "../../phrase_animations_v1/01_…_render_plan.json", "repeat": 3},
    {"label": "video-source-1080p", "plan": "../../phrase_preset_videos/phrase_01_…_plan.json", "repeat": 3}
  ]
}
```

Il loader (`internal/vramprobe/suite.go`) rifiuta, con messaggio esplicito:
schema sconosciuto, zero shape, label vuota o duplicata, piano mancante, e
**`repeat < 3`** («a calibration needs at least 3»). Le regole esistono perché
ognuna è un modo di chiamare «calibrazione» qualcosa che non lo è.

**I piani relativi sono risolti rispetto alla DIRECTORY DEL MANIFEST**, non alla
CWD del processo: la matrice è un documento autosufficiente e significa le stesse
shape invocata da altrove (la stessa regola che il resto del sistema applica ai
path relativi rispetto alla propria config).

**(b) L'aggregazione** — `RunSuite` esegue la matrice (shape × repeat, **in
sequenza**: sovrapporre due shape renderebbe ogni numero funzione
dell'allocazione dell'altra), e `SummarizeSuite` produce un documento con, per
ogni shape: plan + SHA-256, canvas/frame/layer types **letti dal piano**, device
e driver, backend/encoder/preset, intervallo di campionamento, i run completati,
`daemon_attributed_mib` in **min/p50/max** (l'attribuzione per processo: il
numero su cui si calibra), `device_peak_mib`/`device_delta_mib` (registrati, con
la caveat che il contatore device-wide non è stabile), `resident_hold` e
`artifacts_identical_across_runs`.

Un run fallito è **evidenza mancante**, mai uno zero: abbassa `runs_completed` e
il documento lo dice. E il report dichiara `authorizes_mutex_removal: false`,
come il report a shape singola: la suite produce l'**input** della calibrazione,
non l'autorizzazione.

**(c) Un dry-run senza GPU** — `-dry-run` risolve la matrice e valida i piani
(inclusa la loro schema) **prima** di spendere tempo GPU:

```bash
cd RenderingGen/renderinggen
go run ./cmd/vram-probe -mode calibration-suite \
  -shape-manifest ../multilingual_overlays_v1/evidence/vram_calibration_suite_2026-09-17.json \
  -out /tmp/suite.json -dry-run
```
```
shape overlay-only-1080p       1920x1080 24 fps 120 frames layers=[color text] repeat=3 plan_sha256=b14ec958a2e9
shape video-source-1080p       1920x1080 24 fps 125 frames layers=[text video]   repeat=3 plan_sha256=7f669df143ef
dry-run: 2 shape(s), 6 run(s) to execute; nothing was rendered
```

Lo SHA della prima shape (`b14ec958a2e9…`) è **lo stesso** registrato
nell'evidenza del probe a due runtime del 2026-09-16 (§3): la matrice esegue
esattamente il piano già misurato, non «un piano simile».

## 3. Inventario delle shape nel checkout (misurato)

Scansione di ogni `*.json` con schema `chronon.render-plan.v2`:

| fatto | valore |
|---|---|
| piani concreti | **113** |
| canvas | **tutti 1920×1080** (nessuna shape 4K) |
| layer set `(color,text)` | 67 — la shape overlay-only |
| `(text,video)` | 10 (5 distinti, duplicati sotto `renderinggen/`) — la shape con sorgente video |
| `(color,image)` / `(text)` / `(color,image,text)` | 17 / 13 / 6 |

Conseguenza per l'exit condition: **2 shape su 3–5 sono eseguibili oggi**, e le
altre sono bloccate da cause **nominabili**:

* **4K**: non esiste alcun piano 4K nel repository. La shape richiede che un
  piano venga autorato (o che un canvas 3840×2160 venga aggiunto al corpus), non
  un trucco dell'harness;
* **DirectYUV**: non è una proprietà del piano. È la decisione di lane del
  motore per un piano a sorgente singola, quindi **non si dichiara in una
  label**: va certificata dalla telemetria del run (`execution_path` nel
  summary). Oggi l'harness registra le layer types come fatti e
  deliberatamente **non** indovina la lane; estrarre `execution_path` dal run è
  il passo che rende la shape DirectYUV dichiarabile senza mentire.

## 4. Cosa non è stato eseguito qui

I 6 run GPU (`-dry-run` assente) **non** sono stati lanciati: servono ~3 min per
run (render + hold) su una GPU condivisa con un servizio vivo e con un altro
processo che lavora sullo stesso checkout. Il comando è pronto:

```bash
# prerequisiti: un chronon3d_cli costruito e l'asset root ASSOLUTO
CHRONON_BIN=$PWD/Chronon3d/.tmp/chronon-builds/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli
python3 cmd/vram-probe -mode calibration-suite \
  -shape-manifest ../multilingual_overlays_v1/evidence/vram_calibration_suite_2026-09-17.json \
  -chronon-bin "$CHRONON_BIN" -assets-root "$PWD/testdata/golden" \
  -output-dir /tmp/vram-suite -out ../multilingual_overlays_v1/evidence/vram_calibration_suite_2026-09-17.report.json
```

Nota di ambiente: il lane `linux-fast-dev` (l'unico albero ricostruito in questa
sessione) **non** compila l'exporter video nativo, quindi il CLI va preso da un
lane video (`linux-video-release`, i cui alberi sono stati liberati da un
processo concorrente) — è lo stesso prerrequisito della verità pre-encode.

## 5. Gate

* `go build ./...`, `go vet ./internal/vramprobe/... ./cmd/vram-probe/...` → PASS;
* `go test -count=1 ./internal/vramprobe/` → PASS (nuovi: validazione della
  matrice incluse le regole sul floor, statistica min/p50/max senza mutare
  l'input, aggregazione con run mancanti, identità degli artifact, determinismo
  del documento, risoluzione dei piani rispetto al manifest, dry-run che rifiuta
  un piano semantico);
* `gofmt -l` pulito.
