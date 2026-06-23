# Chronon3D — Next Steps Reali

> Snapshot: `main@25049b2`, 23 giugno 2026.
>
> Stato prodotto: [`CURRENT_READINESS.md`](CURRENT_READINESS.md).
> Milestone: [`ROADMAP.md`](ROADMAP.md).

Questo documento descrive l’ordine operativo immediato. Le precedenti attività
“Agente 1 / Agente 2” sono già state integrate e non rappresentano più il piano
corrente.

## Priorità 0 — Rendere verificabile il codice già atterrato

### 1. Chiudere TICKET-029

Obiettivo: `chronon3d_scene_tests` deve collegare ed eseguire.

Il blocker impedisce di trasformare diversi fix camera da “compila” a
“regression test verificato”. Non aggiungere nuove feature camera importanti
prima di questa chiusura.

Prove richieste:

- build del target scene tests;
- esecuzione dei test camera compilati;
- nessun include/circular dependency workaround fuori dal percorso canonico;
- ticket e matrice camera aggiornati con comando, commit ed esito.

### 2. Rieseguire i fix camera recenti

Dopo TICKET-029, verificare almeno:

- projection variant preservation;
- orientation applicata una volta;
- OrbitMotion in camera-local basis;
- motion blur mode unico;
- checkpoint/pre-roll e random access;
- focal X/Y, gate fit e anamorphic/pixel aspect;
- descriptor camera di default nella composition.

I ticket passano a ✅ soltanto dopo esecuzione osservata.

### 3. Produrre una baseline sullo stesso commit

Eseguire, senza modificare soglie o saltare errori:

1. core build/test;
2. lean build/test;
3. no-content build/test;
4. architecture boundary gate;
5. renderer boundary gate;
6. scene/camera tests;
7. install consumer;
8. full validation.

Salvare commit, comandi, exit code e risultati nei documenti canonici.

## Priorità 1 — Consumer SDK reale

Il consumer corrente verifica il package e il link. Estenderlo o affiancarlo con
un consumer fuori-tree che:

1. include soltanto header pubblici;
2. collega soltanto `Chronon3D::SDK`;
3. registra un `ExtensionModule` esterno;
4. costruisce una composizione con testo e camera compilata;
5. crea runtime/backend;
6. renderizza un frame;
7. scrive un PNG;
8. verifica dimensioni, hash o marker diagnostico.

Profili minimi:

- core/no-text;
- text + Blend2D;
- no-content;
- configurazione release Linux;
- configurazione release Windows quando disponibile.

## Priorità 2 — Text Production V1

Ordine consigliato:

1. visual regression harness sui preset già presenti;
2. chiusura rich text e styling per parola end-to-end;
3. modello timed text e parser JSON/SRT;
4. word timing compiler;
5. subtitle layout policies 16:9, 9:16 e 1:1;
6. highlight/karaoke/word-pop;
7. Wiggly/Wave/Random selector necessari ai preset;
8. catalogo di 20 preset generali + 8 subtitle;
9. consumer SDK e documentazione pubblica.

Non iniziare Text 3D/MSDF prima di questo milestone.

## Priorità 3 — Camera Production V1

Dopo la baseline e i regression test:

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

## Priorità 4 — Export delle animazioni

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

- baseline/link/test;
- consumer SDK;
- text visual harness;
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
