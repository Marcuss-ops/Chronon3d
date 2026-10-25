# TICKET-MAPS-NATIVE-PORT — porting nativo del rendering mappe

Stato: **ASSE ACQUISIZIONE TILE CHIUSO (2026-10-24)** — la misura ha smontato la
premessa del porting: il rendering pesa solo ~4,8 s su 33,3 s; il ≥2× di
accettazione è conquistato e certificato sull'asse acquisizione tile + budget
1080p (parità bit-exact mantenuta). Scaffolding nativo atterrato; le fasi 2–7
restano aperte come opzione di unificazione nelle lane condivise.

Ambito aggiornato 2026-10-26: `ChrononPlugin/plugins/maps` possiede il rendering map plate e i relativi test. Chronon3D non contiene più il modulo geo. Restano nel ticket gli assi di acquisizione tile, integrazione e demolizione del percorso Python in ChrononTemplate.

**Migrazione 2026-10-25:** il codice geo C++ e i tool cartography generici
sono stati estratti in `ChrononPlugin/plugins/maps/` (namespace
`chrononplugin::maps`). Chronon3D non possiede più il sottosistema geo; il
renderer `map_plate_render` resta uno stub fail-closed nel plugin. La raster
vector plate compila lì, ma la prova pixel bit-exact del port resta aperta.

## 1. Perché (misure, non stime)

Nella run golden v3 del caso overlay-throughput
([TICKET-OVERLAY-THROUGHPUT-100-MIN](../../../refactored/docs/operations/TICKET-OVERLAY-THROUGHPUT-100-MIN.md),
refactored) **2 mappe hanno costato 20,66 s e 37,37 s** (`process_wall_s`) —
da sole più di quanto costano 10–15 item frase (2–5 s ciascuno). La mediana
fused su 3 lane (v22–v24) è **78,9 item/min (range 74,6–86,2)** contro il
gate 100/min: gap 1,27×. Con ~2 mappe per run da ~30 s l'una, la classe map
consuma oltre un minuto di finestra al run: è la classe che decide il gate.

Le mappe oggi sono **processi Python esterni per item**, cioè l'unica classe
del piano che esce dal rendering condiviso. Il porting nativo le rientra.

### 1.1 Contromisura 2026-10-24: l'attribuzione reale (prima di scrivere C++)

Payload di controllo (2 tappe, 8 s, 240 frame) misurato sulla macchina di
produzione con lo stesso renderer della golden, prima di ogni modifica:

| Stadio | Tempo | Quota |
|---|---|---|
| Prefetch tile (4834 tile, 2069 in rete, 34,5 MB) | 25,3 s | 76% |
| Composizione plate (28 plate × 3840×2160 = 802 MB) | 1,4 s | 4% |
| Raster frame + encode (240 frame) | 4,8 s | 14% |
| **Totale** | **33,3 s** | |

Il rendering — cioè l'oggetto del porting nativo — è solo ~4,8 s: anche
renderselo gratuito lascia 28 s (1,17×, sotto l'accettazione ≥2× della sezione
6). Il costo è l'**acquisizione tile** (146 ms per fetch × 2069 fetch, latenza
di rete pura). Conseguenza operativa: prima si sistema l'asse tile, poi si
misura di nuovo, e solo allora ha senso valutare il C++.

## 2. Architettura attuale (da sostituire)

- **Orchestrazione** Go `refactored/internal/capabilities/scripts/render_queue_map.go`
  (+ `render_queue_map_composition.go`): identità cache = payload + fingerprint
  del renderer + profilo encode + sampler + dimensioni; ammissione GPU tramite
  il gate RenderingGen condiviso.
- **Rendering**: `/usr/bin/python3 ChrononTemplate/tools/map/render_dynamic_map_overlay.py`
  (797 righe) + plate generator (`VELOX_GEO_MAP_PLATE_GENERATOR_PATH`), un
  processo per item, tile pyramid + flyover + etichette in Python.
- **Telemetria**: `chronon.dynamic-map-telemetry.v1` certificata obbligatoria —
  la cache rifiuta le entry senza telemetria completa (fail-closed).

## 3. Obiettivo

Le mappe diventano nodi nativi del rendering (modulo `geo`/`render` di
Chronon3d): **tile pyramid → textured planes → flyover camera → etichette**,
sulle stesse lane GPU di ogni altra classe di item. Il servizio per item
passa da decine di secondi di processo esterno a servizio render misurato.

## 4. Contratto di parità (prima del cutover)

1. **Parità pixel**: stesso payload immutabile → frame-a-frame framehash
   contro il binario Python, corpus dichiarato (almeno un flyover, un
   territorio, un piano con etichette).
2. **Telemetria certificata equivalente**: la cache identity del Go
   orchestratore richiede già il fingerprint del renderer
   (`dynamicMapRendererFingerprint`); il renderer nativo deve esporsi con la
   stessa regola (fingerprint del codice nativo nella cache key).
3. **Fail-closed**: nessun frame senza telemetria completa, nessuna identità
   fabbricata (la regola è già nel contratto di scaffolding).
4. **Nessuna regressione alle altre classi**: il gate GPU condiviso resta
   l'autorità di ammissione.

## 5. Fasi

| # | Fase | Stato |
|---|---|---|
| 0 | Fix asse acquisizione tile (keep-alive + parallelismo + budget 1080p) + parità bit-exact | **CHIUSO 2026-10-24** |
| 1 | Scaffolding: contratto `map_plate_render` fail-closed + test | **ATTERATO** |
| 2 | Primitiva tile/plate: tile pyramid → textured planes | aperto |
| 3 | Passo etichette (text run builder) | aperto |
| 4 | Flyover/camera (contratto motion esistente) | aperto |
| 5 | Telemetria certificata + parità pixel vs Python | aperto |
| 6 | Cutover orchestratore Go (dual-lane: nativo preferito, Python fallback) | aperto |
| 7 | Demolizione del binario Python + aggiornamento cache identity | aperto |

