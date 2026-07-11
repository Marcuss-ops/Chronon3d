# Chronon3D Agent Instructions

Questo file è il punto di ingresso obbligatorio per ogni agente che lavora nel repository.

## Prima di iniziare

L'agente deve lavorare su `main` aggiornato:

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
git status -sb
```

Se il checkout non contiene i file elencati sotto, il clone o worktree è obsoleto. Non inventare percorsi alternativi e non ricreare copie dei documenti.

Verifica rapida (i documenti canonici devono essere presenti nel checkout):

```bash
git ls-tree -r --name-only HEAD docs/CURRENT_STATUS.md docs/ROADMAP.md docs/RELEASE_GATE.md docs/FOLLOWUP_TICKETS.md
```

## Insieme canonico della documentazione

I seguenti quattro file sono le **uniche fonti canoniche** di stato, requisiti, roadmap e problemi.
Qualsiasi stato osservabile del repository deve risiedere in uno (e solo uno) di questi file.

| Canonical                                          | Responsabilità unica                                                          |
| -------------------------------------------------- | ----------------------------------------------------------------------------- |
| [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) | stato presente (SHA, ultima baseline, blocker correnti, stato per area)        |
| [`docs/ROADMAP.md`](docs/ROADMAP.md)               | direzione futura (milestone, gate di uscita, non-goal)                        |
| [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md)     | requisiti permanenti di release (Text V1, Camera V1, SDK V1)                 |
| [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) | indice one-line dei blocker attivi (max ~10 righe)                        |

I documenti storici (vecchi `docs/STATUS.md`, `docs/NEXT_STEPS.md`, `docs/stabilization-plan/**`,    // drift-allow: archived-doc-pattern
plan agente legacy `docs/agent-tasks/`) vivono in `docs/ARCHIVE/` e **non** sono operativi.
Nessun riferimento operativo a path in `docs/ARCHIVE/` è consentito.

### Pattern di filename vietati

Sono vietati nuovi file con nome simile ai canonicals. In particolare:

* `docs/STATUS.md`, `docs/STATUS_NEXT.md`, `docs/STATUS_<variante>.md` → **VIETATO**    // drift-allow: archived-doc-pattern
* `docs/NEXT.md`, `docs/NEXT_STEPS.md`, `docs/NEXT_<variante>.md` → **VIETATO**    // drift-allow: archived-doc-pattern
* `docs/ROADMAP_<variante>.md` (es. `ROADMAP_DEV.md`) → **VIETATO**
* `docs/RELEASE_<variante>.md` (es. `RELEASE_v0.md`) → **VIETATO**
* `docs/FOLLOWUP_<variante>.md` (es. `FOLLOWUP_BACKLOG.md`) → **VIETATO**
* qualsiasi combinazione di prefissi simili (`CURRENT_STATUS_*.md`, `ROADMAP_*.md`, ...) → **VIETATO**

Per qualsiasi nuova informazione operativa: aggiungere il contenuto al **canonical esistente** appropriato
(stato → `CURRENT_STATUS`, requisiti → `RELEASE_GATE`, futuro → `ROADMAP`, blocker → `FOLLOWUP_TICKETS`).

### Regola del gate doc-sync

`tools/check_doc_sync.sh` deve accettare aggiornamenti **SOLO** ai 4 file canonical sopra elencati.
Qualsiasi check o validazione che richieda aggiornamenti a file diversi (es. `docs/STATUS.md`,    // drift-allow: archived-doc-pattern
`docs/NEXT_STEPS.md`, `docs/stabilization-plan/...`) è vietato e deve essere rimosso — // drift-allow: archived-doc-pattern
quei file sono storici, vivono in `docs/ARCHIVE/` e non possono essere modificati operativamente.

### Documenti di supporto (NON fonti canoniche di stato)

I seguenti file sono **ruoli documentali di supporto**: possono linkare i canonicals ma **non** devono duplicarne il contenuto.
Per il contratto completo di ciascun ruolo, vedi [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md).

| Ruolo                              | File                                               |
| ---------------------------------- | -------------------------------------------------- |
| Contratto documentale              | `docs/DOCUMENTATION_GOVERNANCE.md`                 |
| Audit registry                     | `docs/ARCHITECTURE_AUDIT.md`                       |
| Inventario feature                 | `docs/FEATURES.md`                                 |
| Piano testo                        | `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`      |
| Matrice camera                     | `docs/CAMERA_FEATURE_MATRIX.md`                    |
| Futuro tile-first                  | `docs/V3_BLUEPRINT.md`                             |
| Chiusure recenti                   | `docs/CHANGELOG.md`                                |
| Baseline macchina-verificata       | `docs/baselines/main-<sha>-baseline.md`             |
| Scheda ticket specifico            | `docs/tickets/TICKET-NNN.md`                       |    // drift-allow: ticket-template-pattern
| Decisione architetturale           | `docs/adr/ADR-NNN-<titolo>.md`                     |
| Materiale storico (non operativo)  | `docs/ARCHIVE/`                                     |



## ✅ Feature Freeze — REVOCATO (2026-07-06)

**Baseline verde certificata: `main@7eb5c2ba` — 11/11 PASS.**

Il feature freeze V0.1, attivo dal 2026-06-29, è stato revocato.
La baseline è documentata in [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md).

### Regole permanenti (ereditate dal freeze)

Queste regole restano in vigore come best practices, non più come blocco:

- **No stime percentuali**: usare stato osservabile `PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.
- **No espansione API non necessaria**: ogni nuovo simbolo in `include/chronon3d/` va giustificato.
- **No nuovi singleton/registry/resolver/cache** senza ADR.
- **No `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>`** senza ADR.

---

### Install Pipeline Plumbing (Cat-4 ancillary)

Asset class documentale per audit/tooling ancillari al
`tools/install_consumer_test.sh` pipeline (categoria 4 del feature
freeze: external consumer SDK).  Questi script NON sono parte dei 11/11
gate baseline — sono rot-pattern preventive FU4-lineage, invocati
durante la pipeline install/consumer o stand-alone via `bash`.

Asset category: **`INSTALL_PIPELINE_PLUMBING`**.

| Script                                          | Asset Category              | Function |
|-------------------------------------------------|-----------------------------|----------|
| `tools/audit_incomplete_type_pattern.sh`        | INSTALL_PIPELINE_PLUMBING   | std::make_shared\<T\> umbrella-header full-def probe. Emette **`BROKEN`** se l'header canonico del tipo T in `include/chronon3d/` contiene solo `class T;` (forward declaration) invece di `struct T { ... }` (full definition).  FU4 rot preventive (TICKET-GATE-10-PHASE-4-BLACK-FU4 closure lineage). |

Stand-alone usage:

```bash
bash tools/audit_incomplete_type_pattern.sh                        # default: tests/install_consumer/ + include/chronon3d/
INCOMPLETE_TYPE_SCAN_PATHS='tests/integration_test/main.cpp src/sdk_consumer/' \  <!-- drift-allow: stale-ref -->
  bash tools/audit_incomplete_type_pattern.sh                       # override scan paths
```

Exit codes (custom): 0 = clean, 1 = BROKEN rot detected, 2 = internal
error.  Status degli script INSTALL_PIPELINE_PLUMBING è tracciato in
`docs/CHANGELOG.md`; rot-pattern detect è referenziato come blocker in
`docs/FOLLOWUP_TICKETS.md` quando attiva.

---

## Priorità obbligatoria

1. Mantenere baseline verde: 11/11 gate su ogni commit su `main`.
2. Text V1 completamento (golden test, preset, kinetic typography).
3. Camera V1 completamento (runtime certification, DOF, motion blur, framing).
4. V0.1 release (SDK packaging, cross-language ABI, formato `.chronon`).
5. Certificare una sola strategia di packaging CMake per l'SDK (verifica `ar t` + `nm -C`).
6. Riallineare `docs/CURRENT_STATUS.md` e `docs/ROADMAP.md` alla baseline osservata.

## Regole di lavoro

- Cercare prima il codice e i documenti esistenti.
- Non duplicare registry, resolver, sampler, cache, service locator o checklist.
- Non segnare verde una suite che restituisce failure.
- Non cambiare un gate per nascondere un errore.
- Aggiornare il piano relativo nello stesso commit che cambia lo stato.
- Ogni nuova feature deve usare il registry, resolver o sampler canonico già esistente.
- Non introdurre GUI, browser o dipendenze GPU nel core headless CPU-first.
- Non introdurre `#include <msdfgen>`, `<libtess2>` o `<unicode[/...]>` da nessuna parte — dei pattern deny-everywhere di Gate 5 in `tools/check_architecture_boundaries.sh` (Check 11). Per deroghe serve prima un ADR.
- Fare PR piccole e mirate, senza mescolare refactor indipendenti.
- Non committare `node_modules/`, directory di build, output, artefatti o file generati.
- Eseguire almeno i test del modulo toccato prima della PR.
- Dopo ogni push verificare la cronologia recente con `git log -n 5 --oneline`.

## Regole di lint documentale

Questa sezione raccoglie regole di disciplina documentale dedotte dai cleanup pregressi (non legate a un singolo ticket). Le regole sono permanenti: ogni nuova doc-rule che emerge da un cleanup deve essere aggregata qui — non lasciata implicita nel codice, dove decade in code review ripetuti.

### SHA cite pattern (inline-only rule)

Quando lo stesso commit SHA deve apparire in una sezione (es. §References) o nel corpo di un testo, preferire **sempre** la citazione narrativa inline-on-file-location rispetto a una voce di catalogo standalone separata.

**Perché**: la citazione inline trasporta simultaneamente il ruolo semantico E fornisce `rg`-discoverability in egual misura. Le voci standalone non necessarie creano pattern di duplicazione dello stesso SHA, che richiedono deduplicazione a valle.

**Origine**: regola dedotta dai commit `3febd8cd` e `4cded60e` (lineage di dedup di ADR-020); la regola stessa applica il pattern che enuncia — cita gli SHAs inline, non come voci di catalogo separate.

#### Anti-esempio — SHA cite duplication

**VIETATO (duplicato standalone)**

> - Commit `1a2b3c4d` (correzione del regression-lock).
> - Risolto nel commit `1a2b3c4d` ripristinando il bounding box.

**CORRETTO (singolo inline)**

> - Risolto nel commit `1a2b3c4d` (correzione del regression-lock ripristinando il bounding box).

### INFO-level diagnostic style (gates)

Per i gate CI (`tools/check_*.sh`) che emettono un singolo messaggio informativo addizionale sullo stato clean (in aggiunta al canonico `GATE_PASS` o `OK:` finale), adottare il formato:

```
[INFO] <gate-name>: <message>
```

dove:
- `<gate-name>` = basename dello script senza estensione `.sh` (es. `check_test_suite_registration` per `tools/check_test_suite_registration.sh`); raccomandata la dichiarazione come variabile bash `GATE_NAME=...` in cima allo script per grep-discoverability;
- `<message>` = una sola riga, ≤ 200 caratteri, semanticamente ancorata allo stato del gate (no duplicato del `GATE_PASS` finale);
- **emissione**: una sola volta sullo stato clean (PASS), come riga **addizionale** rispetto al canonico `GATE_PASS` / `OK:` finale — MAI sul FAIL (il FAIL path resta invariato, emette `GATE_FAIL:` con la lista dei colpevoli).

**Perché**: esiste un gap semantico tra `OK:` (stato PASS canonico) e `WARN:` (stato advisory): mancava un livello "informativo addizionale" per audit signal one-shot sul clean state. La convenzione `[INFO] <gate-name>:` risolve il gap introducendo un prefisso grep-discoverabile + un self-identifier che localizza l'origine del messaggio senza ambiguità (a differenza di `OK:` che richiede contesto esterno per sapere da quale gate proviene). La 14-entry sibling-gate audit di `tools/check_doc_sync.sh` ha confermato il gap: 14 gate usano `OK:` / `GATE_PASS` / `HYGIENE_PASS [N/M]:` / `WARN:` / `FAIL:` ma nessuno usa `[INFO]` come livello intermedio.

**Origine**: regola dedotta dal commit di chiusura del forward-point 0j+ amendment (parte del 5-commit lineage TICKET-LAYER-IMAGE-MANIFEST-CLEAN + 0j+ + 0k+ su `tools/check_test_suite_registration.sh`). Il pattern è stato introdotto nel gate come riga aggiuntiva al `GATE_PASS` canonico per segnalare il clean state in modo audit-friendly senza dover grep-are `0 raw` nel flusso di output. Questa è la **seconda** rule del bucket `## Regole di lint documentale` (la prima è `### SHA cite pattern (inline-only rule)`), soddisfacendo il closure criterion `2+ rule aggregate` del ticket [TICKET-FOLLOWUP-PRECEDENT-DOCS](docs/tickets/TICKET-FOLLOWUP-PRECEDENT-DOCS.md) (vedi §Recently Closed in `docs/FOLLOWUP_TICKETS.md`).

**Scope**: la regola si applica ai gate **NUOVI** (introdotti dopo questo commit). I 14 gate esistenti che usano `OK:` / `GATE_PASS` / `HYGIENE_PASS [N/M]:` / `WARN:` / `FAIL:` restano invariati (nessun churn retroattivo — AGENTS.md v0.1 §regole "Fare PR piccole e mirate"). Il pattern è **raccomandato** (non mandatorio) per le future aggiunte di audit INFO one-liner; gate esistenti mantengono il loro prefisso storico.

#### Anti-esempio — INFO-level diagnostic style

**VIETATO (prefisso inconsistente con la famiglia)**

```bash
# Prefisso senza square brackets (NON grep-discoverabile come famiglia):
echo "INFO: check_test_suite_registration: clean state"

# Prefisso senza gate-name self-identifier (richiede contesto esterno):
echo "[INFO] 0 raw matches found — clean state"

# Emissione sul FAIL path (confonde il ruolo semantico):
if [ "$raw_count" -gt 0 ]; then
    echo "[INFO] check_test_suite_registration: $raw_count raw add_executable remain"  # SBAGLIATO
    exit 1
fi

# Multi-linea (rompe grep-discoverability one-liner):
echo "[INFO] check_test_suite_registration:"
echo "  clean state across $N test cmake files"
echo "  with $suite_count suite invocations verified"
```

**CORRETTO (formato canonico)**

```bash
# In cima allo script:
GATE_NAME=check_test_suite_registration

# ... loop di audit ...

if [ "$raw_count" -gt 0 ]; then
    echo "GATE_FAIL: $raw_count raw add_executable call(s) remain in:"  # FAIL path invariato
    ...
    exit 1
fi

echo "GATE_PASS: 0 raw add_executable — all $total test targets use chronon3d_add_test_suite()"  # PASS canonico
echo "[INFO] ${GATE_NAME}: 0 raw matches found — clean state (all $suite_count suite invocations verified)"  # INFO addizionale
exit 0
```

**Lint-checkability (forward-point)**: un futuro `tools/check_info_diagnostic_style.sh` (gate opzionale, NON ancora implementato) potrebbe verificare che ogni emissione `[INFO] ...` in `tools/check_*.sh` rispetti il pattern `[INFO] ${GATE_NAME}: ...` + sia seguita dal canonico `GATE_PASS` / `OK:`. L'implementazione è deferred a un ticket separato (AGENTS.md v0.1 §regole "Fare PR piccole e mirate" + Cat-3 anti-duplication: il rule documentation precede il lint tooling).

### Test binary staleness check (honesty, pre-ctest invariant)

Before running `ctest -R <pattern>` on a test file that was added or
modified in a recent commit, ALWAYS verify the corresponding test
binary exists in the build directory AND is newer than its source.
This prevents a stale-build false-negative: ctest's `-R` regex
matches test binary NAMES (not source files), so a new test added
in commit `X` only exists in the build directory's binary after
`cmake --build` re-runs.

**Perché**: ctest on a stale build directory can produce FOUR
distinct misleading signals that look like real test failures:
  1. **"Unable to find executable"** — the binary doesn't exist yet →
     false verdict "test file is broken / rot unfixed"
  2. **Silent pass with zero matches** — ctest exits 0 but the test
     was never run because the binary doesn't exist → false verdict
     "test passes" (rot undetected)
  3. **Match to a stale binary that has been since deleted from
     source** — old test still passes, the new test never runs → false
     verdict "old test still passes" (the actual rot is silent)
  4. **Stale binary from a failed/aborted `cmake --build`** — the
     previous build errored out partway, leaving an executable but
     stale binary that doesn't reflect the current source. The
     `[ "$SRC" -nt "$TEST_BIN" ]` check catches this case (source
     mtime > stale binary mtime); the agent's reported verdict is
     "old test passes" but the actual rot is in the new code that
     never compiled.
All four are §honesty violations: the agent's reported status does
not reflect reality. The pre-ctest staleness check prevents all
four by surfacing the build state BEFORE the ctest invocation
produces a misleading signal.

**Origine**: the TICKET-DOCTEST-SKIP-ROT closure (2026-07-11) ran
`ctest -R chronon3d_pipeline_parity_real_tests --output-on-failure`
on `build/manual-test` which was last built BEFORE commit `6bc43271`
landed the new test file. The result was "Unable to find executable"
— a stale-build artefact that wasted a session before being
correctly diagnosed as "build directory is stale, needs rebuild".
The fix is a pre-ctest invariant: verify the binary exists + is
fresher than its source BEFORE trusting ctest output. This rule
codifies that fix as a permanent lint discipline.

**Scope**: applies to ANY post-source-commit ctest verification,
including rot-fix verification, golden rebake, new-test smoke runs.
Does NOT apply to long-running regression suites (the build is
expected to be current at suite start) or to ctest invocations where
the source mtime is already ≤ binary mtime (i.e., the CORRETTO
check's `[ "$SRC" -nt "$TEST_BIN" ]` returns false — the rule's own
gate is the machine-checkable definition of "build is current").
The pre-existing 11/11 baseline suite after a clean checkout falls
into this exemption.

#### Anti-esempio — ctest on stale build without check

```bash
# ❌ WRONG: ctest on a stale build without staleness check
cd build/manual-test
ctest -R chronon3d_pipeline_parity_real_tests --output-on-failure
# Stale build (built before commit 6bc43271 added the test file) →
# "Unable to find executable" → agent reports
# "test rot unfixed" when the actual issue is "build needs rebuild"
```

#### CORRETTO — verify binary exists + is fresh, then ctest

```bash
# ✅ RIGHT: confirm binary exists + is fresher than source BEFORE ctest
TEST_BIN="build/manual-test/tests/chronon3d_pipeline_parity_real_tests"
SRC="tests/text/test_pipeline_parity_real.cpp"
[ -x "$TEST_BIN" ] || {
  echo "STALE BUILD: $TEST_BIN not found — run cmake --build first" >&2
  exit 1
}
[ "$SRC" -nt "$TEST_BIN" ] && {
  echo "STALE BUILD: $SRC is newer than $TEST_BIN — rebuild required" >&2
  exit 1
}
cd build/manual-test
ctest -R chronon3d_pipeline_parity_real_tests --output-on-failure
```

**Lint-checkability (forward-point)**: a future `tools/check_stale_build_pre_ctest.sh`
(gate opzionale, NON ancora implementato) could auto-detect stale
build state in CI by comparing each test binary's mtime against
its source mtime. The implementation is deferred to a separate
ticket per AGENTS.md v0.1 §regole "Fare PR piccole e mirate" + the
established rule-documentation-precedes-lint-tooling pattern (see
the INFO-level diagnostic style rule's Lint-checkability
forward-point above for the precedent).
### C++ default-arg uniqueness per TU

Il C++ standard (C++23 §11.5.1/2 [dcl.fct.default]/2, equivalente al C++20 §11.3.3) vieta categoricamente di specificare lo stesso argomento di default più di una volta per un medesimo parametro all'interno della stessa translation unit (TU). Questa regola impone che l'argomento di default (es. `= nullptr`) risieda **SOLO** nella dichiarazione primaria della funzione. Le dichiarazioni successive o gli inline stub, come quelli per macro `#ifndef CHRONON3D_ENABLE_DIAGNOSTICS`, **NON DEVONO** duplicare l'assegnazione dell'argomento.

**Perché**: La duplicazione impedisce la compilazione causando silenti rot-pattern quando i build engine-level diagnostics vengono disabilitati. I futuri maintainer sono spesso spinti al copia-incolla delle firme complete (incluso `= nullptr`) dalla primaria allo stub generato. Serve quindi una direttiva esplicita e grep-discoverabile per far interiorizzare formalmente e a priori questa stringente limitazione imposta dal C++.

**Origine**: Regola dedotta dal lineage di chiusura originato dallo stub rot di `check_asset_integrity` (in `include/chronon3d/render_graph/preflight/preflight_render_graph.hpp`-lineage), in concomitanza al fix del commento guardrail introdotto in `src/render_graph/preflight/analysis.hpp` (vedi § `### Fasi 1–4 — Test coverage expansion (V0.2 milestone, PLANNED, this session catchup)` di `docs/FOLLOWUP_TICKETS.md` per il dettaglio). L'intervento sana il rot della build upstream applicando la direttiva standard e mitigando per il futuro un pattern di rottura improvvisa nelle build prive del flag `CHRONON3D_ENABLE_DIAGNOSTICS`.

#### Anti-esempio — duplicate-default-args

**VIETATO (anti-pattern)**

> ```cpp
> // Forward / primary declaration — UNICO punto in cui dichiarare default args:
> void check_asset_integrity(
>     const RenderGraph& graph,
>     const chronon3d::assets::AssetResolver& resolver,
>     GraphPreflightReport& report,
>     chronon3d::preflight::PathExistenceMap* path_cache = nullptr);
>
> // Inline stub sotto #ifndef CHRONON3D_ENABLE_DIAGNOSTICS — ERRORE:
> #ifndef CHRONON3D_ENABLE_DIAGNOSTICS
> inline void check_asset_integrity(
>     const RenderGraph& /*graph*/,
>     const chronon3d::assets::AssetResolver& /*resolver*/,
>     GraphPreflightReport& /*report*/,
>     chronon3d::preflight::PathExistenceMap* /*path_cache*/ = nullptr) {}  // ← ERRORE: default arg duplicato nella stessa TU
> #endif
> ```

**CORRETTO (canonical pattern)**

> ```cpp
> // Forward / primary declaration — UNICO punto in cui dichiarare default args:
> void check_asset_integrity(
>     const RenderGraph& graph,
>     const chronon3d::assets::AssetResolver& resolver,
>     GraphPreflightReport& report,
>     chronon3d::preflight::PathExistenceMap* path_cache = nullptr);
>
> // Inline stub sotto #ifndef CHRONON3D_ENABLE_DIAGNOSTICS — OK:
> #ifndef CHRONON3D_ENABLE_DIAGNOSTICS
> inline void check_asset_integrity(
>     const RenderGraph& /*graph*/,
>     const chronon3d::assets::AssetResolver& /*resolver*/,
>     GraphPreflightReport& /*report*/,
>     chronon3d::preflight::PathExistenceMap* /*path_cache*/) {}  // ← OK: default arg omesso nello stub
> #endif
> ```

**Lint-checkability (forward-point)**: Un futuro gate CI (`tools/check_duplicate_default_arg.sh`, opzionale e non ancora implementato) potrebbe verificare via grep + parse-AST che nessun parametro riceva `= <value>` due volte nella stessa TU tra la dichiarazione primaria di una funzione e una sua ridefinizione inline (es. `inline` stub sotto `#ifndef CHRONON3D_ENABLE_DIAGNOSTICS`). Il pattern, se implementato, deve seguire la convenzione **INFO-level diagnostic style** (`[INFO] <gate-name>: <message>` su PASS addizionale al canonico `GATE_PASS`) già documentata sopra. L'implementazione è deferred a un ticket separato (AGENTS.md v0.1 §regole "Fare PR piccole e mirate" + Cat-3 anti-duplication: il rule documentation precede il lint tooling).

