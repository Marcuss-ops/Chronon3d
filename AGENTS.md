# Chronon3D Agent Instructions

File di ingresso obbligatorio per ogni agente.  Solo invarianti permanenti — cronaca
storica, esempi dettagliati e regole di lint documentale vivono in file separati:
[`docs/reference/AGENTS_LINT_RULES.md`](docs/reference/AGENTS_LINT_RULES.md) e
[`docs/reference/AGENTS_GATE_MNT.md`](docs/reference/AGENTS_GATE_MNT.md).

---

## Prima di iniziare

Lavorare sempre su `main` aggiornato:

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git status -sb
```

Verifica canonici presenti:

```bash
git ls-tree -r --name-only HEAD docs/CURRENT_STATUS.md docs/ROADMAP.md docs/RELEASE_GATE.md docs/FOLLOWUP_TICKETS.md
```

---

## Documenti canonici (4 file, solo fonti di stato)

| Canonical | Responsabilità |
|---|---|
| `docs/CURRENT_STATUS.md` | stato presente per area (PASS / FAIL / NOT RUN) |
| `docs/ROADMAP.md` | direzione futura (milestone, gate) |
| `docs/RELEASE_GATE.md` | requisiti permanenti di release |
| `docs/FOLLOWUP_TICKETS.md` | indice one-line dei blocker attivi (max ~10 righe) |

**Pattern di filename vietati**: `STATUS.md`, `NEXT_STEPS.md`, `ROADMAP_*.md`,
`RELEASE_*.md`, `FOLLOWUP_*.md` e ogni variante simile.

**Disciplina di aggiornamento**: solo quando cambia lo stato di un'area
(CURRENT_STATUS), si apre/chiude un blocker (FOLLOWUP_TICKETS), una milestone
chiude (CHANGELOG), la direzione futura cambia (ROADMAP), o un requisito
permanente di release viene modificato (RELEASE_GATE).

**Fix piccoli** (`fix:`, `chore:`, `refactor:`) **NON toccano i canonici**.
Dettagli tecnici → `docs/tickets/TICKET-NNN.md`.  Nei canonici: max una riga
sintetica (stato + link al ticket).

### Documenti di supporto

| Ruolo | File |
|---|---|
| Piano operativo | `docs/CHRONON_PLAN.md` |
| Zero-copy gates | `docs/ZERO_COPY_GATES.md` |
| Baseline certificata | `docs/baselines/main-<sha>-baseline.md` |
| Scheda ticket | `docs/tickets/TICKET-NNN.md` |
| ADR | `docs/adr/ADR-NNN-<titolo>.md` |
| Regole lint | `docs/reference/AGENTS_LINT_RULES.md` |

---

## Priorità

1. Baseline verde su ogni commit su `main`.
2. Text V1 completamento.
3. Camera V1 completamento.
4. V0.1 release (SDK packaging, cross-language ABI).
5. Una sola strategia di packaging CMake per l'SDK.

---

## Regole di lavoro

- Cercare prima codice e documenti esistenti.
- **No duplicazione** di registry, resolver, sampler, cache, service locator.
- Non segnare verde una suite che restituisce failure.
- Ogni nuova feature usa il registry/resolver/sampler canonico già esistente.
- **No `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>`** senza ADR (gate architecture rules).
- **No nuovi singleton/registry/resolver/cache** senza ADR.
- **No stime percentuali**: usare `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.
- No GUI, browser o dipendenze GPU nel core headless CPU-first.
- PR piccole e mirate.
- Non committare build/, output/, artefatti, file generati.
- Dopo ogni push: `git log -n 5 --oneline`.

### Demolition Debt — ogni ingresso abilita un'uscita

Ogni nuova dipendenza, bridge, compatibility path, parser o shim introdotto
per una migrazione DEVE indicare anche cosa potrà essere rimosso. Non si
accettano duplicazioni permanenti del tipo "nuovo sistema + vecchio sistema"
senza una scheda di **Demolition Debt** nel ticket o nell'ADR collegato.

