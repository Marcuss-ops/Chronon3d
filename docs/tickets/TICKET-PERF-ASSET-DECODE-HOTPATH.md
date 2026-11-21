# TICKET-PERF-ASSET-DECODE-HOTPATH — il decode asset è il costo dominante del job overlay

**Stato:** PARTIAL — aggiornato 2026-11-21. A/B seriale/parallelo same-plan/same-window PASS su quattro caption r2/r3 e sul caso `entity-chain-bg` r21: tre processi freschi per variante, MP4 byte-identici entro piano. `prepare_asset_decode_ms` p50 cala del 72,7–75,8% sui caption; sul r21 2.549,861 → 790,687 ms (−69,0%). Il wall p50 r21 cala del 37,1%. Per abilitare il replay r21 è stato corretto `source_node_native_image.cpp`: un'immagine completamente clipped conserva la surface trasparente valida invece di restituire un handle nullo. Il vecchio confronto col finale storico resta non attribuibile al decode (`libx264` contro NVENC driver-default). Il prefetch resta disabilitato perché gli A/B concorrenti peggiorano il throughput. Restano replay worker e wall p50 sul worker di produzione. Report: [`prepare_overlap_prefetch_20261121`](../../../ops/jobs/prepare_overlap_prefetch_20261121/REPORT.md) e [`report asset decode r2/r3`](../../../ops/jobs/asset_decode_corpus_replay_20261121/REPORT.md).
**Area:** `src/runtime/render_preparation_assets_detail.hpp`, `src/backends/assets/image_cache.cpp`, `apps/chronon3d_cli/daemon/daemon_service_ipc_dispatch.cpp`.
**Owner:** maintainer della pipeline asset / `ImageCache`.
**Dipende da:** niente per la fase A/B. La fase C (prefetch) dipende dalla policy di concorrenza del daemon, ma non la blocca.
**Non sostituisce:** [`TICKET-PERF-100X-VERIFIED-PLAN`](TICKET-PERF-100X-VERIFIED-PLAN.md), [`TICKET-PERF-FRAME-PROOF-CHEAP`](TICKET-PERF-FRAME-PROOF-CHEAP.md), [`TICKET-PERF-GLOW-HALF-RES`](TICKET-PERF-GLOW-HALF-RES.md). Quelli parlano di frame, timeline e GPU; questo del costo **prima** del primo frame.

---

## 0. Regola di evidenza

| Tag | Significato |
|---|---|
| `[CODICE]` | Verificato nel checkout, path e funzione citati. |
| `[MISURA]` | Letto da un artefatto nominato. |
| `[INFERITO]` | Conseguenza logica di codice + dati, non ancora osservata da un marker runtime. |
| `[IPOTESI]` | Stima o idea; non è un'accettazione. |
| `[NOT RUN]` | Non eseguito in questa sessione. |

---

## 1. Evidenza misurata

`asset_decode_ms` è il tempo speso in `render_preparation_assets_detail.hpp` dentro il loop delle immagini: comprende `ImageCache::get_or_load`, la conversione a RGBA32F, l'eventuale SHA-256 del payload e la promozione in `GpuAssetCache`. Non è solo I/O.

Job reali del 2026-11-19, due item per job, RTX A4000:

| Job / item | `job_wall_ms` | `prepare_ms` | `asset_decode_ms` | `render_loop_wall_ms` | `validation_ms` |
|---|---:|---:|---:|---:|---:|
| entity-rect-caption-oswald r2 / 0 | 5 951 | 3 273 | **3 018** | 2 642 | 68 |
| entity-rect-caption-oswald r2 / 1 | 5 805 | 3 387 | **3 148** | 2 373 | 78 |
| entity-rect-caption-abril r2 / 0 | 7 861 | 3 538 | **3 341** | 4 271 | 73 |
| entity-rect-caption-abril r2 / 1 | 8 213 | 3 903 | **3 702** | 4 259 | 78 |
| entity-rect-caption-oswald r3 / 0 | 4 988 | 2 099 | **1 907** | 2 839 | 62 |
| entity-rect-caption-oswald r3 / 1 | 4 666 | 2 182 | **1 976** | 2 444 | 88 |
| entity-rect-caption-abril r3 / 0 | 5 074 | 2 172 | **1 933** | 2 853 | 68 |
| entity-rect-caption-abril r3 / 1 | 4 855 | 2 310 | **2 062** | 2 498 | 61 |
| entity-chain-bg-oswald r21 / 0 | 65 600 | 6 244 | **1 069** | 58 252 | 46 |
| entity-chain-bg-oswald r21 / 1 | 32 227 | 2 445 | **1 462** | 28 544 | 49 |