## Workflow Git obbligatorio

```bash
git fetch origin
git checkout main
git pull --ff-only origin main
```

> **Per-branch rebase convention (read-side):** future `git pull` (unpulled) su una branch con `branch.<name>.rebase = true` esegue rebase invece di merge non-fast-forward (history lineare). Verificare lo stato con `git config --local --get-regexp '^branch\.'` — ogni entry per-branch DEVE mostrare `rebase = true`. Impostare idempotentemente con `git config branch.<name>.rebase true` (per-repo local, **NON** `--global`).
>
> Questo è il pezzo **read-side** della triade main-sync hygiene: per-branch rebase (read, qui) + `tools/wrap_push.sh` GATE-MNT-01 (push-side wrapper) + `tools/check_main_clean.sh` (fail-on-dirty gate). Vedi § GATE-MNT-01 in fondo a questo file per il push-side + il gate canonicali (anchor-resilient: nessuna dipendenza da slug di GitHub; il riferimento è plain-text). Su `main` la verifica canonica è: `git config --local --get branch.main.rebase` ⇒ `true`.

Dopo le modifiche:

```bash
git status -sb
git diff
# test mirati
git add <solo-file-modificati>
git commit -m "<tipo(scope): descrizione chiara>"
git push origin main
git log -n 5 --oneline
```


