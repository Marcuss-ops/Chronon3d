# AGENTS.md — GATE-MNT-01 + Install Pipeline Plumbing (reference)

> **Sede canonica**: `AGENTS.md` §Workflow Git obbligatorio (riepilogo).
> Questo file contiene i dettagli operativi del gate system e degli
> script di plumbing ancillari.

---

## GATE-MNT-01 — main-sync fail-on-dirty gate (TICKET-048 closure)

Prima di ogni `git push` su `main` il checkout deve essere gated da
`tools/check_main_clean.sh` che fallisce (exit ≠ 0) se:

1. `git fetch origin` non riesce.
2. `HEAD` e `origin/main` sono divergenti (nessuno dei due è antenato
   dell'altro); FF-pull, post-commit-push e uguaglianza sono tutti accettati.
3. `git status -s` non vuoto (uncommitted o untracked).
4. `git config --local --get branch.main.rebase` ≠ `true`.

### Wrapper canonico

```bash
tools/wrap_push.sh origin main    # drop-in per `git push`
```

Il wrapper esegue in ordine:
1. `git fetch $REMOTE` + parse args
2. **auto-FF unidirezionale**: se `HEAD` != `$REMOTE/$BRANCH` (disabilitabile
   con `CHRONON3D_WRAP_PUSH_AUTO_FF=false`) E `HEAD` è antenato di
   `$REMOTE_REF`, esegue `git merge --ff-only`. Se FF fallisce emette
   `GATE_FAIL` + hint `git pull --rebase`.
3. invoca `tools/check_main_clean.sh`. Se gate PASS, inoltra `git push "$@"`.

### Hook difensivo

```bash
.git/hooks/pre-push               # auto-installed local; NON tracked
```

Invocato automaticamente da qualsiasi `git push`. Stesso gate:
`tools/check_main_clean.sh`.

### Re-installazione post-clone

```bash
cp .git/hooks/pre-push.example .git/hooks/pre-push 2>/dev/null || true
```

### Smoke-test

```bash
tools/check_main_clean.sh   # atteso: GATE_PASS, exit 0
```

### Step 1 (GATE-MNT-01-EXT): per-branch rebase verify + auto-repair

- **Verify**: `tools/check_main_clean.sh` Step 4 rifiuta con
  `GATE_FAIL: branch.main.rebase != 'true'`.
- **Auto-repair**: `tools/wrap_push.sh` Step 2.5 imposta
  `branch.${TARGET_BRANCH}.rebase=true` quando mancante.
- **Bootstrap**: `tools/install_consumer_test.sh` Step 0 applica
  auto-repair su `branch.main.rebase`.

**Closure lineage**: TICKET-048 → TICKET-067/075 → TICKET-076 →
GATE-MNT-01-EXT.

---

## Per-branch rebase convention (read-side)

`git pull` su branch con `branch.<name>.rebase = true` esegue rebase
invece di merge non-fast-forward.  Verificare:

```bash
git config --local --get branch.main.rebase   # ⇒ true
```

Impostare: `git config branch.main.rebase true` (per-repo, **NON** `--global`).

---

## Install Pipeline Plumbing (Cat-4 ancillary)

Script ancillari per `tools/install_consumer_test.sh` pipeline (categoria 4
del feature freeze: external consumer SDK). Non parte dei gate baseline 11/11.

| Script | Funzione |
|---|---|
| `tools/audit_incomplete_type_pattern.sh` | std::make_shared\<T\> umbrella-header full-def probe. Emette `BROKEN` se l'header canonico del tipo T in `include/chronon3d/` contiene solo `class T;` invece di `struct T { ... }`. |
| `tools/check_clean_rebuild.sh` | Opt-in periodic gate: clean rebuild + cmake + build + ctest smoke. Opt-in via `CHRONON3D_CLEAN_REBUILD=1`. |

### Stand-alone usage

```bash
bash tools/audit_incomplete_type_pattern.sh

# Override scan paths
INCOMPLETE_TYPE_SCAN_PATHS='tests/integration_test/main.cpp src/sdk_consumer/' \
  bash tools/audit_incomplete_type_pattern.sh
```

Exit codes: 0 = clean, 1 = BROKEN rot detected, 2 = internal error.