# Chronon3D — Current Status (canonico)

> Questo è l’unico documento di stato corrente del repository.
> Le vecchie snapshot `CURRENT_READINESS.md`, `STATUS.md` e `NEXT_STEPS.md`
> sono state eliminate perché duplicate e superate.

## Direzione prodotto

Chronon3D è un motore C++20 headless, Linux-only e CPU-first per motion graphics.
Il traguardo immediato è produrre animazioni testuali reali e camera 2.5D
cinematografica realmente utilizzabile dalla CLI.

Non sono priorità attuali:

- supporto Windows;
- parser JSON per testi;
- GUI o browser;
- binding cross-language;
- V3 tile-first;
- nuove pipeline parallele.

## Percorsi canonici

- Render: `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`
- Camera: `CameraDescriptor → compile_camera() → CameraProgram`
- Testo: `TextDocument/TextSpec → layout → animator stack → renderer`
- SDK pubblico: `Chronon3D::SDK`

## Obiettivo operativo immediato

Produrre e validare uno showcase camera + testo che dimostri:

1. traiettoria Bezier reale;
2. orientamento dalla tangente;
3. banking e keep-horizon;
4. lens, projection e DOF preservati;
5. preset testuali realmente temporali;
6. parallasse tra più piani;
7. output PNG e MP4 dalla CLI su Linux CPU.

## Stato sintetico

| Area | Stato | Gap principale |
|---|---|---|
| Render graph compilato | Avanzato | Baseline completa sullo stesso commit |
| Software backend | Avanzato | Gate e full validation da osservare insieme |
| Text Production V1 | In corso | Preset temporali reali, golden e consumer pubblico |
| Camera Production V1 | In corso | Tangente reale, preservazione stato e golden autentici |
| SDK C++ | Avanzato | Consumer fuori-tree che renderizzi una composizione reale |
| Expressions V2 | Sperimentale | Fuori dal percorso produttivo |
| V3 tile-first | Pianificato | Non prioritario |

## Camera

### Presente

- descriptor variant-based;
- programma compilato immutabile;
- sessione per render job;
- Zoom, FOV e Physical Lens;
- Pose Tracks, Orbit Motion e Trajectory Motion;
- look-at point/layer;
- constraint tipizzati;
- shot timeline e transizioni;
- motion blur temporale;
- traiettorie Bezier e showcase cinematografico.

### Da chiudere

- `OrientAlongPath` deve usare la tangente campionata, non il point of interest;
- trajectory source deve preservare projection, lens, DOF, motion blur e parent;
- banking e keep-horizon devono avere test matematici reali;
- i golden camera devono contenere riferimenti catturati e fallire in caso di drift;
- rimuovere progressivamente `AnimatedCamera2_5D` e authoring legacy dopo parity.

## Testo

### Presente

- FreeType, HarfBuzz e FriBidi;
- shaping, bidirezionalità, layout, wrap, overflow e auto-fit;
- `TextSpec`, `TextRunSpec`, `TextDocument`, span e paragrafi;
- animator e selector per glifo, grapheme, parola e riga;
- registry centrale dei preset;
- visual harness e composizioni dimostrative.

### Da chiudere

- `cinematic_title_reveal` realmente temporale;
- `word_cascade` realmente per parola;
- `character_cascade` realmente per glifo;
- `tracking_close` realmente animato nel tempo;
- frame iniziale, intermedio e finale visivamente differenti;
- golden reali e determinismo seriale/parallelo;
- consumer SDK che usi il percorso testuale pubblico.

## Gate di successo camera + testo

La milestone è chiusa quando esiste un render riproducibile che mostra:

- camera che percorre una curva e guarda lungo la tangente;
- banking visibile durante la curva e orizzonte stabile nel finale;
- almeno tre piani con parallasse;
- quattro animazioni testuali reali;
- frame finale leggibile;
- output MP4 prodotto dalla CLI su Linux CPU.

## Documenti canonici

| Documento | Ruolo |
|---|---|
| [`README.md`](../README.md) | Entry point e quick start |
| [`CURRENT_STATUS.md`](CURRENT_STATUS.md) | Stato corrente unico |
| [`ROADMAP.md`](ROADMAP.md) | Milestone attive |
| [`RELEASE_GATE.md`](RELEASE_GATE.md) | Criteri tecnici di validazione |
| [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) | Problemi ancora aperti |
| [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md) | Dettaglio camera |
| [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) | Dettaglio testo |
| [`adr/`](adr/) | Decisioni architetturali |

Quando un documento di dettaglio contraddice questo file, prevalgono il codice e
la prova eseguita più recente. Il documento in conflitto va corretto o eliminato.