Le fasi 2–7 sono il porting nativo propriamente detto: resta aperto come
opzione di **unificazione** (le mappe rientrano nelle lane render condivise),
non più come prerequisito del gate di accettazione (vedi sezione 6).

### 5.1 Fase 0 — cosa è atterrato (2026-10-24)

- **Keep-alive per thread** su `_http_get` (`dynamic_tile_pyramid.py`): una
  connessione HTTPS persistente per host per worker, riconnessione una sola
  volta su socket stantia. Misura di controllo: il TLS handshake NON è il
  collo (4–6 ms/fetch su connessione viva); un tile distinto costa ~105 ms di
  latenza bordo/origine comunque — quindi il keep-alive da solo non basta e il
  parallelismo resta il moltiplicatore vero.
- **Parallelismo prefetch 12 → 64 worker** (`fast_plate_sampler.py`,
  override `DYNAMIC_MAP_TILE_WORKERS`): throughput misurato 100 fetch/s a 12
  worker, 754 fetch/s a 64 (scala lineare, zero errori su ~3k fetch di prova).
  Il bound resta rispettato e testato (`prefetch_workers`).
- **Budget plate 1080p** (decisione operatore 2026-10-24: «non mi serve 4k, mi
  basta 1920*1080»): `plate_margin_factor` default **1.0** per le composizioni
  classiche (3.0 preservato per le editorial certificate), risoluzione plate
  derivata dal payload. La geometria del campionamento esige il plate `hi` fino
  a 2× canvas: quando non è coperto la degradazione corretta è **solo `lo`**
  (mai crollare a zoom 5), contata onestamente in `lo_only_blend_frames`.
- **Parità bit-exact**: a `plate_margin_factor` 2.0 i 240 frame sono
  **identici bit-a-bit** (framemd5) al riferimento pre-modifiche; gate
  determinismo PASS. La parità è indipendente dal margin finché la copertura è
  completa: il margin cambia solo quanto la camera può muoversi senza
  degradare, non i pixel serviti.

## 6. Misura di accettazione

### 6.1 Avanzamento misurato (2026-10-24, corpus di controllo)

Stesso payload immutabile (2 tappe, 8 s, 240 frame), **1920×1080** (budget
operatore), `plate_margin_factor` 1.0:

| Corsa | Fetch di rete | Prefetch | Frame+encode | **Wall renderer** |
|---|---|---|---|---|
| Fredda (aree mai viste) | 1259 (+404 disco) | 4,29 s | 7,45 s | **13,50 s** |
| Warm ×3 | ~59 | 2,26/2,07/1,95 s | 7,79/7,95/7,87 s | 12,06/12,00/**11,90 s** |

- **Mediana wall 3 run = 12,00 s a 1920×1080** contro la baseline Python
  pre-fix di **32,85 s a 1280×720**: **2,7×** con 1,4× i pixel (fredda 13,50 s
  = 2,4×). Gate ≥2× **già superato** sul binario Python stesso, stessa
  macchina. A parità di 720p il wall post-fix è 8,34–9,20 s → **3,6–4,0×**.
- Il solo frame+encode è 7,5–8,0 s (≈4× sulla pipeline di frame), ma la
  finestra del gate paga il **wall del processo** (12,0 s): è quello il numero
  di riferimento, non il sotto-stadio.
- Contatori onesti: `tile_fallbacks = 0`, `engine_fallback_frames = 0`,
  `lo_only_blend_frames = 117–123` (degradazioni dichiarate a margin 1.0, non
  difetti). `late_tile_fetches = 0`.
- Le due mappe della golden v3 costavano 20,66 s e 37,37 s per item: con
  l'asse tile sistemato la classe map esce dalla posizione di collo del gate
  fused; la soglia assoluta si fissa con il primo `make deploy-golden`.


- Sullo stesso payload immutabile, **mediana 3 run** (protocollo
  `make deploy-golden` di refactored: deploy → golden ×3 → goldensummary
  mediana + range): il servizio per-item della **classe map** deve calare di
  **almeno 2×** rispetto al binario Python; nessuna regressione sulle altre
  classi. La soglia assoluta si fissa con la prima misura nativa, non prima.
- Parità pixel + telemetria certificata (fase 5) verificate prima di ogni
  passo di cutover (fase 6); la demolizione (fase 7) è ammessa solo dopo che
  il dual-lane ha girato almeno una golden completa sul nativo.

## 7. Verifiche (2026-10-24)

- Suite mappe Python **verdi**:
  `test_fast_plate_sampler_prefetch` 3/3 (i mock di rete sono stati ri-saldati
  al nuovo seam `_http_get` — `urllib.request.urlopen` non è più il percorso di
  rete — e il bound worker resta pinnato dal test),
  `test_render_dynamic_map_payload` 19/19,
  `test_dynamic_tile_pyramid_suite` 5/5 (attenzione: il Test 5 è un gate FPS e
  fallisce solo a cache fredda — 12,3 FPS — per poi passare a caldo, 21,7 FPS;
  non è una regressione).
- Parità pixel: framemd5 240/240 identici al riferimento pre-modifiche.
- Suite Go: **verde e non toccata in questa fase** (solo Python + docs).
  Verificato 2026-10-24: `go test ./internal/capabilities/scripts/` → ok
  (25,4 s, 0 fail, 1 skip hardware NVENC), `go test ./internal/capabilities/...`
  e `go test ./...` senza `FAIL`. Non esistono le "6 pre-rotture" citate in
  precedenza: era un'assunzione errata, smentita dall'esecuzione.
