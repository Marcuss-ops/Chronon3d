# Chronon3D — Next Steps Reali

> **Snapshot:** current `main` — 24 giugno 2026. Stato confluito dai tre
> blocchi recenti:
>
> - **Blocco 1** (baseline registrata con articolo dedicato, già su main):
>   `main@25c6b5cd` — render-aggregator wiring verificato.
> - **Blocco 2** (eseguito sul main corrente): TXT-00 **CHIUSO** su
>   `main@f90174cc` — build/link/test rc=0, `VRTextPresetVisual` 18/18,
>   263/263 assertions, entrambe le `TextE2E` verdi.
> - **Blocco 3** (eseguito a valle): TXT-01 parzialmente **CHIUSO** su
>   `main@5b83a123` — hash gate ingaggiato (128 sentinel catturati come
>   `constexpr`) + failure-path provata (`+1` mutation osservata a
>   doctest rc=8; revert a rc=0). Restano aperti: PNG dump,
>   light/dark BG, testo corto/lungo, determinismo seriale/parallelo
>   (i restanti 4 DoD di TXT-01).
>
> Baseline Blocco 1 corrente: [`baselines/main-25c6b5cd-render-aggregator-deps-fixed.md`](baselines/main-25c6b5cd-render-aggregator-deps-fixed.md).
> Baseline Blocco 2 (TXT-00 chiuso): [`baselines/main-345e5f2e-txt-00-closed.md`](baselines/main-345e5f2e-txt-00-closed.md).
> Baseline Blocco 3 (TXT-01 hash-gate): [`baselines/main-5b83a123-txt-01-blocco-3-failure-path-proven.md`](baselines/main-5b83a123-txt-01-blocco-3-failure-path-proven.md).
> Baseline storica TXT-00 build-only: [`baselines/main-ccabb574-txt-00-build-green.md`](baselines/main-ccabb574-txt-00-build-green.md) (superata da Blocco 2).
> Stato prodotto: [`CURRENT_READINESS.md`](CURRENT_READINESS.md).
> Milestone: [`ROADMAP.md`](ROADMAP.md).

Questo documento descrive l'ordine operativo immediato. Agent 1 (renderer/backend)
e Agent 2 (CMake/SDK/baseline) sono entrambi **completati** e mergiati su `main`.
Il lavoro procede sequenziale, direttamente su `main`, un task alla volta.

## Stato osservato della baseline

La baseline registrata su `main@ccabb574` (Agent 2 merge) ha prodotto:

| Controllo | Esito |
|---|---|
| `cmake --preset linux-ci` configure | ✅ PASS |
| Build `chronon3d_text_preset_visual_tests` | ✅ PASS (rc=0) |
| ctest `VRTextPresetVisual` | ✅ PASS (rc=0) — 18/18 test cases, 263/263 assertions |

Il linker strutturale (~30 simboli irrisolti) è **risolto** dal fix CMake registry
dell'Agent 2. Il FontEngine è ora inizializzato nel runner di test, quindi
sia le 16 `VRTextPreset/*` (128 sentinels) sia le due `TextE2E` renderizzano
ink visibile e i gate `expected_visible`/`ink_pixels` tornano tutti rc=0.

Il verdetto: **TXT-00 CHIUSO** — build, link e run tutti rc=0 sullo stesso
commit. Nessuna classificazione intermedia inventata: la matrice
`expected_visible` è confermata dal log (14 preset hanno ink=0 a F000 per
design; BlurIn F020 e MaskedLineReveal F020 sono sub-threshold documentati;
tracking_close + minimal_white sono visibili a tutti i timestamp).


### 1. TICKET-039 — RISOLTO ✅

Il fix CMake registry dell'Agent 2 ha risolto il problema strutturale di link.
`SoftwareRenderer::settings()` è stato migrato a `render_settings()` nel commit
`9703960b` (branch `codex/p0-render-engine-settings-fix`, ora eliminato).
Il target `chronon3d_text_preset_visual_tests` compila e linka con rc=0.

### 2. ~~Sbloccare FontEngine per TXT-00~~ → **CHIUSO**

Il FontEngine è inizializzato correttamente nel test runner; l'injection è
avvenuta nei commit `3254ef9f` (FontEngine propagato da SceneBuilder a
LayerBuilder, iniettato nel visual test, `WORKING_DIRECTORY` corretto) e
`c68196d7` (asset_resolver mancante in `make_processor_context` che causava
SIGSEGV in `draw_text_run`). `VRTextPresetVisual` torna rc=0 — niente da
sbloccare ulteriormente.