La scheda DEVE contenere:

1. **Owner** — chi mantiene il bridge e aggiorna la scheda.
2. **Reason** — quale migrazione incompleta lo rende ancora necessario.
3. **Exit condition** — condizione osservabile e misurabile per rimuoverlo.
4. **Equivalence test/gate** — test che dimostra la parità del replacement.
5. **Removal scope** — file, dipendenze, counter e flag da cancellare.
6. **Status** — `ACTIVE`, `READY_TO_REMOVE` o `REMOVED`.

La regola vale in particolare per:

- fallback Vulkan verso CPU, immediate-submit compatibility path e counter `legacy_*`;
- parser o scanner custom sostituiti da ICU, SVG++/XML parser o altro componente;
- expression parser custom in attesa di un backend con parity certificata;
- dipendenze non-core (Boost, validator, compressori, parser) introdotte per una sola feature;
- validazione manuale duplicata da JSON Schema o da un contratto generato;
- fault-injection, crash-artifact e debug GPU aggiunti come lane di certificazione;
- gate di build, ABI, SBOM e riproducibilità che sostituiscono controlli manuali;
- stub, alias e shim di compatibilità.

Quando l'exit condition è soddisfatta, la rimozione è un lavoro obbligatorio
successivo, non un'opzione. Restano invece permanenti solo i componenti che
esprimono policy o semantica Chronon — graph compiler, scheduler, resolver,
determinism e allocazione fisica — e non implementazioni generiche già fornite
da una dipendenza specializzata.

---

## Cache taxonomy (3 famiglie, nessuna altra)

Ogni cache deve appartenere a ESATTAMENTE una famiglia.  Vedi
`include/chronon3d/cache/cache_taxonomy.hpp` e `tools/architecture_rules.toml`.

| Famiglia | Semantica | Membri |
|---|---|---|
| **ContentCache** | Stessa chiave → stesso output, sempre | NodeCache, FrameCache, VideoFrameCache, GpuAssetCache, GpuGlyphAtlas styled |
| **ResidencyCache** | Bounded memory / residency reuse | FramebufferPool (cold-path), GpuGlyphAtlas pages |
| **ProgramCache** | Stesso fingerprint → stesso programma compilato | TemplateProgramCache, CompiledGraphCache, SceneProgramCache, OverlayTemplateCache |

Il placement hot-path non appartiene alla cache taxonomy: `runtime::ResourcePlanner`
/ `runtime::ResourcePlan` sono algoritmi e risultati effimeri di placement; la sola
authority persistente compilata è `graph::CompiledResourceTable`.

**Primitive canonica**: `LruCache` (include/chronon3d/cache/lru_cache.hpp).  Nessuna seconda cache engine.

---

## No-duplication invariants

- **Una sede canonica** per ogni informazione (Cat-3 anti-dup).
- **Registry condivisi**: AssetRegistry, RenderSurfaceRegistry, EffectCatalog — nessun registry parallelo per feature.
- **Resolver canonico**: AssetResolver (include/chronon3d/assets/asset_resolver.hpp).
- **Unico compilatore**: CompiledFrameGraph (include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp).
- **Unica authority persistente di allocazione hot-path**: CompiledResourceTable (include/chronon3d/render_graph/compiler/compiled_resource_table.hpp); ResourcePlanner/ResourcePlan restano solo placement effimero durante il lowering.
- **Unico executor**: GraphExecutor → node_runner.cpp (compiled path) o node_executor.cpp (fallback).

---

## Workflow Git obbligatorio

