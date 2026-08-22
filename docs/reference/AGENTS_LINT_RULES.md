# AGENTS.md — Regole di lint documentale (reference)

> **Sede canonica**: `AGENTS.md` §Regole di lint documentale (riepilogo rapido).
> Questo file contiene la versione ESTESA con origini, anti-esempi e forward-point.

---

## SHA cite pattern (inline-only rule)

Quando lo stesso commit SHA deve apparire in una sezione (es. §References) o
nel corpo di un testo, preferire **sempre** la citazione narrativa inline
rispetto a una voce di catalogo standalone separata.

**Perché**: la citazione inline trasporta simultaneamente il ruolo semantico
E fornisce `rg`-discoverability. Le voci standalone creano duplicazione
dello stesso SHA, che richiede deduplicazione a valle.

**Origine**: regola dedotta dai commit `3febd8cd` e `4cded60e` (lineage di dedup
di ADR-020).

### Anti-esempio — SHA cite duplication

**VIETATO (duplicato standalone)**

> - Commit `1a2b3c4d` (correzione del regression-lock).
> - Risolto nel commit `1a2b3c4d` ripristinando il bounding box.

**CORRETTO (singolo inline)**

> - Risolto nel commit `1a2b3c4d` (correzione del regression-lock ripristinando il bounding box).

---

## INFO-level diagnostic style (gates)

Per i gate CI (`tools/check_*.sh`) che emettono un singolo messaggio informativo
addizionale sullo stato clean (in aggiunta al canonico `GATE_PASS` o `OK:`),
usare il formato:

```
[INFO] <gate-name>: <message>
```

dove:
- `<gate-name>` = basename dello script senza estensione `.sh`
- `<message>` = una sola riga, ≤ 200 caratteri
- **emissione**: solo su PASS, come riga addizionale al `GATE_PASS` — MAI sul FAIL

**Scope**: gate NUOVI (post-commit). I 14 gate esistenti restano invariati.

### Anti-esempio

```bash
# VIETATO: prefisso senza square brackets
echo "INFO: check_test_suite_registration: clean state"

# VIETATO: sul FAIL path
if [ "$raw_count" -gt 0 ]; then
    echo "[INFO] check_test_suite_registration: $raw_count raw add_executable remain"  # SBAGLIATO
    exit 1
fi
```

**CORRETTO**:

```bash
GATE_NAME=check_test_suite_registration
# ... loop di audit ...
if [ "$raw_count" -gt 0 ]; then
    echo "GATE_FAIL: $raw_count raw add_executable" >&2; exit 1  # FAIL path invariato
fi
echo "GATE_PASS: 0 raw add_executable"                             # PASS canonico
echo "[INFO] ${GATE_NAME}: clean state (all $suite_count suites verified)"  # INFO addizionale
```

---

## Test binary staleness check (pre-ctest invariant)

Prima di `ctest -R <pattern>` su un test file aggiunto/modificato di recente,
verificare SEMPRE che il binary esista nella build directory E sia più recente
del source.

**Perché**: ctest su build stale può produrre 4 segnali fuorvianti:
1. "Unable to find executable" → falso negativo "test rotto"
2. Zero matches → falso positivo "test passa"
3. Match a binary rimosso dal source → falso segnale
4. Binary stale da build abortita → rot non rilevata

**Origine**: TICKET-DOCTEST-SKIP-ROT closure (2026-07-11).

### Anti-esempio

```bash
# ❌ WRONG
cd build/manual-test
ctest -R chronon3d_pipeline_parity_real_tests --output-on-failure
```

### CORRETTO

```bash
TEST_BIN="build/manual-test/tests/chronon3d_pipeline_parity_real_tests"
SRC="tests/text/test_pipeline_parity_real.cpp"
[ -x "$TEST_BIN" ] || { echo "STALE BUILD: binary not found" >&2; exit 1; }
[ "$SRC" -nt "$TEST_BIN" ] && { echo "STALE BUILD: source newer than binary" >&2; exit 1; }
cd build/manual-test
ctest -R chronon3d_pipeline_parity_real_tests --output-on-failure
```

---

## C++ default-arg uniqueness per TU

