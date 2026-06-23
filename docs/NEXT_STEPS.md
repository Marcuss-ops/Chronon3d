# Chronon3D — Next Steps Reali

> **Snapshot funzionale analizzato:** `main@25049b2`, 23 giugno 2026.
>
> **Ultima baseline eseguita e documentata:** `main@446a60e2`.
>
> **HEAD ricontrollato:** `main@83a4bf21`, 23 giugno 2026.
>
> Stato prodotto: [`CURRENT_READINESS.md`](CURRENT_READINESS.md).
> Milestone: [`ROADMAP.md`](ROADMAP.md).
> Prova operativa: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).

Questo documento descrive l’ordine operativo immediato. Le precedenti attività
“Agente 1 / Agente 2” non rappresentano più il piano corrente: il lavoro Agente 1
è stato integrato, mentre il branch Agente 2 non risulta avviato su origin.

## Stato osservato della baseline

La baseline registrata su `main@446a60e2` ha prodotto:

| Controllo | Esito |
|---|---|
| Software renderer boundary gate | ✅ PASS |
| `linux-full-validation` configure | ✅ PASS |
| `linux-lean-dev` configure | ✅ PASS |
| Build `chronon3d_text_preset_visual_tests` | ❌ FAIL |

Il verdetto complessivo resta **rosso**: tre controlli su quattro sono verdi, ma
il target compilato fallisce. Una configurazione CMake riuscita non equivale a
una build o a una baseline verde.

L’HEAD corrente `83a4bf21` contiene altri commit successivi alla baseline, ma
non esiste ancora una nuova validazione completa sullo stesso HEAD. I risultati
di `446a60e2` restano quindi l’ultima prova operativa canonica, non una
certificazione dell’HEAD corrente.

## Priorità 0 — Ripristinare la compilabilità della baseline

### 1. Chiudere TICKET-039 — blocker globale immediato

Il target `chronon3d_text_preset_visual_tests` si ferma in
`src/runtime/render_engine.cpp`: `RenderEngine::Impl` usa il vecchio accessor
`SoftwareRenderer::settings()`, mentre il confine renderer ha mantenuto come
accessor canonico `render_settings()`.

Criteri di chiusura:

- aggiornare il consumer al percorso canonico senza ripristinare sinonimi API
  inutili;
- compilare il translation unit interessato;
- rieseguire il target che ha scoperto il problema;
- aggiungere o mantenere una regressione mirata;
- aggiornare ticket e baseline con comando, commit ed exit code.

### 2. Chiudere TICKET-038 — secondo blocker di compilazione

Dopo TICKET-039 potrebbe riemergere il problema già noto in
`tests/text/test_text_preset_visual.cpp` relativo a lambda capture e deduzione
`auto`.

La chiusura richiede build ed esecuzione del target visuale, non soltanto la
compilazione isolata del file modificato.

### 3. Completare TICKET-009 residuo

Restano da chiudere separatamente:

- orphan cleanup ancora effettivamente non confluito, se presente dopo il branch
  cleanup;
- PR-B per il rot `CompileResult` e gli errori variant nel percorso
  `experimental/expressions`;
- verifica che il profilo stabile non dipenda da Expressions V2 sperimentale.

Ogni parte deve restare una PR piccola e non sovrapposta.

## Priorità 1 — Sbloccare e certificare i test camera

### 1. Chiudere TICKET-029 — blocker specifico camera

TICKET-029 riguarda la type visibility di `camera_program_compiler.cpp` e blocca
il link/run dei test scene/camera compilati. Non è più il primo blocker globale,
perché la baseline corrente si arresta prima su TICKET-039; resta però necessario
per certificare i fix camera recenti.

Prove richieste:

- compilazione del translation unit;
- link di `chronon3d_scene_tests` e dei target camera compilati;
- esecuzione delle regressioni;
- nessun include circolare o secondo percorso compiler introdotto.

### 2. Rieseguire i fix camera recenti

Dopo TICKET-039, TICKET-038 e TICKET-029, verificare almeno:

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

## Priorità 4 — Text Production V1

Ordine consigliato:

1. rendere verde il visual regression target esistente;
2. ampliare il visual regression harness sui preset già presenti;
3. chiudere rich text e styling per parola end-to-end;
4. introdurre modello timed text e parser JSON/SRT;
5. implementare word timing compiler;
6. chiudere subtitle layout policies 16:9, 9:16 e 1:1;
7. implementare highlight, karaoke e word-pop;
8. completare Wiggly/Wave/Random selector necessari ai preset;
9. certificare 20 preset generali e 8 subtitle;
10. verificare consumer SDK e documentazione pubblica.

Non iniziare Text 3D o MSDF prima di questo milestone.

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

- TICKET-039 renderer consumer;
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
nella stessa PR salvo necessità architetturale dimostrata.

## Workflow Git obbligatorio

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git checkout -b codex/<nome-blocco>
```

Durante il lavoro:

- cercare il codice esistente prima di aggiungere nuovi tipi;
- modificare soltanto i file del problema assegnato;
- fare rebase frequente su `origin/main`;
- eseguire test mirati;
- controllare `git status -sb` e `git diff`;
- non committare output, `node_modules` o `*.tsbuildinfo`;
- dopo il push controllare la cronologia recente.

## Criterio di chiusura

Un lavoro è chiuso soltanto quando:

- il codice usa il percorso canonico;
- i test pertinenti vengono eseguiti e passano;
- i gate non sono indeboliti;
- il branch è aggiornato su `origin/main`;
- la PR è piccola e reviewabile;
- i documenti riportano lo stesso stato osservato.
