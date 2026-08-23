# Chronon3D — Active Roadmap

La roadmap è organizzata per milestone prodotto. Non avviare una milestone
successiva per nascondere blocker della precedente.

Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md). Criteri di release: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

> **Snapshot osservato (2026-08-09):** `main@dc3fb34e` (worktree locale non pulito). Il cleanup legacy
> confermato è allineato al codice: registration descriptor-only, shaping
> canonico, `Composition` descriptor-only per la camera, factory unica del
> runtime, alias temporali/camera rimossi e ticket superati chiusi. Verifiche
> mirate PASS; golden visuali focalizzati 39/39 e camera visuale 9/9 passano
> su questo SHA; la baseline globale 11/11 resta da ricalcolare.

## Decisione corrente — Cleanup → Glow V1 → Camera 2.5D V1

Il repository resta vincolato alla certificazione sullo stesso SHA: il cleanup
non è ancora promosso a `CLEAN-BASELINE`; Glow V1, Camera 2.5D V1 e la fixture
combinata hanno certificazioni storiche su `main@5cfdf1cd`, ma non sono ancora
ricertificate sullo SHA corrente. Il gate cleanup globale resta da eseguire con
il checkout senza file non tracciati e con il full baseline runner.
Il lineage di cleanup osservato arriva a `main@20a102f3`: timing globale rimosso
dal job, Render Plan tipizzato e preparato prima del frame, CLI unificata, C ABI
isolata, ciclo base/animazioni spezzato, percorso non-modulare rimosso, adapter
legacy cancellati, asset integrity al render boundary e gate architetturali resi
bloccanti. La certificazione globale macchina è ancora `NOT RUN`: sanitizer
completo e packaging release richiedono un ambiente/checkout dedicato; i gate
focalizzati del nuovo SHA sono riportati in `CURRENT_STATUS.md`.

L'ordine vincolante successivo è:

1. **Baseline cleanup** — tutti i gate, consumer e test richiesti sullo stesso SHA.
2. **Glow V1** — solo glow software CPU, con sampling continuo, bbox/dirty rect,
   alpha corretto, determinismo, landscape/portrait e video reale.
3. **Camera 2.5D V1** — un solo percorso descriptor/program/session, projection,
   OrientAlongPath, constraint e framing, DOF/motion blur e accesso casuale.
4. **Combined Product** — scena Glow + Camera certificata via CLI, SDK C++, C ABI,
   seriale/parallelo e cache cold/warm sullo stesso SHA.

Fino alla chiusura combinata non si aprono nuovi effetti, preset, binding,
plugin o sistemi sperimentali.

## M4 — GPU backend Vulkan (IN PROGRESS, dopo la baseline corrente)

> Storico audit dettagliato archiviato in `ROADMAP.archive.md` (771 righe).


## M0 — Baseline verificata


### Obiettivo

Produrre un commit candidato sul quale build, test, gate, consumer e documenti
riportano lo stesso stato.

_Dettaglio completo in `ROADMAP.archive.md`._

## V0.1 — Acceptance suite (REGISTERED, macchina-verification deferred)


> **Origine:** TICKET-ACCEPTANCE-SUITE-PHASE-D closure commit (2026-07-11).
> 20 acceptance contract rows REGISTERED into `chronon3d_acceptance`
> aggregate meta-target (15 in-orchestrator + 1 out-of-tree + 4 forward-point
> catalog rows).

_Dettaglio completo in `ROADMAP.archive.md`._

## M2 — Camera Production V1


### Obiettivo

Rendere `CameraDescriptor → CameraProgram` l'unico percorso authoring nuovo e
coprire i movimenti cinematografici necessari al motion graphics 2.5D.

_Dettaglio completo in `ROADMAP.archive.md`._

## M3 — CapCut-grade Parity (in progress, post-V0.2 cycle)