### 3. ~~Chiudere TICKET-038 — lambda capture in test visuale~~ → **CHIUSO**

La build + esecuzione del target visuale non ha rivelato il problema lambda
capture / `auto` deduzione: `VRTextPresetVisual` rc=0 con 263/263 assertions
passate. La nota issue lambda è neutralizzata dal fix Agent 2 e non
riemerge su `main@f90174cc`.

### 4. Completare TICKET-009 residuo

Restano da chiudere separatamente:

- orphan cleanup ancora effettivamente non confluito, se presente dopo il branch
  cleanup;
- PR-B per il rot `CompileResult` e gli errori variant nel percorso
  `experimental/expressions`;
- verifica che il profilo stabile non dipenda da Expressions V2 sperimentale.

Ogni parte deve restare una modifica piccola e non sovrapposta.

## Priorità 1 — Sbloccare e certificare i test camera

### 1. Chiudere TICKET-029 — blocker specifico camera

TICKET-029 riguarda la type visibility di `camera_program_compiler.cpp` e blocca
il link/run dei test scene/camera compilati. Ora che TICKET-039 è risolto,
TICKET-029 è il prossimo blocker camera da affrontare.

Prove richieste:

- compilazione del translation unit;
- link di `chronon3d_scene_tests` e dei target camera compilati;
- esecuzione delle regressioni;
- nessun include circolare o secondo percorso compiler introdotto.

### 2. Rieseguire i fix camera recenti

Dopo TICKET-038 e TICKET-029, verificare almeno:

- projection variant preservation;
- orientation applicata una volta;
- OrbitMotion in camera-local basis;
- motion blur mode unico;
- checkpoint/pre-roll e random access;
- focal X/Y, gate fit e anamorphic/pixel aspect;
- descriptor camera di default nella composition.

I ticket passano a ✅ soltanto dopo esecuzione osservata.

## Priorità 2 — Produrre una nuova baseline sullo stesso commit

Eseguire, senza modificare soglie o saltare errori:

1. core build/test;
2. lean build/test;
3. no-content build/test;
4. architecture boundary gate;
5. renderer boundary gate;
6. scene/camera tests;
7. install consumer;
8. full-validation build/test.

Salvare commit, comandi, exit code e risultati in un nuovo documento sotto
`docs/baselines/`. Non aggiornare `STATUS` a verde sulla base di configure-only o
di risultati provenienti da commit differenti.

## Priorità 3 — Consumer SDK reale

Il consumer corrente verifica package e link. Estenderlo o affiancarlo con un
consumer fuori-tree che:

1. include soltanto header pubblici;
2. collega soltanto `Chronon3D::SDK`;
3. registra un `ExtensionModule` esterno;
4. costruisce una composizione con testo e camera compilata;
5. crea runtime/backend;
6. renderizza un frame;
7. scrive un PNG;
8. verifica dimensioni, hash o marker diagnostico.

Il vecchio alias non namespaced `Chronon3D_SDK` è stato rimosso. Non deve essere
ripristinato: il target pubblico canonico resta `Chronon3D::SDK`.

Profili minimi:

- core/no-text;
- text + Blend2D;
- no-content;
- release Linux;
- release Windows quando disponibile.

## Priorità 1.5 — TXT-01 rimanenti (Blocco 4+: PNG dump + scenari aggiuntivi)

Dopo la chiusura parziale del Blocco 3 (hash gate ingaggiato + failure-path
provata) restano aperti 4 dei 6 Definition-of-Done di TXT-01 sul main corrente:

1. `PNG o altro artifact visuale ispezionabile` — PNG dump da cablare nel TU;
   introduce `tests/helpers/render_regression.hpp::verify_golden_or_create` oppure
   un `tools/dump_text_preset_pngs.sh` parallelo al ctest, dump su failure o
   in modalità `--update-goldens`.
2. `almeno un testo corto e un testo lungo per ogni famiglia critica` — la matrice
   attuale usa solo `"THE QUICK BROWN FOX JUMPS"`. Aggiungere `LONG_TEXT_FIXTURE`
   (es. paragraph 4-line) per il gate di overflow.
3. `sfondo chiaro e scuro almeno per i preset con fill, stroke, glow o box` —
   la matrice attuale usa solo sfondo trasparente. Servono 2 combinazioni
   `CentroTextOptions.background_color` (light/dark) per i 4 preset della
   famiglia fill/stroke/glow/box.
4. `seriale e parallelo producono gli stessi riferimenti quando supportati` —
   harness determinismo che confronta i 128 hash con N_BUILD_WORKERS=1 vs
   N_BUILD_WORKERS=8.

