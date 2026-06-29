# Chronon3D — Active Roadmap

> Snapshot: `main@4586d816`, 2026-06-29. Linux-only.
>
> Ultima baseline macchina-verificata: `main@446a60e2` (3/4 ✅).
>
> Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Requisiti di release: [`RELEASE_GATE.md`](RELEASE_GATE.md).

La roadmap è organizzata per milestone prodotto. Non avviare una milestone
successiva per nascondere blocker della precedente.

## M0 — Baseline verificata

### Obiettivo

Produrre un commit candidato sul quale build, test, gate, consumer e documenti
riportano lo stesso stato.

### Lavori

1. ✅ Chiudere il link/run di `chronon3d_scene_tests`, incluso **TICKET-029** — **RISOLTO** (`fb1b7e97`).
2. Rieseguire i regression test dei fix camera recenti (ora possibili grazie a TICKET-029).
3. ✅ Chiudere **TICKET-039** (`SoftwareRenderer::settings()` regression) — **RISOLTO** (`68c3e0f0`,
   TXT-00 build/link green, `chronon3d_text_preset_visual_tests` cablato in `44def204`).
4. ✅ Chiudere **TICKET-038** (lambda/auto rot in text preset visual) — **RISOLTO** (`91debc36`,
   3 source-level compile rot chiuse).
5. Chiudere i gap Precomp, execution scope e identity/session che bloccano la baseline.
6. Eseguire core, lean, no-content e full-validation sullo stesso commit.
7. Rendere architecture e renderer-boundary gate realmente bloccanti.
8. Eseguire install consumer sullo stesso commit.
9. Registrare comandi, commit ed esiti osservati in `docs/baselines/`.

### Gate di uscita

- nessun test richiesto skipped per nascondere un errore;
- nessun gate con `continue-on-error` sul percorso candidato;
- tutti i profili richiesti verdi sullo stesso commit;
- documenti sincronizzati.

## M1 — Text Production V1

### Obiettivo

Generare titoli, citazioni, keyword highlight e sottotitoli animati affidabili
per pipeline video automatizzate.

### Lavori

1. Completare rich text e styling per parola end-to-end.
2. Introdurre timed text/SRT/JSON e word timing.
3. Implementare highlight, karaoke, word pop e subtitle layout policies.
4. Completare Wiggly/Wave/Random selector richiesti dai preset.
5. Stabilizzare almeno 20 preset generali e 8 subtitle.
6. Aggiungere golden 16:9/9:16, testo corto/lungo e più timestamp.
7. Verificare 24/30/60 fps e determinismo seriale/parallelo.
8. Esporre esempi pubblici tramite `Chronon3D::SDK`.

### Non-goal M1

- Text 3D;
- MSDF;
- morph avanzato;
- supporto globale ICU completo;
- nuovo renderer testuale parallelo.

## M2 — Camera Production V1

### Obiettivo

Rendere `CameraDescriptor → CameraProgram` l’unico percorso authoring nuovo e
coprire i movimenti cinematografici necessari al motion graphics 2.5D.

### Lavori

1. Completare i ticket camera P0/P1 ancora test-blocked o parziali.
2. Completare `OrientAlongPath`, look-ahead, keep-horizon e banking.
3. Preservare correttamente stato base/projection in tutte le source.
4. Chiudere diagnostica e determinismo della shot timeline.
5. Portare framing a multi-target, bounds-aware, rule-of-thirds e dead-zone.
6. Completare clipping near/far necessario al renderer.
7. Migliorare DOF senza creare un secondo sistema ottico.
8. Aggiungere parity test e golden sul percorso compilato.
9. Migrare preset e composizioni, poi deprecare/rimuovere authoring legacy.

### Gate di uscita

- nuove composizioni senza `AnimatedCamera2_5D` o rig legacy;
- sessione posseduta dal render job;
- nessuna compilazione o lookup catalog per frame;
- test camera bloccanti verdi;
- consumer esterno usa una camera compilata.

## M3 — SDK Product V1

### Obiettivo

Distribuire Chronon3D come SDK C++ installabile e documentato, non soltanto come
repository sorgente.

### Lavori

1. Consumer esterno reale: extension pack, runtime, testo, camera, render e file output.
2. API pubblica minima documentata e header interni esclusi dagli esempi.
3. Compatibility/deprecation policy.
4. Versionamento package e verifica `find_package`.
5. Artifact Linux riproducibili.
6. Esempi separati dalla source tree.
7. Smoke test install/configure/build/run in CI.

### Gate di uscita

- un progetto vuoto può installare e usare `Chronon3D::SDK`;
- render di almeno una composizione reale fuori-tree;
- nessun link diretto a target interni;
- documentazione e artifact associati allo stesso tag.

## M4 — Pacchetti animazione e interoperabilità

### Obiettivo

Permettere ad altri programmi di caricare e usare animazioni Chronon3D senza
compilare direttamente il core C++.

### Lavori

1. Definire schema versionato `.chronon`.
2. Serializzare composition, layer, animation tracks, text, camera, effects e asset refs.
3. Loader con validazione, diagnostica e migration version.
4. Definire C ABI con handle opachi.
5. Esporre runtime, package, render job, framebuffer ed error API.
6. Creare primo binding Python.
7. Definire plugin ABI versionato per pack C++ non serializzabili.

### Regola

Python, C#, Node, Rust e altri binding devono appoggiarsi allo stesso C ABI.
Non duplicare il runtime in ogni binding.

## M5 — Global text ed effetti premium

Solo dopo M0–M4:

- ICU opzionale;
- variable fonts;
- color glyph/emoji;
- Expression Selector produttivo;
- Text 3D;
- MSDF;
- morph e displacement;
- ottica/DOF più avanzati.

## M6 — V3 tile-first

V3 è una futura evoluzione interna. Non deve interrompere la chiusura di Text,
Camera e SDK V1 né introdurre una pipeline parallela prima che i contratti V1
siano verificati.

## Vincoli permanenti

- non reintrodurre executor su raw graph o `ExecutionPlanCache`;
- non creare registry, resolver, sampler o cache paralleli;
- non costruire executor dentro i nodi;
- non indebolire gate per adattarli al codice;
- non promuovere `experimental/` perché compila in opt-in;
- non aggiungere GUI/browser al core headless;
- una PR deve risolvere un problema chiaro e avere test mirati.