> **Origine:** verdict CapCut-grade (2026-07-21, sessione CapCut parity). Formalizza
> il milestone di parità visiva e comportamentale con CapCut per la pipeline tipografica
> Chronon3D (subtitle + kinetic typography + static text + rendering globale).
> NON avviabile come milestone completamente macchina-verificata sul lineage corrente

_Dettaglio completo in `ROADMAP.archive.md`._

## M3 — SDK Product V1


### Obiettivo

Distribuire Chronon3D come SDK C++ installabile e documentato, non soltanto come
repository sorgente.

_Dettaglio completo in `ROADMAP.archive.md`._

## M4 — Pacchetti animazione e interoperabilità


### Obiettivo

Permettere ad altri programmi di caricare e usare animazioni Chronon3D senza
compilare direttamente il core C++.

_Dettaglio completo in `ROADMAP.archive.md`._

## M5 — Global text ed effetti premium


Solo dopo M0–M4:

- ICU opzionale;
- variable fonts;

_Dettaglio completo in `ROADMAP.archive.md`._

## M6 — V3 tile-first


V3 è una futura evoluzione interna. Non deve interrompere la chiusura di Text,
Camera e SDK V1 né introdurre una pipeline parallela prima che i contratti V1
siano verificati.


_Dettaglio completo in `ROADMAP.archive.md`._

## M1.8 — Text Simplicity (Remotion-like Ergonomics) (PLANNED)


> **Origine:** sessione 2026-07-10. Piano dettagliato per raggiungere l'ergonomia di Remotion
> mantenendo headless, CPU-first, deterministico, server-side, senza browser.
> Piano operativo completo: [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md).
> Regola operativa: commit atomici su `main`, nessuna branch, push frequente.

_Dettaglio completo in `ROADMAP.archive.md`._

## M7 — Video Compiler Architecture (PLANNED)


> Architettura "video compiler offline": `SceneIR → CompiledTemplateProgram →
> PreparedJobProgram → DeviceProgram → hot loop` (temporal analysis, static island
> baking, PhysicalResourcePlan, FrameSlot parameter ring, command replay, PixelProgram
> IR + fusion, PixelDomain inference YUV-first, daemon + template cache, Macro-ROI,

_Dettaglio completo in `ROADMAP.archive.md`._

## Vincoli permanenti


- non reintrodurre executor su raw graph o `ExecutionPlanCache`;
- non creare registry, resolver, sampler o cache paralleli;
- non costruire executor dentro i nodi;
- non indebolire gate per adattarli al codice;

_Dettaglio completo in `ROADMAP.archive.md`._

## Global DoD Sign-off (21-item) — PARTIAL-BLOCKED @ `main@ef9c83f1` (2026-07-12)


Il comando canonico di certificazione prodotto `tools/verify_chronon_product_linux.sh` orchestra 14 sub-gate eseguibili + 1 forward-pointed che coprono i **21 item DoD** dello spec utente (13 zero-require + 8 one-of). Stato corrente osservato: **`CHRONON_PRODUCT_FUNCTIONAL_BLOCKED`** (14/14 PASS + 1 forward-pointed `verify_diagnostics_linux`). Dettaglio: [`docs/baselines/main-ef9c83f1-baseline.md`](docs/baselines/main-ef9c83f1-baseline.md). Forward-point: `TICKET-VERIFY-DIAGNOSTICS-LINUX` + `TICKET-VERIFY-DIAGNOSTICS-ORCHESTRATOR-WIREIN` (separati per AGENTS.md "Fare PR piccole e mirate"). M0 §10 closes: l'orchestratore esiste + esegue + riporta verdict onesto.

---


_Dettaglio completo in `ROADMAP.archive.md`._

## Transitions cleanup roadmap (TRN-01..TRN-07)


Stato: CLOSURE CANDIDATE (source/gate audit corrente). Le fasi TRN-01..TRN-07
sono implementate nei percorsi canonici e coperte dai test/gate correnti; il
master tracker resta OPEN finché la sua scheda non viene sincronizzata.


_Dettaglio completo in `ROADMAP.archive.md`._
