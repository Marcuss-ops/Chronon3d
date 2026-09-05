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
| M1.8 — Text Simplicity | Ergonomia Remotion-like, headless/CPU-first/deterministico/server-side | Piano: `docs/TEXT_SIMPLICITY_ACTION_PLAN.md` |
| M7 — Video Compiler Architecture | `SceneIR → CompiledTemplateProgram → PreparedJobProgram → DeviceProgram → hot loop` | Temporal analysis, static island baking, PhysicalResourcePlan, command replay, PixelProgram IR + fusion, daemon + template cache |

## Vincoli permanenti

- non reintrodurre executor su raw graph o `ExecutionPlanCache`;
- non creare registry, resolver, sampler o cache paralleli;
- non costruire executor dentro i nodi;
- non indebolire gate per adattarli al codice.

## Global DoD Sign-off (21-item) — historical evidence, not v0.1 certification

Il comando canonico di certificazione prodotto è `tools/verify_chronon_product_linux.sh`
(15 sub-gate eseguibili, verdict aggregato `CHRONON_PRODUCT_FUNCTIONAL_{PASS,FAIL,BLOCKED}`).
Dettaglio e baseline storiche: [`docs/baselines/`](baselines/) e [`docs/RELEASE_GATE.md`](RELEASE_GATE.md).
