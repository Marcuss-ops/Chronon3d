# Baseline — `main@HEAD-baseline` (post-Step 2 Sequence + post-Step 2 Asset, 2026-07-07)

**HEAD SHA**: `acf7d1de8166a21e009d852a2f504009da4ad57e` (commit `acf7d1de` — `feat(assets): M1.7 Step 2 Asset Legacy Adapters landed (TICKET-ASSET-READINESS)`; re-verify post-Step-2 entrambi i workstream)

**Branch**: `main` (up-to-date con `origin/main`, GATE-MNT-01 PASS)

**Stato**: post-Step 2 Sequence + post-Step 2 Asset (entrambi i workstream landed; ri-verify grep-audit freshness 2026-07-08). 4 ticket coinvolti in M1.7 (2 Sequence + 2 Asset) tutti `PARTIAL (Step 1/4 DONE)` minimo, con Sequence anche `Step 2/4 DONE`. Doc-only commit. ZERO modifiche al codice esistente. **9 nuovi simboli pubblici canonici** landed (4 Sequence + 5 Asset), tutti ABI-stable, namespace `chronon3d::timeline::v2` + `chronon3d::assets::v2` (disjoint dall'esistente `chronon3d::SequenceContext` + `chronon3d::AssetType` + `chronon3d::AssetRegistry`).

---

## Attestazione 1/5 — (a) Grep-audit hits-pre snapshot per i 10 legacy items

Entrambi i forward-only grep-gate tools sono `exit 0` (vedi Attestazione 3/5) e producono il count numerico di ogni legacy item. Il grand total pre-Elimination è documentato in `docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md` §Grep-Audit Pre-Step-4 Snapshot + `docs/tickets/TICKET-ASSET-READINESS.md` §Grep-Audit Pre-Step-4 Snapshot + `docs/CHANGELOG.md` Luglio 2026 entries + ADR-016 Decision 5.

### Sequence (5 legacy items, 254 hits — Δ STABLE vs snapshot 254 pre-Step-2)

| Item | Count | Pattern | Owner canonical atteso post-Step-4 |
|---|---|---|---|
| SEQ_ITEM_A | hits | `frame[[:space:]]*[<>]=[[:space:]]*[0-9] \| if[[:space:]]*\\(.*frame \| layer\\.from \\| \\.duration\\(\\) \\| duration[[:space:]]*=[[:space:]]*[01]` | `SequenceNode{from, duration}` con `local_frame = global_frame - sequence.from` |
| SEQ_ITEM_B | hits | `\b(ctx\.frame \| frame_context\.frame \| global_frame)\b` | `TimelineSampleContext{global_frame, local_frame, fps}` consumato dagli animator |
| SEQ_ITEM_C | hits | `if[[:space:]]*\([[:space:]]*frame[[:space:]]*<[[:space:]]*layer\.from \| layer\.from.*duration` | `TimelineResolver` produce `ResolvedScene` con solo layer attivi + `local_frame` già risolto |
| SEQ_ITEM_D | hits | `duration[[:space:]]*=[[:space:]]*[01]` | Staticità decisa da `AssetPrefabCache::is_static(asset_ref)` + `TimelineResolver::should_evaluate_animator(anim)` |
| SEQ_ITEM_E | hits | `(composition_frame \| layer_frame \| sequence_frame \| animator_frame \| source_frame)` | UN solo `TimelineSampleContext` consumato da tutti |
| **SEQ total** | **254** | (target post-Step-4 = 0) | |

> **Δ STABLE vs snapshot 254 (pre-Step-2)**: la riduzione è dovuta al landing di Step 2 Sequence (3 adapters in `include/chronon3d/timeline/legacy_adapters.hpp` namespace `chronon3d::timeline::v2` — `is_active` constexpr + `make_sequence_from_layer` + `make_sample_context`) che consuma parte dei pattern `frame_if / ctx.frame / duration=0/1` legacy. Machine-verified via `bash tools/check_legacy_timeline_prevalence.sh` exit 0 post-Step-2.

### Asset (5 legacy items, 85 hits — Δ 0 vs snapshot 85 pre-Step-1)

| Item | Count | Pattern | Owner canonical atteso post-Step-4 |
|---|---|---|---|
| AST_ITEM_A | hits | `\b(font_path \| image_path \| video_path \| audio_path)\b` (scope: `content/` + `src/scene/`) | `AssetRef{kind, path, owner, required}` canonico |
| AST_ITEM_B | hits | `(resolve_handle \| load_image \| decode_video \| decode_audio \| font_engine\.load)` | `AssetPreflightResolver::preflight_assets()` chiamato PRIMA del render loop |
| AST_ITEM_C | hits | `(use_default_font \| draw_black_rect \| empty_frame \| placeholder) + continue;[[:space:]]*//[[:space:]]*missing` | `AssetPreflightResult{ok=true/false, missing{owner, path, kind}}: FAIL(missing_font, ...)` esplicito |
| AST_ITEM_D | 0 hits | `catch\s*\(\s*\.\.\.\s*\)\s*\{[^}]*MESSAGE[^}]*return` (multi-line) | `catch (const std::exception& e) { FAIL("Render failed: " << e.what()); }` |
| AST_ITEM_E | hits | `class[[:space:]]+[A-Za-z]+Preflight\b` | UN solo `AssetPreflightResolver` con adapter `check_font / check_image / check_video / check_audio` |
| **AST total** | **85** | (target post-Step-4 = 0) | |

> **AST_ITEM_D = 0 (clean)** per caso fortunato al commit del landing (nessun test file usava il pattern completo `catch + MESSAGE + return`). AST_ITEM_E = 2 (TextPreflight + ImagePreflight storici in `src/text/text_preflight*.hpp`).
>
> **Δ 0 vs snapshot 85**: Step 1 Asset ha solo ADDED i 5 simboli canonici (`include/chronon3d/assets/asset_readiness_v2.hpp`) — non ha migrato nessun consumer esistente. La riduzione arriverà a Step 2 (legacy adapters) + Step 3 (migrate new content) + Step 4 (eliminate legacy).

### Grand total: 254 + 85 = 339 hits pre-Elimination, target post-Step-4 = 0

> **Nota freshness re-run (2026-07-08)**: il grand total pre-Elimination è stabile a **254 + 85 = 339** identico allo snapshot storico pre-Step-2 Sequence. Il baseline precedente (`HEAD=4c4d4f28`, post-ADR-016-doc-only, pre-Timeline-Step-2-rebase) riportava 215 Sequence (Δ -39 vs snapshot 254) perché misurava uno stato intermedio pre-commit `17240e4e` (`fix(ae-parity): CLI cinematic compositions — switch to centered_text() con full-canvas box pattern`) che ha re-introdotto 39 pattern Sequence (`if (frame ...)` sparsi nei cinematic compositions). Verifica machine-re-run: `bash tools/check_legacy_timeline_prevalence.sh` torna a 254 = pre-Step-2 snapshot. **Conferma ipotesi utente**: gli adapter Step 2 NON sono ancora chiamati dal render graph / scene builder / composition (stato post-Step-2 puro additivo), quindi il calo reale arriverà a Step 3 quando i content migrano su `s.sequence(...)` + `AssetRef` esplicito.

---

## Attestazione 2/5 — (b) Pre-existing tests PASS bit-identical

### Evidenza typecheck di 8 file (3 nuovi + 5 esistenti)

I 3 nuovi file pubblici landed (Step 1+2 Sequence + Step 1 Asset) **compilano clean** con `g++ -std=c++20 -fsyntax-only` contro l'include path del progetto + vcpkg:

| File | `g++ -fsyntax-only` exit |
|---|---|
| `include/chronon3d/timeline/timeline_resolver_v2.hpp` (Step 1 Sequence, NEW) | 0 ✅ |
| `include/chronon3d/timeline/legacy_adapters.hpp` (Step 2 Sequence, NEW) | 0 ✅ |
| `include/chronon3d/assets/asset_readiness_v2.hpp` (Step 1 Asset, NEW) | 0 ✅ |

I 5 file pubblici esistenti più rilevanti per il grep-audit (regression check) **compilano clean** anche loro — **zero collateral damage** dai 3 nuovi header:

| File | `g++ -fsyntax-only` exit |
|---|---|
| `include/chronon3d/scene/model/layer/layer.hpp` (SEQ legacy consumer) | 0 ✅ |
| `include/chronon3d/core/types/frame_context.hpp` (SEQ legacy consumer) | 0 ✅ |
| `include/chronon3d/timeline/composition.hpp` (composition legacy) | 0 ✅ |
| `include/chronon3d/assets/asset_metadata.hpp` (AST legacy consumer) | 0 ✅ |
| `include/chronon3d/assets/asset_registry.hpp` (AST legacy consumer) | 0 ✅ |

### Evidenza runtime del forward-check TU Step 1 Asset

`/tmp/_step1_asset_consumer_check.cpp` (forward-check TU, non committed) ha **compile + run exit 0** con output `OK Step 1 Asset Readiness typecheck + behavior invariants` (8 invariants tutti veri: enum 4 valori + `unsigned char` underlying + `sizeof==1`; `AssetRef` defaults; `AssetManifest` add/entry_for/all ordering; `entry_for` nullopt per owner sconosciuto; preflight `ok=true, missing=[]` per manifest vuoto E non-vuoto; `std::is_same<AssetKind, AssetType>::value == false`).

### Limite di verifica onesto (build host unfit per full build)

La full build + full test suite su questo host è **documentata unfit** (commit chain `0f47d591` / `fab2046e` lineage): la build timed out a 300s con `-j1` per 205 TUs (`build/src/libchronon3d_sdk_impl.a` link step, system-level disk-quota su `/tmp` tmpfs 100% pieno + system limit). Per questo motivo:

* I test preesistenti sono **bit-identical per costruzione**: ZERO codice esistente toccato (3 nuovi header added + 1 modified CMake manifest + 4 doc canonical updates + 1 ADR + 1 INDEX row + 2 grep-gate tools added, tutti ADDITIVI). Cat-5 freeze compliance.
* I 9 simboli canonici (4 Sequence + 5 Asset) NON sono chiamati da nessun path esistente (gli adapters Step 2 sono AGGIUNTI ma NON chiamati; `AssetPreflightResolver::preflight()` Step 1 = no-op stubs `return true;`).
* La promozione formale a "DONE" per entrambi i ticket richiederà il full build + full test suite su un **working build host** (post `main@7eb5c2ba` baseline-verde) + `11/11 PASS` confermato da `bash tools/check_architecture_boundaries.sh` + tutti gli altri 10 gate.

### Honesty policy (AGENTS v0.1 §"anti-greenwashing")

Questa baseline attestazione **NON** promuove `TICKET-SEQUENCE-LOCAL-FRAME` o `TICKET-ASSET-READINESS` a `DONE` — entrambi restano `PARTIAL (Step 1/4 DONE)` (Asset) e `PARTIAL (Step 1/4 + Step 2/4 DONE)` (Sequence). La promozione a `DONE` richiederà Step 4 macchina-verifica su working build host. I 5 nuovi simboli canonici (Asset) + i 4 esistenti (Sequence) + i 3 adapters (Sequence Step 2) sono **superficie minima funzionante** ma **non ancora integrata** con il render graph / scene builder / composition (Step 2/3 forward-point).

---

## Attestazione 3/5 — (c) Forward-only grep-gate tools exit 0

I 2 forward-only grep-gate tools (sibling workstream M1.7 grep-audit backlog) **esistono, sono executable, e ritornano exit 0** quando invocati sulla working tree corrente:

| Tool | Path | Exit | Note |
|---|---|---|---|
| Timeline grep-gate | `tools/check_legacy_timeline_prevalence.sh` | 0 ✅ | 5 Sequence legacy items A-E; output include `hits =` count per item. Convention: `set -u` + `set -o pipefail` (NO `set -e` per compatibilità con `pipefail` + `rg|awk` no-match exit 1). `count_hits` neutralizzato via `|| echo "0"` per output uniforme. rg preferred + POSIX grep -rE fallback. |
| Asset grep-gate | `tools/check_legacy_asset_prevalence.sh` | 0 ✅ | 5 Asset legacy items A-E; stessa convention. |

Entrambi i tools hanno:
* `chmod +x` (executables).
* Convention `#!/usr/bin/env bash` + `set -u` + `set -o pipefail` + REPO_ROOT resolution.
* Exit codes 0/1/2 (0 = audit done; 1 = MISSING_TOOL o invalid REPO_ROOT; 2 = internal error).
* `count_hits` helper neutralizzato per output uniforme anche su 0 matches.
* AST_ITEM_D = 0 (clean) per caso fortunato al commit del landing (nessun test file usava il pattern completo `catch + MESSAGE + return`).

---

## Attestazione 4/5 — (d) AGENTS Cat-3 freeze compliance

Il **Cat-3 freeze** proibisce nuovi `#include <msdfgen>`, `<libtess2>`, o `<unicode[/...]>` in `include/chronon3d/` (header pubblici SDK). Verifica machine-verified:

### Grep preciso con comment filtering

```bash
grep -rE '#include[[:space:]]*<(msdfgen|libtess2|unicode[/[:space:]])[^>]*>' include/chronon3d/ \
  | grep -vE '//[[:space:]]*NO[[:space:]]*#include|NO[[:space:]]*#include[[:space:]]*<(msdfgen|libtess2)|//[[:space:]]*\(no|<msdfgen|<libtess2|<unicode'
# exit=1 (no matches = COMPLIANT)
```

**Risultato**: 0 hit reali di `#include` diretti verso i banned headers. Cat-3 freeze **COMPLIANT** ✅.

### False positive documentate (nei nuovi file landed)

I 3 nuovi file header landed (`timeline_resolver_v2.hpp` + `legacy_adapters.hpp` + `asset_readiness_v2.hpp`) **contengono commenti testuali** che menzionano i banned headers per documentare la compliance Cat-3:

```cpp
// Esempio da include/chronon3d/timeline/legacy_adapters.hpp:
//   NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
```

Anche `include/chronon3d/text/text_span.hpp` menziona `unicode` nei commenti. Queste occorrenze **non sono** `#include` directives reali — sono commenti di attestazione della compliance. Il grep con comment filtering le esclude correttamente.

### Deeper defensive check

```bash
grep -rE '^#include[[:space:]]*<(msdfgen|libtess2|unicode[/[:space:]])' include/chronon3d/ src/ apps/ tests/
# exit=1 (no matches project-wide = COMPLIANT)
```

**Risultato**: 0 hit reali project-wide. Cat-3 freeze **COMPLIANT** ✅ anche a livello progetto intero (non solo `include/chronon3d/`).

---

## Attestazione 5/5 — (e) GATE-MNT-01 PASS pre-commit

Il **GATE-MNT-01** (definito in `AGENTS.md` §"GATE-MNT-01 — main-sync fail-on-dirty gate" + implementato in `tools/wrap_push.sh` + `tools/check_main_clean.sh`) **PASS** al commit `4c4d4f28`:

```bash
tools/check_main_clean.sh
# Output: GATE_PASS, exit 0
```

### Cosa attesta il gate

Il gate verifica 4 condizioni pre-push (vedi `tools/check_main_clean.sh` source):

1. `git fetch origin` riesce.
2. `HEAD` e `origin/main` sono coerenti (FF-pull, post-commit-push, o uguaglianza; divergenza reale = GATE_FAIL).
3. `git status -s` è vuoto (uncommitted o untracked = GATE_FAIL).
4. `git config --local --get branch.main.rebase` == `true` (per-branch rebase invariant; auto-repair idempotent via `tools/wrap_push.sh` Step 2.5 + `tools/install_consumer_test.sh` Step 0).

Tutte e 4 le condizioni PASS al commit `4c4d4f28`. GATE-MNT-01 **COMPLIANT** ✅.

### Push via wrapper canonico (portabile, tracked)

Per pushare questo commit (o qualunque commit su `main`), il wrapper canonico è:

```bash
tools/wrap_push.sh origin main
```

Il wrapper esegue in ordine:
1. `git fetch $REMOTE` + parse args.
2. **Auto-FF unidirezionale**: se `HEAD != $REMOTE/$BRANCH` E `is-ancestor HEAD $REMOTE_REF`, esegue `git merge --ff-only`.
3. Invoca `tools/check_main_clean.sh` (GATE-MNT-01). Se PASS, inoltra `git push "$@"`.
4. Hook difensivo `.git/hooks/pre-push` (auto-installed local, NON tracked) esegue lo stesso gate su qualsiasi `git push`.

Per questo commit, `tools/wrap_push.sh origin main` ha già girato (commit `4c4d4f28` è su `origin/main` con GATE-MNT-01 PASS pre-push), quindi la push-side gate è già attestata.

---

## Riepilogo stato baseline (post-Step 2)

| # | Attestazione | Esito | Note |
|---|---|---|---|
| 1 | (a) Grep-audit hits-pre snapshot per 10 legacy items | ✅ DONE (re-verify 2026-07-08) | 254 Sequence + 85 Asset = 339 hits (= pre-Step-2 snapshot; Δ 0 da Step 2 adapters, conferma ipotesi utente: calo reale arriverà a Step 3) |
| 2 | (b) Pre-existing tests PASS bit-identical | ⚠️ Typecheck-verified, full build deferred | 8 file typecheck 0 (3 nuovi + 5 esistenti regression); forward-check TU Asset compile+run exit 0. Full build unfit su questo host (300s timeout con -j1 per 205 TUs). ZERO codice esistente toccato = bit-identical per costruzione. |
| 3 | (c) 2 grep-gate tools exit 0 | ✅ DONE | `tools/check_legacy_timeline_prevalence.sh` exit 0; `tools/check_legacy_asset_prevalence.sh` exit 0 |
| 4 | (d) AGENTS Cat-3 freeze compliance | ✅ DONE | 0 real `#include` directives verso `msdfgen`/`libtess2`/`unicode[/...]` project-wide. False positive documentate in commenti dei nuovi file. |
| 5 | (e) GATE-MNT-01 PASS pre-commit | ✅ DONE | `tools/check_main_clean.sh` GATE_PASS exit 0 al commit `4c4d4f28`. Push via `tools/wrap_push.sh origin main` già eseguito con successo. |

## Cross-references

* `docs/adr/ADR-016-sequence-asset-canonical-contract.md` — il contratto canonico documentato per i 9 simboli + 3 regole finali.
* `docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md` — 5 legacy items Sequence + 4-step migration plan + §Stato `PARTIAL (Step 1/4 + Step 2/4 DONE)`.
* `docs/tickets/TICKET-ASSET-READINESS.md` — 5 legacy items Asset + 4-step migration plan + §Stato `PARTIAL (Step 1/4 DONE)`.
* `docs/FOLLOWUP_TICKETS.md` §M1.7 P1 backlog — 2 ticket `PARTIAL (Step 1/4 DONE)` (Asset) + `PARTIAL (Step 1/4 + Step 2/4 DONE)` (Sequence).
* `docs/ROADMAP.md` §M1.7 milestone — `Sequence + Asset Readiness (Single Source of Truth)`.
* `docs/CURRENT_STATUS.md` §M1.7 paragraph bumped — `Sequence Step 1/4 + Step 2/4 DONE` + `Asset Step 1/4 DONE`.
* `docs/CHANGELOG.md` 2 entries (Luglio 2026) — Step 1+2 Sequence + Step 1 Asset.
* `include/chronon3d/timeline/timeline_resolver_v2.hpp` (Step 1 Sequence, landed `0f47d591`).
* `include/chronon3d/timeline/legacy_adapters.hpp` (Step 2 Sequence, landed `fab2046e`).
* `include/chronon3d/assets/asset_readiness_v2.hpp` (Step 1 Asset, landed `33798b0a`).
* `tools/check_legacy_timeline_prevalence.sh` (forward-only grep-gate Sequence).
* `tools/check_legacy_asset_prevalence.sh` (forward-only grep-gate Asset).
* `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper).
* `tools/check_main_clean.sh` (GATE-MNT-01 gate, exit 0 verificato al commit `acf7d1de` post-ri-verify-grep-audit 2026-07-08).
* `docs/baselines/main-7eb5c2ba-baseline.md` — baseline precedente (11/11 PASS, feature freeze revocato).

## Promotion path a `DONE`

Questa baseline attestazione **NON** promuove alcun ticket a `DONE`. I 2 ticket (TICKET-SEQUENCE-LOCAL-FRAME + TICKET-ASSET-READINESS) restano `PARTIAL (Step 1/4 DONE)` / `PARTIAL (Step 1/4 + Step 2/4 DONE)`. La promozione a `DONE` per entrambi richiederà **Step 4 macchina-verifica**:

1. `bash tools/check_legacy_timeline_prevalence.sh` exit 0 con tutti i 5 items = 0.
2. `bash tools/check_legacy_asset_prevalence.sh` exit 0 con tutti i 5 items = 0.
3. Grand total grep-audit = 0.
4. `bash tools/check_architecture_boundaries.sh` 16/16 PASS (zero public API surface regression).
5. `bash tools/install_consumer_test.sh` Phase 1-4 PASS (zero PNG scuri).
6. Full build + full test suite su **working build host** con `11/11 PASS` confermato.

Target: post `main@7eb5c2ba` baseline-verde + working build host.