L'argomento di default (es. `= nullptr`) risiede **SOLO** nella dichiarazione
primaria. Stub inline (`#ifndef CHRONON3D_ENABLE_DIAGNOSTICS`) **NON DEVONO**
duplicare l'assegnazione.

**Perché**: C++ standard vieta default arg duplicati nella stessa TU.
La duplicazione causa silent rot quando i diagnostics vengono disabilitati.

**Origine**: stub rot di `check_asset_integrity` in
`include/chronon3d/render_graph/preflight/preflight_render_graph.hpp`.

### Anti-esempio

```cpp
// ❌ ERRORE: default arg duplicato nello stub
void check_asset_integrity(..., PathExistenceMap* path_cache = nullptr);  // primaria

#ifndef CHRONON3D_ENABLE_DIAGNOSTICS
inline void check_asset_integrity(..., PathExistenceMap* /*path_cache*/ = nullptr) {}  // ← ERRORE
#endif
```

### CORRETTO

```cpp
void check_asset_integrity(..., PathExistenceMap* path_cache = nullptr);  // primaria

#ifndef CHRONON3D_ENABLE_DIAGNOSTICS
inline void check_asset_integrity(..., PathExistenceMap* /*path_cache*/) {}  // ← OK: default arg omesso
#endif
```

---

## Post-push SHA-selfcheck invariant (lost-commit prevention)

Dopo ogni `bash tools/wrap_push.sh origin main` che esce 0, verificare
**SHA-triple equality**: `HEAD` (post-push) == `@{u}` (upstream) == SHA
locale catturato PRIMA del push.

**Perché**: 5 classi di failure possono dare exit-0 con commit perso:
1. Auto-FF divergence (concurrent agent between auto-FF and push)
2. Stale `@{u}` resolution
3. `tools/wrap_push.sh` GATE_FAIL misfire
4. Multi-agent race window
5. Unique-edit rebase-preserved vs rebase-dropped

**Origine**: `b589fdba` 3-attempt recovery session (2026-07-12).

### Anti-esempio

```bash
# ❌ WRONG: trust exit-0
git commit -m "..." && bash tools/wrap_push.sh origin main && echo "pushed — done"
```

### CORRETTO

```bash
LOCAL_SHA="$(git rev-parse HEAD)"
bash tools/wrap_push.sh origin main
POSTPUSH_SHA="$(git rev-parse HEAD)"
UPSTREAM_SHA="$(git rev-parse '@{u}')"
[ "$LOCAL_SHA" = "$POSTPUSH_SHA" ] && [ "$POSTPUSH_SHA" = "$UPSTREAM_SHA" ] \
  || { echo "SHA MISMATCH: lost-commit pattern detected" >&2; exit 1; }
echo "pushed — chore SHA verified on origin/main"
```

---

## Docs canonical update discipline rule

Cronaca estesa di fix piccolo vive SOLO nella scheda ticket
(`docs/tickets/TICKET-NNN.md`). I 4 canonici conservano al massimo
**una riga sintetica** (stato + riferimento al ticket).

**Perché**: cronaca estesa nei canonici causa 4 classi di rot:
1. Rebase rot-class (conflict markers su `CHANGELOG.md`)
2. Cat-3 anti-duplication (stessa info in 3+ file → drift)
3. Stato-trust degrado (narrative vs sintesi in `CURRENT_STATUS.md`)
4. `FOLLOWUP_TICKETS.md` growth-class (policy ≤10 righe decade)

**Scope**: OGNI commit chore su `main`. Cronaca → ticket. Canonici → max 1 riga.

---

## 2×-in-one-chore: deprecation reversal bundles (Cat-3 anti-dup)

Rimozione di `[[deprecated]]` DEVE raggruppare atomicamente: source change +
apertura forward-point ticket in `docs/FOLLOWUP_TICKETS.md` +
`docs/tickets/TICKET-NNN.md` + ≤1 riga CHANGELOG.

**Perché**: il disaccoppiamento apre 3 classi di rot:
1. Orphan-intent (migration intent perso nella git history)
2. Race-window ticket-open (commit 2 raced-out da upstream churn)
3. Cat-5 3-doc alignment break

**Origine**: `21ece2b3` unique-edit recovery variant (2026-07-12).