Fonti: `ops/jobs/entity_rect_caption_20261119/attempts-r2/*-full.json`, `ops/jobs/entity_rect_caption_20261119/*r3-full.json`, `ops/jobs/entity_chain_background_20261119/entity-chain-bg-oswald-20261119-r21-full.json`, blocco `job.result.result.overlay_render.items[*].artifact.chronon_telemetry.job`.

`[MISURA]` Nei job caption l'asset decode vale il **38–54% del wall** e l'**89–95% di `prepare_ms`**. Nello stesso job il plan non è il costo: `plan_ms` è 1–2 ms e `plan_compile_ms` è `null` sul percorso `compiled_program`. La receipt vale 46–88 ms. Il render loop, che è il lavoro GPU reale, vale 2,4–4,3 s.

`[MISURA]` Nei due item di `entity-rect-caption-oswald` la terza immagine ha lo stesso SHA (`b505fa81…`).
`[INFERITO]` Il secondo item paga comunque 3,1 s di `asset_decode_ms`: non c'è evidenza che quell'asset sia stato riusato dalla cache. Può essere un miss per chiave (`max_dimension`/opzioni diverse) o un'eviction LRU; la fase A deve distinguere i due casi, non assumerli.

---

## 2. Evidenza da codice

| Fatto | Evidenza |
|---|---|
| Il prepare decodifica ogni immagine del piano dentro il job | `[CODICE]` `src/runtime/render_preparation_assets_detail.hpp:132-198`: `image_cache().get_or_load(ref.path, decode_options)` per ogni `image_refs_view`, poi `gpu_asset_cache().acquire(...)`. |
| La decodifica avviene a piena risoluzione e il downscale è **dopo** | `[CODICE]` `src/backends/assets/image_cache.cpp:177-221`: `backend.load_image(...)` produce il buffer completo, poi `downscale_for_runtime(*buffer, options.max_dimension)`. `max_dimension` non riduce il costo del decoder, riduce solo la copia che resta in cache. |
| La chiave include le opzioni di decode esatte | `[CODICE]` `include/chronon3d/backends/assets/image_cache.hpp:157-175` e `src/backends/assets/image_cache.cpp:426-451`. Lo stesso asset richiesto con un `max_dimension` diverso è un miss. |
| La cache trattiene due rappresentazioni | `[CODICE]` `include/chronon3d/backends/assets/image_cache.hpp:30-62`: `BLImage` (4 B/px) + `Framebuffer` float (16 B/px), più `gpu_rgba` quando le righe non sono dense. Capacità default 1 GiB (`image_cache.hpp:88`). |
| `PREFETCH_ASSET` non scalda il decode | `[CODICE]` `apps/chronon3d_cli/daemon/daemon_service_ipc_dispatch.cpp:15-27`: chiama solo `m_engine->assets().import_by_extension(path)`, non `ImageCache::get_or_load`. Non può nascondere i 3 s. |
| Il decode è dentro il lock di esecuzione GPU | `[CODICE]` `apps/chronon3d_cli/daemon/daemon_service_ipc_render_job.cpp:136-150` crea l'`RenderJobAdmissionContext` prima del dispatcher; `admission.reset()` arriva solo a `:245`, dopo render e mux. Vedi anche il ticket [`TICKET-PERF-PREPARE-RENDER-OVERLAP`](TICKET-PERF-PREPARE-RENDER-OVERLAP.md). |

---

## 3. Perché è un freno

`[INFERITO]` Se il decoder deve produrre un buffer 4K/8K anche quando il display box è 800×450, il lavoro è sprecato: il costo di decode scala con i pixel sorgente, non con i pixel mostrati. Il downscale post-decode non recupera quel lavoro.

