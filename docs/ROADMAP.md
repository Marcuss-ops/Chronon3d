# Chronon3D — Active Roadmap

> Release identity and v0.1 acceptance contract: [`docs/RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md).
> This roadmap tracks future work and does not redefine release status.
> Stato corrente: [`docs/CURRENT_STATUS.md`](CURRENT_STATUS.md). Criteri di release: [`docs/RELEASE_GATE.md`](RELEASE_GATE.md).

Non avviare una milestone successiva per nascondere blocker della precedente.

## Decisione corrente — Cleanup → Glow V1 → Camera 2.5D V1

Il repository resta vincolato alla certificazione sullo stesso SHA. L'ordine
vincolante successivo è:

1. **Baseline cleanup** — tutti i gate, consumer e test richiesti sullo stesso SHA.
2. **Glow V1** — solo glow software CPU, con sampling continuo, bbox/dirty rect,
   alpha corretto, determinismo, landscape/portrait e video reale.
3. **Camera 2.5D V1** — un solo percorso descriptor/program/session, projection,
   OrientAlongPath, constraint e framing, DOF/motion blur e accesso casuale.
4. **Combined Product** — scena Glow + Camera certificata via CLI, SDK C++, C ABI,
   seriale/parallelo e cache cold/warm sullo stesso SHA.

Fino alla chiusura combinata non si aprono nuovi effetti, preset, binding,
plugin o sistemi sperimentali.

## Backlog prodotto — dopo la certificazione combinata

Queste lacune sono candidate per la prossima pianificazione. La priorità è
indicativa; ogni voce richiede un audit del codice e un ticket con contratto,
consumer, fingerprint/cache, capability GPU/software e test prima
dell'implementazione. Non sblocca lavoro mentre il gate sopra resta aperto.

1. **Effetti immagine** — valutare gaussian blur, drop shadow e glow/bloom
   tramite il catalogo effetti e i consumer canonici.
2. **Asset vettoriali** — import SVG e Lottie con risoluzione deterministica,
   limiti di risorse e supporto d'animazione espliciti.
3. **Animazione procedurale** — curve Bézier per-keyframe, poi driver riusabili;
   valutare espressioni dichiarative limitate solo con un contratto sicuro,
   deterministico e compilabile.
4. **Transizioni dichiarative** — preset di wipe, push, morph e glitch composti
   dalle primitive esistenti, senza un secondo motore d'animazione.
5. **Caption e data-viz** — caption sincronizzate a timing audio e preset
   data-viz, riusando text animator, shape e variabili già presenti.
6. **Modelli 3D nel piano** — esporre asset, luci e materiali solo dopo aver
   definito il confine con ChrononMotion3D e il percorso render certificabile.
7. **Particelle e fisica** — valutare sistemi limitati e riproducibili, con
   budget temporale/memoria e capability dichiarate.
8. **Animazione gradienti** — aggiungere track per i parametri gradiente se
   l'audit conferma che non sono già supportati dal contratto corrente.

Ordine iniziale suggerito: audio, effetti immagine, asset vettoriali e curve
Bézier. La sequenza va confermata dopo l'audit post-certificazione; stime di
parità o percentuali rispetto ad altri prodotti non sono criteri di accettazione.

## Milestones

| Milestone | Obiettivo | Note |
|---|---|---|
| M0 — Baseline verificata | Un commit candidato su cui build, test, gate, consumer e documenti riportano lo stesso stato | Gate di chiusura di ogni campagna |
| V0.1 — Acceptance suite | 20 contract rows nell'orchestratore `chronon3d_acceptance` | Contract definito in `RELEASE_V0_1_CONTRACT.md`; certificazione same-SHA ancora bloccata |
| M2 — Camera Production V1 | `CameraDescriptor → CameraProgram` unico percorso authoring nuovo | Copertura movimenti cinematografici 2.5D |
| M3 — CapCut-grade Parity | Parità visiva/comportamentale pipeline tipografica (subtitle + kinetic typography) | Post cycle V0.2; non avviabile come milestone macchina-verificata sul lineage corrente |
| M3 — SDK Product V1 | Chronon3D distribuito come SDK C++ installabile e documentato | Requisiti in `RELEASE_GATE.md` §SDK Product V1 |
| M4 — GPU backend Vulkan | Backend headless Vulkan production | IN PROGRESS |
| M4 — Pacchetti animazione | Altri programmi caricano animazioni Chronon3D senza compilare il core C++ | Interop |
| M5 — Global text ed effetti premium | ICU opzionale, variable fonts | Solo dopo M0–M4 |
| M6 — V3 tile-first | Evoluzione interna; non interrompe Text/Camera/SDK V1 né introduce pipeline parallela | |
| M1.8 — Text Simplicity | Ergonomia Remotion-like, headless/deterministico/server-side | Piano: `docs/TEXT_SIMPLICITY_ACTION_PLAN.md` |
| M7 — Video Compiler Architecture | `SceneIR → CompiledTemplateProgram → PreparedJobProgram → DeviceProgram → hot loop` | Temporal analysis, static island baking, PhysicalResourcePlan, command replay, PixelProgram IR + fusion, daemon + template cache |

## Vincoli permanenti

- non reintrodurre executor su raw graph o `ExecutionPlanCache`;
- non creare registry, resolver, sampler o cache paralleli;
- non costruire executor dentro i nodi;
- non indebolire gate per adattarli al codice.

## Global DoD Sign-off (21-item) — historical evidence, not v0.1 certification

La certificazione 21-item è evidenza storica: l'orchestratore
`tools/verify_chronon_product_linux.sh` (15 sub-gate, verdict aggregato
`CHRONON_PRODUCT_FUNCTIONAL_*`) è stato consolidato nei gate CI/manifest il
2026-09-03 (`722aec744`). Baseline: [`docs/baselines/main-ef9c83f1-baseline.md`](baselines/main-ef9c83f1-baseline.md);
gate correnti: `tools/gates/manifest.sh` + `.github/workflows/ci.yml`.
