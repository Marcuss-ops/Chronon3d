# TICKET-GATE-10-PHASE-4-BLACK-FU4 — install_consumer TextRunShape incomplete-type (forward-decl + std::make_shared rot)

| Field           | Value |
|-----------------|-------|
| ID              | TICKET-GATE-10-PHASE-4-BLACK-FU4 |
| Priorità        | P0 |
| Area            | install_consumer SDK Phase 4 subgate (cat-1 build rot + cat-3 legacy umbrella) |
| Stato           | OPEN |
| Wrapper         | `tests/install_consumer/main.cpp:147` (`std::make_shared<c3d::TextRunShape>()`) |
| Lineage         | `TICKET-M1.5#7-AUDIT` (forward-decl + `sizeof`/`make_shared` on incomplete-type pattern) |
| Parent ticket   | [`TICKET-GATE-10-PHASE-4-BLACK`](TICKET-GATE-10-PHASE-4-BLACK.md) (DONE) |
| Anchor commit   | `09e09beb` — M1.5#9 — PNG save_png top-end quantization (PRIMO sotto-blocco risolto) |
| Bloque baseline | 11/11 → non raggiungibile senza questo fix |

## Stato (one-line)

`OPEN | IN PROGRESS | BLOCKED | DONE` → **OPEN** alla scoperta (cfr. storico sotto).

## Priorità

P0 — gate #10 (`tools/install_consumer_test.sh`) resta FAIL post-M1.5#9 perché esiste un **secondo** sotto-blocco di rot in `tests/install_consumer/main.cpp:147`, lineage `M1.5#7-AUDIT`. Senza questo fix la baseline verde 11/11 non è raggiungibile, quindi il feature-freeze V0.1 non può essere revocato.

## Problema