`[INFERITO]` La cache è utile solo se la stessa identità (path canonico + stesse `ImageDecodeOptions`) ritorna. Se il scene builder cambia `max_dimension` di pochi pixel tra item, la cache non può colpire; se il peso LRU supera 1 GiB, l'asset condiviso viene espulso prima del riuso.

`[IPOTESI]` Ridurre il decode al bound di display e rendere il prefetch una vera decodifica calda può tagliare 1,5–3 s/item sul corpus caption. La cifra non è un'impegno: va misurata con il gate sotto.

---

## 4. Piano di chiusura (split in tre fasi)

### Fase A — Attribuzione, nessun cambio di comportamento

- Esporre nel sidecar, per ogni immagine: `image_decode_ms`, `image_convert_ms`, `image_sha256_ms`, `image_cache_hits`, `image_cache_misses`, `image_decode_count`, dimensioni sorgente e dimensioni dopo il bound, peso LRU.
- Log una riga per immagine con path canonico, `max_dimension`, `color_space`, cache hit/miss e byte.
- Misurare un corpus di 10 item con asset ripetuti e uno con asset tutti nuovi.

**Gate A:** i campi sono presenti in 3 run; `asset_decode_ms` si riconcilia con la somma dei tempi per immagine; il numero di decode uguaglia il numero di miss.

### Fase B — Decodifica al bound di display

- Decodificare direttamente alla risoluzione utile (scale nel decoder, es. JPEG scale/`stb` o backend che accetta un target) oppure applicare il bound prima di allocare il buffer RGBA.
- Se il decoder non supporta lo scaling, mantenere il percorso attuale come fallback dichiarato, con contatore.
- Normalizzare la chiave: il bound di display entra nella cache in modo stabile (es. bucket quantizzato), così lo stesso asset usato a 1 920 e 1 800 non viene decodificato due volte. Non cambiare la semantica del bound senza gate visivo.

**Gate B:** su r2/r3, `asset_decode_ms` p50 ≤ 50% del valore attuale (≤1,5 s/item) **oppure** motivazione misurata del perché il decoder non può scendere. Output identico: MP4 byte-identico o hash frame identico al controllo su 3 run. Nessun decode doppio della stessa identità nello stesso job.

### Fase C — Prefetch caldo e sovrapposizione

