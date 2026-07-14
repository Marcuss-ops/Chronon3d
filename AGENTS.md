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


### Disciplina di aggiornamento dei canonici

**Non tutti i commit richiedono modifiche ai documenti canonici.** La regola
riduce i conflitti di rebase e il churn documentale imponendo update
minimali e mirati:

| Canonical            | Quando aggiornare                                                    |
| -------------------- | -------------------------------------------------------------------- |
| `CURRENT_STATUS.md`  | **Solo** quando cambia lo stato presente di un'area (PASS/FAIL/NOT RUN). |
| `FOLLOWUP_TICKETS.md`| **Solo** per apertura, chiusura o cambio stato di un blocker.         |
| `CHANGELOG.md`       | **Solo** per chiusure di milestone o cambiamenti visibili all'utente. |
| `ROADMAP.md`         | **Solo** quando la direzione futura (milestone, gate) cambia.         |
| `RELEASE_GATE.md`    | **Solo** quando un requisito permanente di release viene aggiunto, modificato o rimosso. |

**Per i fix piccoli NON aggiornare i canonici.** Commit di tipo
`fix:`, `chore:`, `refactor:` che non cambiano lo stato di un'area,
non aprono/chiudono blocker e non costituiscono una milestone
*non devono* toccare `CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md` o
`CHANGELOG.md`. Esempi:

- `fix(video): replace removed umbrella include` → non tocca canonici
- `chore(git): install tracked repository hooks` → non tocca canonici
- `refactor(glow): ChrononGlowProps dedup + single source of truth` → non tocca canonici
- `fix(tests): migrate rotate_z to canonical rotate(Vec3)` → non tocca canonici

**Per i dettagli tecnici** usa la scheda ticket specifica
(`docs/tickets/TICKET-NNN.md`). Nei canonici conserva **soltanto
una riga sintetica** (stato + riferimento al ticket).

Questo riduce drasticamente i rebase conflittuali e mantiene i
quattro canonici snelli e sempre aggiornati allo stato reale.


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
| `tools/check_clean_rebuild.sh`                  | INSTALL_PIPELINE_PLUMBING   | Opt-in periodic gate: `rm -rf` + `cmake -B configure` + `cmake --build --target` + `ctest --timeout 30 -R <regex>` smoke pipeline (per build dir). Default **no-op + `[INFO]` diagnostic**; opt-in via `CHRONON3D_CLEAN_REBUILD=1`. NOT in standard `tools/wrap_push.sh` pre-push chain (opt-in discipline preserves fast incremental flow; the gate would cost 5-15 min per build dir). Closes the silent-class "rotted artifacts" failure mode that incremental ninja's mtime-bit-flip exit-0 cannot detect (complements AGENTS.md §Post-push SHA-selfcheck invariant by surfacing rot before trust is placed in incremental PASS). |

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

### Post-push SHA-selfcheck invariant (lost-commit prevention)

After every `bash tools/wrap_push.sh origin main` invocation that exits 0, the agent MUST verify a **SHA-triple equality**: `git rev-parse HEAD` (post-push) equals `git rev-parse '@{u}'` (upstream tracking) equals the local SHA captured BEFORE the push invocation. This invariant closes the WRITE-side lost-commit failure mode that bit the project in the `b589fdba` 3-attempt recovery session (TICKET-SOURCE-CONFLICT-MARKERS-ROT §honesty closure).