`tools/install_consumer_test.sh` (gate #10 dell'audit 11-gate) esegue il build del consumer SDK contro l'header pubblico esposto dal target `chronon3d_sdk_overrides` + `chronon3d_public_headers`. L'audit rieseguito a `main@c9415e5b` (post-M1.5#8 ma pre-M1.5#9) ha rivelato **due** cause concomitanti di FAIL su gate #10:

| Sotto-blocco | Stato |
|--------------|-------|
| (A) PNG save_png top-end off-by-one in `image_writer.cpp::save_png()` | ✅ **DONE** @ `main@09e09beb` (M1.5#9) |
| (B) Forward-decl + `std::make_shared` rot in `tests/install_consumer/main.cpp:147` | ❌ **OPEN** (questo ticket = FU4) |

### Causa specifica di (B)

`tests/install_consumer/main.cpp:147` istanzia:

```cpp
auto modern_shape = std::make_shared<c3d::TextRunShape>();
```

Il tipo `TextRunShape` è:

- ✅ **definito completamente** in `include/chronon3d/text/text_run_shape.hpp` (full struct layout);
- 🔁 **forward-declared** in `include/chronon3d/scene/model/shape/shape.hpp` e `include/chronon3d/scene/builders/text_run_builder.hpp` (e altri visitabili via grep sweep).

Il `std::make_shared<c3d::TextRunShape>()` richiede il **complete type** al call site (compiler deve conoscere layout + allineamento per allocare storage + invocare ctor / dtor).

**Problema contratto consumer SDK**: il consumer (eseguito da `install_consumer_test.sh`) vede il tipo attraverso l'**umbrella header pubblico** esposto dal target installato (`Chronon3D::SDK`). Il path `include/chronon3d/text/text_run_shape.hpp` **NON** è transitivamente esposto dall'umbrella, quindi dal punto di vista del consumer il tipo `c3d::TextRunShape` risulta forward-declared (o invisible) → compile error GCC 15+:

```text
error: invalid application of 'sizeof' to incomplete type 'chronon3d::TextRunShape'
   147 |     auto modern_shape = std::make_shared<c3d::TextRunShape>();
      |                                        ^
note: in instantiation of function template specialization
      'std::make_shared<TextRunShape, ...>'
```

Nota: il build fallisce con `error: invalid application of 'sizeof'` (anche se la riga contiene `make_shared` e non un esplicito `sizeof`) — perché il compilatore evidenzia il **problema di incomplete type** nella template metaprogramming di `std::make_shared<T>`. È la stessa radice GCC-15-level del lineage `M1.5#7-AUDIT`.

### Impatto a cascata su altri consumer

Il rot non è limitato a `tests/install_consumer/main.cpp`: ogni consumer esterno che crea `std::make_shared<TextRunShape>()` o `std::unique_ptr<TextRunShape>` contro `Chronon3D::SDK` umbrella header riporterebbe lo stesso errore di compile. Quindi FU4 NON è solo un ticket di test — è un **bug di contratto SDK pubblico**.

## Evidenza

### File evidence

- File rotto: `tests/install_consumer/main.cpp:147`
- File con la full definition (NON transitivamente esposto al consumer): `include/chronon3d/text/text_run_shape.hpp`
- File forward-declaring (sì transitivamente esposti): `include/chronon3d/scene/model/shape/shape.hpp`, `include/chronon3d/scene/builders/text_run_builder.hpp`
- Tentativo include parziale osservato in `tests/install_consumer/main.cpp:1-30`: nessun `#include <chronon3d/text/text_run_shape.hpp>` esplicito.

### Command evidence (audit rieseguito a `main@c9415e5b`, pre-M1.5#9)

```bash
cd /home/pierone/Pyt/Chronon3d
git fetch origin
git checkout c9415e5b  # detached HEAD
bash tools/install_consumer_test.sh > /tmp/gate10_audit.log 2>&1
```

Output del grep mirato sul log:

```text
error: invalid application of 'sizeof' to incomplete type 'chronon3d::TextRunShape'
   147 |     auto modern_shape = std::make_shared<c3d::TextRunShape>();
exit code: 1
```

Log conservato in `/tmp/gate10_audit.log` (catturato durante il debug session di questo turno, prima della fix commit `09e09beb`).

### Anchor commit (sotto-blocco A risolto)

```bash
git log --oneline -n 1 main@09e09beb
# expected: 09e09beb fix(backends/image): M1.5#9 / TICKET-GATE-10-PHASE-4-BLACK — PNG save_png top-end quantization
```

### SHA progression (post-M1.5#8 → post-M1.5#9 → pre-FU4)

| SHA       | Ticket                              | Stato gate #10 |
|-----------|-------------------------------------|----------------|
| `c9415e5b` | M1.5#8 atomic (diagnostic + cat-1/cat-3 + diagnostic wiring) | FAIL PNG-off-one + FAIL incomplete-type |
| `09e09beb` | M1.5#9 atomic (image_writer.cpp fix) | FAIL solo incomplete-type |
| `<open>`   | **FU4 (questo ticket)** — umbrella header expose | atteso PASS |

## Impatto

| Area                                          | BEFORE (c9415e5b) | post-M1.5#9 (09e09beb) | post-FU4 (atteso) |
|-----------------------------------------------|-------------------|-----------------------|-------------------|
| Gate #10 audit                                | FAIL PNG + incomplet | FAIL incomplet-type only | **PASS** |
| Consumer SDK compile (umbrella header)      | ❌ broken          | ❌ broken              | ✅ compilable     |
| `std::make_shared<TextRunShape>()` da consumer | error GCC 15+     | error GCC 15+         | compiles cleanly  |
| Feature-freeze V0.1 revocation path          | blocked           | blocked               | unblocked IF anche FU1/FU2/FU3 chiusi |

## Confine

**In scope** (delivered by FU4):

- Decidere e applicare il fix-path per esporre `include/chronon3d/text/text_run_shape.hpp` al consumer SDK umbrella.
- Verificare che `cmake/Chronon3DPublicHeaders.cmake` includa il path nel manifest pubblico.
- Verificare compatibilità con le altre shadow-fwd-decls (`shape.hpp`, `text_run_builder.hpp`) — nessuna regressione attesa (sono semplici ricompilazioni dello shadow).
- Re-run `bash tools/install_consumer_test.sh` in isolation → atteso PASS.
- Re-run full 11-gate audit → atteso 11/11 PASS.
- 1 commit atomico su `main` (doc + fix eventualmente nello stesso commit O due commit separati, vedi Soluzione accettabile).
- Push via `tools/wrap_push.sh` GATE-MNT-01.

**Out of scope** (continuazione del lavoro FU-n lineage):

- FU1 — latent premul alpha rot in `image_writer.cpp::save_png` (semi-transparent PNG outputs).
- FU2 — timeline V2 migration per i 4 call sites deprecated per rimuovere `add_compile_options(-Wno-error=deprecated-declarations)` from root `CMakeLists.txt`.
- FU3 — idiomatic LUT-path refactor (`to_srgb()` + 4× cast → `Color::linear_to_srgb8(...)`).
- Feature-freeze revocation (`AGENTS.md` 🔴 Feature Freeze section removal at first 11/11 baseline verde).

## Soluzione accettabile

### Option-preferred (scelta canonica)

1. Aggiungere `#include <chronon3d/text/text_run_shape.hpp>` al file `include/chronon3d/scene/model/shape/shape.hpp` (shadow fwd-decl consumer-visible). Vantaggi: il consumer vede TextRunShape transitivamente senza modificare il consumer-side.
2. **OPPURE** (fallback se Option-preferred rompe catena compile-time): aggiungere path a `cmake/Chronon3DPublicHeaders.cmake` così che `text_run_shape.hpp` sia installato sotto `<prefix>/<includedir>/chronon3d/text/text_run_shape.hpp` e accessibile dal consumer. (Meno preferito perché modifica la lista "manifest-clean" accettata dal `install_consumer_test.sh` Phase 1.)

### Vincoli

- Il consumer-side (`tests/install_consumer/main.cpp`) non deve rompere l'invariante "manifest-clean": può includere SOLO header dichiarati nel manifest pubblico.
- Il fix NON deve ampliare la superficie pubblica API in modo non-mi­nimale (Feature freeze V0.1 cat-5 in AGENTS.md).
- Il fix NON deve generare nuove `#include` circolari — `text_run_shape.hpp` non deve includere a sua volta `shape.hpp` reciprocamente.
- Il fix NON deve rimuovere alcun forward-decl esistente (shadow fwd-decls sono OK come secondo livello di visibility).

### Trade-off documentale

| Path | Pro | Contro |
|------|-----|--------|
| Option-preferred: include in `shape.hpp` | 1 riga modificata, idempotente, no manifest exposure shift | aumenta compile-time in TUs che includono shape.hpp (già molti di loro, marginal cost) |
| Fallback: add to `Chronon3DPublicHeaders.cmake` | preserva backward-compat ABI per shadow fwd-decl consumers | aumenta publicly-exposed API surface (va motivato in ADR se scelto) |

**Decisione richiesta dal fixer al commit time**: scegliere uno dei due path. Default suggerito: **Option-preferred** (più conservativa + idempotente + storicamente già testata in altri forward-decl + full-type aggregations nel codebase Chronon3d).

## Criteri di accettazione

- ✅ (futuro, post-fix) `bash tools/install_consumer_test.sh` in isolation → exit 0.
- ✅ (futuro, post-fix) 11-gate audit full → 11/11 PASS sullo stesso commit.
- ✅ `git log --oneline -n 1` mostra un commit atomico su `main` con messaggio che cita questo TICKET-ID.
- ✅ Doc-sync nello stesso commit (o commit successivo a catena, ma stesso feature-lifecycle):
  - Questo ticket `TICKET-GATE-10-PHASE-4-BLACK-FU4.md` ← Stato aggiornato da OPEN → DONE a chiusura.
  - `docs/FOLLOWUP_TICKETS.md` ← riga FU4 rimossa dall'indice (marcata come risolta).
  - `docs/CURRENT_STATUS.md` ← riga "Backends / SDK Phase 4" aggiornata a `PASS @ <new-SHA>` (anchor M1.5#9@09e09beb ancora valido per PNG, anchor FU4 per incomplet-type).
  - `docs/CHANGELOG.md` ← entry FU4 (3-6 bullet points canonical).
- ✅ `git push origin main` via `tools/wrap_push.sh` GATE-MNT-01 (con `check_main_clean.sh` PASS).

## Cross-references

### Parent (DONE)

- [`docs/tickets/TICKET-GATE-10-PHASE-4-BLACK.md`](TICKET-GATE-10-PHASE-4-BLACK.md) — ticket superiore DONE (catena M1.5#8 → M1.5#9 → FU1 FU2 FU3 proposti).

### Anchor commit (sotto-blocco A PNG)

- `main@09e09beb` — M1.5#9 — fix(backends/image): PNG save_png top-end quantization — risolve il PRIMO sotto-blocco del FAIL di gate #10 (PNG off-by-one). SHA canonico da citare come anchor "PNG già risolto".

### Lineage (stesso pattern compile-time rot)

- [`docs/tickets/TICKET-M1.5#7-AUDIT.md`](TICKET-M1.5#7-AUDIT.md) — audit per il pattern `forward-decl + sizeof/make_shared<incomplete-type>` (proactive diagnostic tool `tools/audit_incomplete_type_pattern.sh`). FU4 è istanza nota di questo pattern nel consumer-side `tests/`.
- [TICKET-M1.5#7-CASE-2](../tickets/) — caso storico chiuso (commit `c9415e5b`): rot analogo tra `software_session_resources.hpp` + `TextRenderResources` in `runtime_adapter.cpp`. Pattern lineare: forward-decl in header + full-type non incluso nel .cpp TU proprietario → GCC 15+ compile error.

### Forward dependencies (sotto FU4, conditionali su 11/11 baseline)

- `TICKET-GATE-10-PHASE-4-BLACK-FU1` — premul alpha (semi-trans PNG), aperto.
- `TICKET-GATE-10-PHASE-4-BLACK-FU2` — timeline V2 migration (4 call sites), aperto.
- `TICKET-GATE-10-PHASE-4-BLACK-FU3` — LUT-path refactor, aperto.
- Feature-freeze V0.1 revocatione (`AGENTS.md` 🔴 section removal) — possibile solo DOPO 11/11 baseline verde.

### Documentation governance

- [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) ← aggiungi riga FU4 all'indice OPEN (Step 2 del action plan).
- [docs/CHANGELOG.md](../CHANGELOG.md) ← entry FU4 a chiusura.
- [docs/CURRENT_STATUS.md](../CURRENT_STATUS.md) §"Backends" row ← anchor update.
- [AGENTS.md](../../AGENTS.md) §Feature freeze ← Feature-freeze revocation gate.
- [docs/DOCUMENTATION_GOVERNANCE.md](../DOCUMENTATION_GOVERNANCE.md) ← schema canonico.

### Relazionato al tool di audit

- [`tools/audit_incomplete_type_pattern.sh`](../../tools/audit_incomplete_type_pattern.sh) — *segnalazione*: il tool copre attualmente `src/` + `include/chronon3d/` ma NON `tests/install_consumer/`. Estensione suggerita in FU4-hardening (Step 8 action plan), per coprire consumer-side rot futuri come questo.

## Historical notes

Scoperto durante la riesecuzione dell'audit 11-gate su `main@c9415e5b` (M1.5#8) eseguita in questo session. Il FAIL di gate #10 era atteso per il PNG off-by-one (anchor: commit `c9415e5b`). L'analisi più approfondita del log di `tools/install_consumer_test.sh` ha rivelato un **secondo** sotto-blocco di rot, lineage `M1.5#7-AUDIT`: `std::make_shared<c3d::TextRunShape>()` con `TextRunShape` full-type NON transitivamente esposto al consumer SDK umbrella. Il commit `09e09beb` (M1.5#9) chiude solo il PNG off-by-one → FU4 deve chiudere la catena per ottenere 11/11.

Il file `tests/install_consumer/main.cpp:147` è l'unico call site nel consumer-side che istanzia direttamente `make_shared<TextRunShape>()`; altri consumer che istanziano lo stesso tipo contro `Chronon3D::SDK` sarebbero interessati dallo stesso bug, ma siamo allineati con la regola Cat-1 (build correction minimale) → fix nell'header shadow `include/chronon3d/scene/model/shape/shape.hpp` (Option-preferred).