- Estendere `PREFETCH_ASSET` (o un'opzione esplicita dello stesso comando) per decodificare e promuovere l'immagine in cache, non solo importarla.
- Il producer può prefetcharre gli asset dell'item N+1 mentre l'item N è in render. Il prefetch non deve tenere il lock di esecuzione GPU e non deve creare un secondo renderer.
- Rispettare il limite di memoria: il prefetch non può far crescere la VRAM oltre il budget e non può espellere l'asset che il job corrente sta usando.

**Gate C:** su un batch di 10 item, `asset_decode_ms` degli item prefetchati è ≤10% del valore attuale; nessun deadlock/race con `LanesPerDaemon=2`; VRAM entro budget; output identico. Se il prefetch non porta guadagno, chiudere la fase C con la misura e lasciare A/B.

**Split:** A è indipendente. B e C possono essere due PR separate. Se B non passa il gate, C resta valida solo se il decode è ancora il collo di bottiglia.

---

## 5. Gate complessivo

- Corpus: i 4 job caption r2/r3 + `entity-chain-bg` r21 + un corpus sintetico di 10 item con asset ripetuti.
- Metrica primaria: `asset_decode_ms` p50 e `job_wall_ms` p50, non una percentuale inventata.
- Parità: MP4 byte-identico (SHA-256) o hash frame identico al controllo; nessun drift visivo.
- Memoria: `ImageCache` e `GpuAssetCache` entro budget; nessuna crescita non limitata; hit/miss contati.
- Fallback: decoder senza scaling, immagine corrotta, asset fuori root, cache piena.
- `[PARTIAL]` implementazione e A/B sintetico completati; gate sul corpus
  reale r2/r3 e equivalenza end-to-end del worker ancora `NOT RUN`.

---

## 6. Aggiornamento 2026-11-20

- `render_preparation_assets_detail.hpp` carica in parallelo le immagini
  indipendenti tramite il `ImageCache` canonico; l'aggiornamento del piano e
  la promozione GPU restano seriali. Non cambia il contenuto decodificato.
- Il sidecar espone ora `job.image.cache_hits` e `job.image.cache_misses` per
  distinguere i cache hit dalle nuove decodifiche.
- Evidenza pre-fix nei due job r3 (quattro item): `asset_decode_ms` 1.907–2.062
  ms/item, `image.decode_ms` 772–868 ms, `image.convert_ms` 502–573 ms,
  `image.decode_count=3`. `asset_decode_ms` misura l'intera barriera immagini,
  non il solo decoder. I sidecar storici non riportano i contatori cache.
- Build Release `cmake --build build/chronon/linux-video-release -j10`: PASS.
  Build mirata test core/CLI con `-j10`: PASS. CTest `chronon3d_core_tests` e
  `chronon3d_cli_tests`: 2/2 PASS; la suite CLI verifica i campi cache del
  sidecar.
- A/B post-fix sul fixture sintetico software, 3 run per variante: prepare p50
  4.615 s seriale → 1.574 s parallelo/deduplicato; job wall p50 5.184 s →
  2.136 s. Tutti i sei MP4 hanno lo stesso SHA-256; decode count resta 3 per
  run. Evidenza e fixture in
  [`asset_decode_parallel_20261120`](../../../ops/jobs/asset_decode_parallel_20261120/REPORT.md).
- `[NOT RUN]` confronto sui payload reali r2/r3 dopo il fix. I tre daemon GPU
  live sono rimasti intatti; il confronto isolato usa backend software e non
  dimostra ancora il gate produttivo del ticket.
- `[MISURA]` A/B aggiuntivo del downscale seriale vs parallelo per righe,
  tre run per variante sullo stesso fixture 4K: `asset_prepare_ms` p50
  1.628,71 → 1.403,09 ms; `job_wall_ms` p50 2.672,91 → 2.682,91 ms.
  Tutti i sei MP4 sono byte-identici. Il guadagno resta locale alla fase di
  prepare e non dimostra un miglioramento wall end-to-end. Sidecar e report:
  [`asset_resize_parallel_20261120`](../../../ops/jobs/asset_resize_parallel_20261120/REPORT.md).
- `[MISURA — SOTTOGATE PREPARE PASS, PARITÀ END-TO-END APERTA]` Replay locale
  2026-11-21 dei quattro piani semantici originali caption r2/r3, ricompilati
  dal compilatore RenderingGen e renderizzati col binario Release Vulkan. I
  cinque JPEG unici (sei riferimenti per piano, con una ripetizione) sono stati
  verificati tramite SHA-256. Per ogni piano: 3 processi freschi, un frame
  rappresentativo, 5 `ImageCache` miss e output MP4 a un frame byte-identico
  tra i 3 run. `prepare_asset_decode_ms` p50 dell'intero piano a due item:
  Oswald r2 854,57 ms; Abril r2 842,35 ms; Oswald r3 840,40 ms; Abril r3
  812,89 ms. Sommando il baseline storico `asset_decode_ms` dei due item, la
  riduzione osservata è 78,4–88,0%. Il p50 di `prepare_image_decode_ms` è
  1.521–1.613 ms per piano, cioè 761–806 ms per item se ripartito sui due item.
  Sidecar, plan, log, asset e MP4 sono in
  [`report asset decode r2/r3`](../../../ops/jobs/asset_decode_corpus_replay_20261121/REPORT.md).
  Il render limitato al frame 48 verifica il prepare completo e la ripetibilità
  di quel frame. Tre replay full-video Vulkan/NVENC p4 con encoder dichiarato
  uguale al controllo sono byte-identici tra loro; ciascuno produce 366/366
  frame hash diversi dal riferimento storico, SSIM medio 0,985817. Quindi il
  sottogate numerico passa ma il gate B di parità resta FAIL; mancano inoltre
  replay del worker end-to-end, decode al bound e wall p50 del worker. Dati:
  [`full_video_p4_three_run_compare.json`](../../../ops/jobs/asset_decode_corpus_replay_20261121/full_video_p4_three_run_compare.json).
- `[CODICE]` `downscale_for_runtime` esegue righe indipendenti in TBB; il test
  cache verifica output pixel-identico su due decode separati.
- `[CODICE]` il daemon può anche pre-decodificare le immagini di N+1 prima
  dell'admission GPU usando il renderer/cache CPU già caldi, deduplicando per
  `ImageAssetKey` e richiedendo identità delle chiavi tra resolver del piano e
  resolver runtime. Il tempo viene riportato nelle fasi prepare. Root diverse
  o assenza di renderer caldo fanno saltare il prefetch in sicurezza.
- `[MISURA]` il test IPC del ticket prepare usa un PNG reale nella stessa root
  del renderer e dimostra che il callback di prefetch per N+1 completa mentre
  il dispatcher di N resta bloccato. Questo verifica la schedulazione/cache,
  non il guadagno sul corpus produttivo.
- `[MISURA]` l'A/B concurrent producer 20 item × 3 run del 2026-11-21 non
  supera il gate C: con prefetch il throughput è inferiore in software e
  Vulkan render. La lane Vulkan ha `encoder_backend=software`; i dati non
  certificano encoder NVENC. L'immagine resta opt-in finché la riduzione
  decode-fullres o un'altra strategia non dimostra un guadagno netto.
- `[CHIUSA — NON ABILITARE]` Fase C (prefetch daemon): la misura negativa
  soddisfa la decisione prevista dal gate C di non adottare il prefetch quando
  non porta un guadagno. Non resta un lavoro di rollout per questa variante;
  una nuova strategia di overlap richiede un A/B nuovo. Il ticket complessivo
  resta aperto per Fasi A/B sul corpus r2/r3 e decode al bound.

### Follow-up same-plan r21 — 2026-11-21

- `[MISURA]` A/B seriale/parallelo completato sul caso `entity-chain-bg` r21:
  tre processi freschi per variante, Vulkan/NVENC p4, stessa finestra 48–71.
  Tutti i sei output hanno SHA identico `d193a3c3…8cb2d3`. `prepare_asset_decode_ms`
  p50: 2.549,861 → 790,687 ms (−69,0%); wall p50: 3.694,946 → 2.325,115 ms
  (−37,1%). Sidecar, MP4 e log sono in
  [`A/B r21 same-plan`](../../../ops/jobs/asset_decode_corpus_replay_20261121/asset_prepare_ab/chain-r21/).
- `[CODICE + MISURA]` La prova ha scoperto che un'immagine completamente
  clipped rilasciava la surface trasparente ma restituiva successo; il composite
  riceveva handle nullo. `source_node_native_image.cpp` ora conserva la surface
  inizializzata. Prima del fix anche il controllo seriale falliva al frame 56;
  dopo il fix tutte le run seriali e parallele completano 24/24 frame.
- `[PASS — GATE B, CORPUS LOCALE]` I quattro piani caption r2/r3 e r21
  completano il before/after seriale/parallelo con output byte-identico per tre
  processi per variante; il p50 prepare scende oltre il 50% su ogni piano. Il
  fixture sintetico con asset ripetuti è già PASS. Il ticket resta PARTIAL per
  il replay worker end-to-end e per i gate complessivi di memoria e fallback.

## 7. Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | maintainer asset pipeline. |
| **Reason** | Il percorso `downscale_for_runtime` post-decode resta come fallback per i decoder che non accettano un target. |
| **Exit condition** | Tutti i backend usati dal percorso overlay decodificano al bound; `downscale_for_runtime` non è più raggiunto (`asset_decode_fullres_fallback=0` su 3 run). |
| **Equivalence test** | Hash dell'immagine decodificata e MP4 byte-identico prima/dopo. |
| **Removal scope** | Il ramo di downscale post-decode e il suo contatore, non l'intera `ImageCache`. |
| **Status** | `ACTIVE` finché il fallback esiste. |

Nessuna nuova cache: si riusa `ImageCache`. Qualsiasi cache di decoded asset separata richiede ADR (regola `AGENTS.md`).