Ognuno di questi sub-PR è piccolo (1 TU + 1 doc update), mantiene il contratto
di "lavoro direttamente su `main`, un task alla volta", e produce la propria
baseline dedicata (es. `docs/baselines/main-<sha>-txt-01-png-dump.md`,
`docs/baselines/main-<sha>-txt-01-light-dark.md`, ecc.).

## Priorità 4 — Text Production V1

Ordine consigliato (TXT-01 in cima, poi lavoro già pianificato):

1. (TXT-01 in corso) — completare i 4 DoD aperti sopra (PNG dump, light/dark BG,
   testo corto/lungo, determinismo seriale/parallelo).
2. (TXT-02) — `TextPresetRegistry` unica fonte della verità (dipende da TXT-01
   green end-to-end).
3. (TXT-03) — Contratto `TextPropertyStage` e invalidazione cache.
4. (TXT-04) — Character Offset realmente pre-shaping.
5. (TXT-05) — Ritiro della pipeline legacy `TextAnimator`.
6. (TXT-06) — Correttezza paragraph e layout (rimozione `doctest::skip()`).
7. (TXT-07) — Wiggly Selector nel resolver comune.
8. (TXT-08) — Wave Selector e completamento Range Selector.
9. (TXT-09) — Preset per-unità autentici (word_cascade, character_cascade, word_pop).
10. (TXT-10) — Rich text produttivo e semantic word IDs.
11. (TXT-11) — Modello timed text e parser SRT/VTT/JSON.
12. (TXT-12) — Subtitle compiler, highlight, karaoke e layout (16:9, 9:16, 1:1).
13. (TXT-13) — Text on Path completo.
14. (TXT-14) — Certificazione Text Production V1 finale + consumer SDK esterno.

Non iniziare Text 3D o MSDF prima di TXT-14.

## Priorità 5 — Camera Production V1

Dopo baseline e regression test:

1. completare TICKET-023 `OrientAlongPath`;
2. chiudere trajectory/base-state preservation;
3. chiudere diagnostica shot timeline;
4. aggiungere parity/golden del percorso compilato;
5. completare framing essenziale bounds-aware;
6. completare clipping necessario;
7. introdurre adapter parity per i percorsi legacy;
8. migrare preset e composizioni;
9. deprecare e rimuovere authoring duplicato.

Ogni nuova feature deve entrare nelle variant, registry e resolver canonici già
presenti. Non creare `CameraProgramV2`, un secondo catalogo o un secondo
projection contract.

## Priorità 6 — Export delle animazioni

Prima scrivere la specifica, poi il codice.

### Work package A — formato dichiarativo

Definire un ADR e uno schema versionato per:

- manifest;
- composition metadata;
- layer e gerarchie;
- keyframe/animation tracks;
- `TextDocument` e timed text;
- `CameraDescriptor` e shot timeline;
- effects tramite ID catalogo;
- asset/font references;
- parametri pubblici;
- compatibility e migration version.

### Work package B — C ABI

Definire handle opachi e ownership chiara per:

- engine/runtime;
- package;
- extension/plugin;
- render job;
- framebuffer;
- diagnostics/error;
- cancellation e progress.

Non esporre STL, eccezioni C++ o tipi interni nell’ABI.

### Work package C — binding iniziale

Creare il binding Python sul C ABI e usarlo come prova che il confine non dipende
dal linguaggio host.

## File ownership consigliata

Mantenere PR piccole e non sovrapposte:

- TICKET-038 text visual test;
- TICKET-009 orphan/experimental cleanup;
- TICKET-029 camera compiler/link;
- baseline;
- consumer SDK;
- timed text;
- camera orient/path;
- camera legacy migration;
- schema `.chronon`;
- C ABI.

Non mescolare refactor, nuova feature, documentazione ampia e modifica dei gate
nella stessa modifica salvo necessità architetturale dimostrata.

## Workflow Git obbligatorio

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
```

Lavoro direttamente su `main`, un task alla volta:

- cercare il codice esistente prima di aggiungere nuovi tipi;
- modificare soltanto i file del problema assegnato;
- eseguire test mirati;
- controllare `git status -sb` e `git diff`;
- non committare output, `node_modules` o `*.tsbuildinfo`;
- dopo il push: `git push origin main && git log -n 5 --oneline`.

## Criterio di chiusura

Un lavoro è chiuso soltanto quando:

- il codice usa il percorso canonico;
- i test pertinenti vengono eseguiti e passano;
- i gate non sono indeboliti;
- il commit è su `main` aggiornato;
- la modifica è piccola e reviewabile;
- i documenti riportano lo stesso stato osservato.