## Quando un file sembra mancare

1. Controllare `git status -sb`.
2. Controllare `git rev-parse HEAD`.
3. Eseguire `git fetch origin`.
4. Confrontare `HEAD` con `origin/main`.
5. Aggiornare il checkout prima di concludere che il file non esiste.

Non creare un nuovo file sostitutivo con nome simile: usare sempre i percorsi canonici sopra.
## GATE-MNT-01 — main-sync fail-on-dirty gate (TICKET-048 closure)

Prima di ogni `git push` su `main` il checkout deve essere gated da `tools/check_main_clean.sh` che fallisce (exit ≠ 0) se:

1. `git fetch origin` non riesce.
2. `HEAD` e `origin/main` sono divergenti (nessuno dei due è antenato dell'altro); FF-pull, post-commit-push e uguaglianza sono tutti accettati (vedi `tools/check_main_clean.sh` per l'implementazione).
3. `git status -s` non vuoto (uncommitted o untracked).
4. `git config --local --get branch.main.rebase` ≠ `true` (per-branch rebase invariant; chiusura GATE-MNT-01-EXT, idempotent + auto-repair via `tools/wrap_push.sh` Step 2.5 + `tools/install_consumer_test.sh` Step 0).

### Wrapper canonico (portabile, tracked)
```bash
tools/wrap_push.sh origin main    # drop-in per `git push`
```
Il wrapper (`tools/wrap_push.sh`) — vedi closure lineage TICKET-048 + TICKET-067/TICKET-075 + **TICKET-076** — esegue in ordine:
  1. `git fetch $REMOTE` + parse args (default `origin` + current branch);
  2. **auto-FF unidirezionale**: se `HEAD` != `$REMOTE/$BRANCH` (può essere disabilitato impostando `CHRONON3D_WRAP_PUSH_AUTO_FF=false` nell'ambiente — escape hatch per preservare commit local-only senza replay automatico) E `is-ancestor HEAD $REMOTE_REF` (cioè `origin/<branch>` è discendente fast-forward-puro di `HEAD`), esegue `git merge --ff-only "$REMOTE/$BRANCH"`. Se FF fallisce (vera divergenza) emette `GATE_FAIL` diagnostico + hint `git pull --rebase`;
  3. invoca `tools/check_main_clean.sh`. Se gate PASS, inoltra `git push "$@"` (drop-in trasparente, tutti gli args originali vengono forwardati). È il punto di ingresso canonico per il workflow Agent3 atomic-commit.

### Hook difensivo (per-repo, local-only)
```bash
.git/hooks/pre-push               # auto-installed local; NON tracked
```
Invocato automaticamente da qualsiasi `git push` (no global git config). Stesso gate: `tools/check_main_clean.sh`. Presente dopo il primo commit di Agent3 in un nuovo clone via questo stesso commit.

### Re-installazione post-clone / post-rebase
```bash
cp .git/hooks/pre-push.example .git/hooks/pre-push 2>/dev/null || true
# oppure usare direttamente `tools/wrap_push.sh origin main` per ogni push
```

### Smoke-test del gate
```bash
tools/check_main_clean.sh   # atteso: GATE_PASS, exit 0  (HEAD==origin/main, clean tree)
```

### Step 1 (GATE-MNT-01-EXT): per-branch rebase verify + auto-repair

**Verify (gate-side, read-enforcement):** `tools/check_main_clean.sh` Step 4 rifiuta con `GATE_FAIL: branch.main.rebase != 'true'` (single-line check: `[ ... ] = "true" || { diagnostic; exit 1; }`).

**Auto-repair (push-side, write-enforcement):** `tools/wrap_push.sh` Step 2.5 imposta `branch.${TARGET_BRANCH}.rebase=true` quando la chiave è **mancante** — idempotent + forward-only (influenza `git pull` futuri). Loggato quando accade (silent mutation = violazione di "non sorprendere l'utente"). I valori espliciti non-`true` sono preservati (esplicito > default operativo).

**Bootstrap (consumer-side):** `tools/install_consumer_test.sh` Step 0 applica la stessa auto-repair su `branch.main.rebase` per il checkout consumer — invocazioni agent post-clone si auto-riparano prima dell'inizio della pipeline install/test.

**Closure lineage:** TICKET-048 (gate wrap) → TICKET-067/075 (merge-base ancestor) → TICKET-076 (auto-FF in wrapper) → **GATE-MNT-01-EXT** (per-branch rebase verify + auto-repair).

**Smoke-test (manuale, presubmit):**
```bash
git config --local --unset branch.main.rebase
tools/check_main_clean.sh     # atteso: GATE_FAIL ... != 'true'
git config branch.main.rebase true
tools/check_main_clean.sh     # atteso: GATE_PASS
tools/wrap_push.sh origin main  # atteso: auto-repair log only on first invocation; push proceeds
```