```bash
# Pre-push
git fetch origin && git checkout main && git pull --ff-only origin main

# Modifiche + test
git status -sb && git diff
# run targeted tests
git add <solo-file-modificati>
git commit -m "<tipo(scope): descrizione>"

# Push (usa il wrapper canonico)
bash tools/wrap_push.sh origin main

# Verify landed (SHA-triple invariant)
LOCAL_SHA="$(git rev-parse HEAD)"           # capture pre-push
bash tools/wrap_push.sh origin main
POSTPUSH_SHA="$(git rev-parse HEAD)"
UPSTREAM_SHA="$(git rev-parse '@{u}')"
[ "$LOCAL_SHA" = "$POSTPUSH_SHA" ] && [ "$POSTPUSH_SHA" = "$UPSTREAM_SHA" ] \
  || { echo "SHA MISMATCH: lost-commit pattern detected" >&2; exit 1; }

git log -n 5 --oneline
```

**Per-branch rebase**: `git config branch.main.rebase true` (obbligatorio).

**GATE-MNT-01**: `tools/check_main_clean.sh` fallisce se: fetch fallisce, HEAD
diverge da origin/main, tree sporco, o `branch.main.rebase != true`.  Wrapper
canonico: `tools/wrap_push.sh origin main`.  Dettagli: `docs/reference/AGENTS_GATE_MNT.md`.

---

## Quando un file sembra mancare

1. `git status -sb` + `git rev-parse HEAD`
2. `git fetch origin`
3. Confrontare `HEAD` con `origin/main`
4. Aggiornare il checkout prima di concludere che il file non esiste

Non creare file sostitutivi: usare i percorsi canonici.

---

## Architecture rules

Le regole architetturali dichiarative vivono in `tools/architecture_rules.toml`
e sono eseguite da `tools/check_architecture.py`. I gate specialistici che non
rientrano nel motore dichiarativo devono essere registrati una sola volta in
`tools/gates/manifest.sh`; wrapper e CI consumano il manifest senza duplicarne
la lista. CI: `.github/workflows/ci.yml` (Gate 5 / architecture-check).

---

## Feature Freeze legacy (revocato 2026-07-06)

Regole permanenti ereditate:
- **No stime percentuali** (già sopra).
- **No espansione API non necessaria** in `include/chronon3d/`.
- **No nuovi singleton/registry/resolver/cache** senza ADR (già sopra).
- **No `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>`** senza ADR (già sopra).

---

## CI workflows (3 famiglie)

| Workflow | Contenuto |
|---|---|
| `.github/workflows/ci.yml` | Build matrix, 8 gates, full validation, lint, render contracts, golden, benchmarks |
| `.github/workflows/nightly.yml` | ASan/UBSan, TSan, cinematic visual, profile envelope |
| `.github/workflows/runtime.yml` | Docker image build & publish |

---

## Regole di lint documentale

Le regole complete (SHA cite pattern, INFO-level diagnostic style, test binary
staleness check, C++ default-arg uniqueness, post-push SHA-selfcheck, docs
canonical update discipline, deprecation reversal bundles) sono in
[`docs/reference/AGENTS_LINT_RULES.md`](docs/reference/AGENTS_LINT_RULES.md).

Riepilogo rapido:
- **SHA cite**: citare inline, non standalone.
- **INFO diagnostic**: `[INFO] <gate-name>: <message>` su PASS addizionale.
- **Test staleness**: verificare binary esiste + è più recente del source prima di `ctest`.
- **C++ default-arg**: unico punto nella dichiarazione primaria, MAI duplicare negli stub inline.
- **Post-push SHA-selfcheck**: SHA-triple equality dopo ogni push.
- **Docs canonical discipline**: cronaca estesa solo nei ticket, max 1 riga nei canonici.
- **Deprecation reversal**: bundle atomic: source + FOLLOWUP_TICKETS + ticket + CHANGELOG.

---

## Testing requirements

- Eseguire almeno i test del modulo toccato prima della PR.
- **No `ctest` su build stale**: verificare binary esiste + mtime (vedi lint rules).
- Target di build canonicale: `build/fast` (preset `linux-fast-dev`).
- Suite minima pre-push: backend_registry, compositor, render_job_contract, render_graph.