**Perché**: `tools/wrap_push.sh` returning exit 0 is a NECESSARY but NOT SUFFICIENT signal that the chore landed on `origin/main`. Four distinct failure modes can present an exit-0 verdict while a commit is effectively lost or diverged:
  1. **Auto-FF divergence** (the lost-2nd-attempt pattern): a concurrent agent pushes between `tools/wrap_push.sh` Step 3 (auto-FF unidirectional) and Step 5 (final `git push "$@"`). The local chore commit is rebased OUT by the upstream churn; `git push` exits 0 because there is nothing new to push. The agent reports "pushed" but the chore never reached `origin/main`.
  2. **Stale `@{u}` resolution** after a rebase: `git rev-parse '@{u}'` may transiently resolve to a stale commit SHA (origin's previous HEAD before the upstream churn); equality check returns false-positively, masking the divergence until a later inspection.
  3. **`tools/wrap_push.sh` GATE_FAIL misfire** (rare): Step 4's `check_main_clean.sh` can return exit 0 even when upstream has advanced past the local HEAD because the gate accepts `FF-pull + post-commit-push + uguaglianza` as PASS conditions. The wrapper exit 0 is not a definitive "the commit I just made is now at `origin/main` HEAD" signal.
  4. **Multi-agent race window** (§honesty closure lineage): two agents push concurrently to `origin/main`. The first one's push succeeds; the second one's auto-FF pulls in new commits; the second one's push then exits 0 but the chore is appended AFTER the first's. Without the SHA-triple check, the second agent cannot detect that its chore is one or more commits downstream of where they thought they pushed.
  5. **Unique-edit rebase-preserved vs rebase-dropped**: when the local chore contains a fix UNIQUE to your session, `git pull --rebase origin main` PRESERVES the commit (symmetric to bullet #1's lost-commit pattern); the semantic-identical portion MUST be dropped via `git reset --hard '@{u}'` esplicitamente. (Vedi §Origine **21ece2b3 unique-edit recovery variant (this session, 2026-07-12)** per la narrativa completa — questo bullet è il single-line INDEX entry per Cat-3 anti-dup discipline.)

All four are §honesty violations: the agent's reported "pushed to main" does not match reality. The READ-side triad (**per-branch rebase** + `tools/wrap_push.sh` + `tools/check_main_clean.sh`, the GATE-MNT-01 closure lineage in §GATE-MNT-01 in fondo a questo file) catches most multi-agent-race + stale-@{u} divergences at push time (the wrapper emits `GATE_FAIL: remote is ahead but fast-forward not possible` when auto-FF rejects); the SHA-triple invariant is the WRITE-side belt-and-suspenders for the silent-class failures the read-side triad cannot catch (e.g., when the auto-FF succeeds but the chore's content is silently merged downstream of where the agent intended). The SHA-triple equality invariant surfaces the divergence BEFORE the agent reports completion.

**Origine**: rule synthesized after the `b589fdba` 3-attempt recovery session (2026-07-12) which closed the AGENTS.md §honesty closure of TICKET-SOURCE-CONFLICT-MARKERS-ROT via the macchina-verifica paragraph. In that recovery session: the 1st attempt (`4697a9d9`) was wiped by upstream FF-merge churn between push and verification; the lost-2nd-attempt (a tightening-commit) was raced-out by a concurrent-agent's competing commit `a1835369 feat(check): determinism matrix gate (Test #6)` BEFORE SHA assignment (the tightening attempt itself never received an SHA per the race-win semantics); the 3rd attempt (`b589fdba`) was recovered via the **READ-side triad** (per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh`), NOT via the SHA-triple invariant (which was codified only in the next-chore commit `4cfceca9` and was NOT yet executed at the `b589fdba` recovery moment). The SHA-triple invariant codifies the WRITE-side belt-and-suspenders discipline for the silent-class failures the read-side triad cannot catch at the push boundary: capture local SHA pre-push, push via the canonical wrapper, verify SHA-triple equality post-push. Cross-link: the pre-existing **GATE-MNT-01 closure lineage** (`TICKET-048` + `TICKET-067`/`TICKET-075` + `TICKET-076` + `GATE-MNT-01-EXT`) documente in §GATE-MNT-01 in fondo a questo file — that triad is the READ-side complement; the SHA-selfcheck is the WRITE-side gate that closes the silent-class lost-commit failure mode.

**21ece2b3 unique-edit recovery variant (this session, 2026-07-12)**: a 2nd variant of the recovery pattern emerged in this session via commit `21ece2b3` (the rotate_z → rotate(Vec3) canonical-API update on tests/cache/test_cache_invariance.cpp:119, originally committed locally as `4028f6cc` then rebase-orphaned by a race-window concurrent-agent push with an equivalent semantic change). The canonical resolution sequence was: (a) `git reset --hard '@{u}'` to drop the semantic-identical-to-upstream block (upstream had already replicated the rotate_z canonical-API change via a different commit path); (b) cherry-pick the UNIQUE non-conflicting portion — the docstring-only line specifying which canonical-API form was used (content-distinct from upstream's replication); (c) commit fresh as `21ece2b3` atop the new upstream; (d) auto-FF preservation propagated the unique edit. The lesson is **unique-edit recovery**: quando il commit locale contiene una vera fix UNIQUE alla sessione (vs semantic-equivalent a upstream), `git pull --rebase origin main` PRESERVA il commit invece di drop-oramelo; la parte semantic-identical a upstream DEVE essere droppata via `git reset --hard '@{u}'` (literal documentation of the drop-oramelo form, non instruction to execute) per evitare double-application in rebase-replay o cherry-pick successivi. The `21ece2b3` variant complements the `b589fdba` precedent on the silent-class lost-commit failure mode by demonstrating an explicit `reset --hard '@{u}'` drop-and-reapply template for the unique-content subcase. Per the SHA cite pattern rule above, both SHAs `4028f6cc` and `21ece2b3` are cited inline at their semantic role boundary.

**Scope**: applies to EVERY `git push` invocation on `main` (and to all branches where `@{u}` resolves to a remote-tracked branch). Does NOT apply to:
  - Pushes during a rebase troubleshooting cycle: capture `git rev-parse HEAD@{1}` (the pre-rebase SHA) instead of the canonical `git rev-parse HEAD`.
  - The auto-FF unidirectional path inside `tools/wrap_push.sh` itself (Step 3): the wrapper has its own SHA capture mechanism + GATE_FAIL diagnostic on non-FF-able divergence. The rule applies AFTER the wrapper returns; the wrapper's internal exit codes are NOT a substitute for the SHA-triple check.
  - Local amendments before the very first push of a chore (when there's no upstream tracking yet); the post-push equality check is vacuously true (`HEAD == @{u}` trivially).
Specifically: every `bash tools/wrap_push.sh origin main` invocation MUST be followed by the SHA-triple equality check before the agent reports "pushed".

#### Anti-esempio — trust exit-0 without SHA selfcheck

```bash
# ❌ WRONG: trust exit-0 without SHA selfcheck
git commit -m "docs(state): post-FF 11/11 green + Phase 1 rot resolved" -m "..."
bash tools/wrap_push.sh origin main    # exit 0 ⇒ assumes push succeeded
echo "pushed — done"                  # ← SBAGLIATO: exit 0 alone is INSUFFICIENT
# Lost-commit pattern: a concurrent agent pushed between auto-FF and final push.
# The chore was rebased-out; the wrapper exit 0 is the AUTO-NOTHING-TO-PUSH
# signal, NOT a confirmation that the chore reached origin/main.
```

#### CORRETTO — SHA-triple equality post-push

```bash
# ✅ RIGHT: capture pre-push + push + SHA-triple equality verify
LOCAL_SHA="$(git rev-parse HEAD)"                              # capture pre-push
bash tools/wrap_push.sh origin main                           # forward push via wrapper
POSTPUSH_SHA="$(git rev-parse HEAD)"
UPSTREAM_SHA="$(git rev-parse '@{u}')"
[ "$LOCAL_SHA" = "$POSTPUSH_SHA" ] \
  && [ "$POSTPUSH_SHA" = "$UPSTREAM_SHA" ] \
  || { echo "SHA MISMATCH: lost-commit pattern detected" >&2; \
       echo "  local    = $LOCAL_SHA" >&2; \
       echo "  postpush = $POSTPUSH_SHA" >&2; \
       echo "  upstream = $UPSTREAM_SHA" >&2; \
       exit 1; }
echo "pushed — chore SHA verified on origin/main"
```

**Lint-checkability (forward-point)**: a future `tools/check_post_push_consistency.sh` (gate opzionale, NON ancora implementato) could auto-verify the discipline by scanning recent `git reflog` entries: if the last `push` reflog entry was preceded by a commit `C` whose SHA does NOT appear in the current `origin/main` reflog, emit `GATE_FAIL: post-push SHA-mismatch detected — chore <SHA> lost between local and upstream`. The gate would implement the SHA-triple equality check as a CI invariant that complements the agent-side discipline. Implementation deferred per AGENTS.md v0.1 §regole "Fare PR piccole e mirate" + the rule-documentation-precedes-lint-tooling pattern (see INFO-level diagnostic style rule's Lint-checkability forward-point above for the precedent). Per AGENTS.md §GATE-MNT-01 closure lineage, this future gate sits on the WRITE-side triad complement (per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` are the READ-side triad; the SHA-selfcheck is the WRITE-side discipline that closes the lost-commit failure mode that bit the `b589fdba` recovery).

### Docs canonical update discipline rule

Ogni cronaca estesa di fix piccolo (`fix include`, `rename`, `initializer`, `cleanup di test`, `chore:` non-milestone) vive SOLO nella scheda ticket dedicata (`docs/tickets/TICKET-NNN.md`). I 4 file canonical (`CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md`, `CHANGELOG.md`, `ROADMAP.md`) conservano al massimo **una riga sintetica** (stato + riferimento al ticket). La tabella scope della sezione [### Disciplina di aggiornamento dei canonici](#disciplina-di-aggiornamento-dei-canonici) (sopra) è canonica; la presente rule la ESPLICITA in forma lint-discipline Cat-3 anti-dup.

**Perché**: la cronaca estesa nei canonici causa quattro classi di rot §honesty-violation:
  1. **Rebase rot-class**: narrative novel in `CHANGELOG.md` diverge a ogni `git pull --rebase`, generando conflict markers che mascherano la rot reale (precedent: `874b5b37` + `b589fdba` lost-commit pattern, vedi §Post-push SHA-selfcheck rule sopra).
  2. **Cat-3 anti-duplication violation**: la stessa narrative in 3+ file canonical → nessuno `rg`-canonical → drift inevitabile al prossimo edit (la stessa informazione vive in 3 posti, nessuno canonico).
  3. **Stato-trust degrado**: `CURRENT_STATUS.md` con narrative di commit invece di sintesi impedisce di distinguere "stato di main ORA" da "storia" → file non-`rg`-queryable per forward-state queries.
  4. **`docs/FOLLOWUP_TICKETS.md` growth-class**: la policy `≤10 righe open` decade quando ogni riga contiene narrative multi-line dal ticket body; il fail-loud recede per inflazione.

**Origine**: rule formalizzata per escalation della pre-esistente `### Disciplina di aggiornamento dei canonici` table — che già dichiara lo scope per canonical — in forma di lint-discipline rule esplicita con anti-esempi + Lint-checkability forward-point, onorando il principio Cat-3 anti-duplication (1 sede canonica per ogni informazione) e il ticket-home per i dettagli tecnici dei fix piccoli. La rule è il canonical enforcement-side dei 3-doc deferred obligations già in essere (paralleli a [TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN](docs/FOLLOWUP_TICKETS.md) + [TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN](docs/FOLLOWUP_TICKETS.md) + [TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN](docs/FOLLOWUP_TICKETS.md)) che stabiliscono il pattern `body commit + CHANGELOG prepended + CURRENT_STATUS/FOLLOWUP deferred a dedicated Cat-5 ticket row`.

**Scope**: si applica a OGNI commit chore su `main` (inclusi `fix:`, `chore:`, `refactor:`, `docs:`, `test:`, e `feat:` minori non-milestone). NON si applica a: schede ticket (`docs/tickets/TICKET-NNN.md` — la cronaca home permissive per ticket contract); ADR (`docs/adr/ADR-NNN-<titolo>.md` — decision rationale indipendente per `docs/DOCUMENTATION_GOVERNANCE.md` §adr); baseline (`docs/baselines/main-<sha>-baseline.md` — il macchina-verification artifact È il detail-content per definizione); CHANGELOG milestone entries che citano il ticket ma NON ne replicano il body (Cita-Only pattern). Specificamente: ogni chore commit DEVE verificare `git diff origin/main..HEAD -- docs/CURRENT_STATUS.md docs/FOLLOWUP_TICKETS.md docs/CHANGELOG.md docs/ROADMAP.md` per assenza di cronaca >1 riga sintetica — più-di-1-riga DEVE essere in `docs/tickets/TICKET-NNN.md`.

#### Anti-esempio — cronaca lunga in 3 canonici

**VIETATO (stesso diff replicato in 3 file = Cat-3 anti-dup violation)**:

> `docs/CURRENT_STATUS.md`:
> ```markdown
> Camera V1: ...
> - Il 2026-07-12 l'agente ha committato il fix rotate_z → rotate(Vec3) su
>   tests/cache/test_cache_invariance.cpp:119 con subject `fix(cache): migrate
>   rotate_z` (62 char). Cat-3 minimal-surface (no new SDK API). Test rebuild
>   DEFERRED-WBH. Forward-point: TICKET-CACHE-ROTATE-Z-MACHINE-VERIFY.
> ```
> `docs/FOLLOWUP_TICKETS.md` (riga `TICKET-CACHE-ROTATE-Z-MIGRATION`):
> ```markdown
> ### TICKET-CACHE-ROTATE-Z-MIGRATION
> Storia: il 2026-07-12 l'agente ha committato `fix(cache): migrate rotate_z`
> su tests/cache/test_cache_invariance.cpp:119, rotate_z sostituito con
> rotate(Vec3) (canonical API). Test rebuild DEFERRED-WBH. Subject envelope
> 62 char ≤ 72. Forward-point: TICKET-CACHE-ROTATE-Z-MACHINE-VERIFY.
> ```
> `docs/CHANGELOG.md`:
> ```markdown
> ## 2026-07-12
> ### fix(cache): migrate rotate_z
> Cambiato tests/cache/test_cache_invariance.cpp:119 da `rotate_z()` (legacy
> API) a `rotate(Vec3)` (canonical API). Cat-3 minimal-surface (no new SDK
> API). Test rebuild DEFERRED-WBH. Subject envelope 62 char ≤ 72.
> Forward-point: TICKET-CACHE-ROTATE-Z-MACHINE-VERIFY.
> ```

**CORRETTO — 1 canonical line + body in ticket scheda (cronaca nel ticket)**:

> `docs/CHANGELOG.md`:
> ```markdown
> ## 2026-07-12
> ### `fix(cache): migrate rotate_z`
>   ([TICKET-CACHE-ROTATE-Z-MIGRATION](docs/tickets/TICKET-CACHE-ROTATE-Z-MIGRATION.md))
> Migrate `rotate_z` → `rotate(Vec3)` su tests/cache/test_cache_invariance.cpp:119.
> Cat-3 minimal-surface.
> ```
> `docs/tickets/TICKET-CACHE-ROTATE-Z-MIGRATION.md` (cronaca estesa libera):
> ```markdown
> # TICKET-CACHE-ROTATE-Z-MIGRATION — Migrate rotate_z to canonical API
> 
> ## Stato: DONE (2026-07-12, commit <sha>)
> 
> ## Problema
> `lb.rotate_z()` chiamava API legacy rimossa; sostituzione con
> `lb.rotate(Vec3)` (canonical API).
> 
> ## Evidenza
> - File: tests/cache/test_cache_invariance.cpp:119 (1-line docstring update)
> - Compilazione: invariata (no new SDK API)
> - Strict-gate rg: `\.rotate_z\s*\(` tests content → 0 match post-commit
> 
> ## Criteri di accettazione
> - Strict-gate 0 match (verified)
> - 11/11 baseline suite verde post-commit (verified, see TICKET-VERIFY-...)
> 
> ## Forward-points
> TICKET-CACHE-ROTATE-Z-MACHINE-VERIFY: working build host ctest run.
> ```
> `docs/CURRENT_STATUS.md` + `docs/FOLLOWUP_TICKETS.md` rimangono UNTOUCHED (no
> canonical state change, no blocker, no milestone closure).

**Lint-checkability (forward-point)**: un futuro `tools/check_docs_canonical_discipline.sh` (gate opzionale, NON ancora implementato) potrebbe verificare via `awk` + JSONL-grammar parse che ogni entry `## YYYY-MM-DD` di `docs/CHANGELOG.md` non superi la soglia di righe-per-entry (canonical config: ≤6 punti per ticket per `docs/DOCUMENTATION_GOVERNANCE.md` §CHANGELOG "Limite raccomandato: da tre a sei punti per ticket") + che ogni entry abbia al massimo 1 link a `docs/tickets/TICKET-NNN.md` come espansione canonica + che `git diff --numstat <prev-changelog-sha>..HEAD -- docs/CURRENT_STATUS.md docs/ROADMAP.md` non superi N righe per chore commit (canonical config: ≤4 righe/state-per-area). L'implementazione è deferred a un ticket separato (AGENTS.md v0.1 §regole "Fare PR piccole e mirate" + regola-documentation-precedes-lint-tooling pattern, come per i `Lint-checkability` forward-point delle altre 5 rules in `## Regole di lint documentale`).

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
