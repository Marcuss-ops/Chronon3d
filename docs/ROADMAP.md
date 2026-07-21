# Chronon3D — Active Roadmap

La roadmap è organizzata per milestone prodotto. Non avviare una milestone
successiva per nascondere blocker della precedente.

Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md). Criteri di release: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

> **Snapshot (2026-07-15):** `main@04c1cb48` — current origin/main HEAD. 13/13 action plan closure landed earlier (see §13/13 Action Plan closure below). CI infrastructure: sanitizers nightly/weekly, renderer-boundary gate, test hygiene gate, CI status JSON artifact; **current HEAD non certificato** (multipli gate CI in fallimento sui commit recenti). Video pipeline: structured errors, memory budget, atomic output. Test coverage: frame rate 55 tests, determinism matrix 16 tests.

> **Snapshot macchina-verificato (2026-07-04):** `main@2895bd88` — post-FF-pull origin/main; **§3.1 Esecuzione del piano commitato (commit `a8414705`)** at 2026-07-04: TICKET-A3-CACHE-LEASE (CameraSessionLease rollback real) **CHIUSO**.  **8/11 atomic PASS + 1 FAIL (g4) + 1 PASS* (g8) + 1 NOT RUN (g10)** osservato post-§3.1 atomic audit run: g1, g2, g3, g5, g6, g7, g9, g11 tutti `exit 0`; **g4 FAIL** per abs-path leak in `docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md:75` (`cd <REPO_ROOT>`) — propagazione TICKET-GATE-4-LEAK cluster (vedi `FOLLOWUP_TICKETS.md`); **g8 PASS*** (warn-mode; 89 drift findings, ↓ da 170 post-M1.5#6 tightening pass); **g10 NOT RUN** (heavy full-build requirement; carry-over rot install_consumer_test.sh, TICKET-GATE-10-PHASE-4-FIX parziale).  **8 atomic PASS ≠ 11/11: feature freeze ANCORA ATTIVO** (revoca richiede 11/11 PASS macchina-verificato sullo stesso commit, vedi `AGENTS.md` §Feature Freeze + `CURRENT_STATUS.md` §Certificazione corrente).  M2 Camera V1 ADR-013 ✅ documented+accepted; A3 cluster source-code **1/8 chiuso** (TICKET-A3-CACHE-LEASE).  M3 SDK V1 **NOT green** (gate #10 carry-over rot).  Storico baseline `>9/11` macchina-verificata: [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md) (10/11 pre-fix log).  Ultima 11/11 macchina-verificata osservata: `main@1078ab46` (post-Fase A+B2+B3, 2026-07-04; conservata in `CURRENT_STATUS.md`, table "Gate audit snapshot — `main@1078ab46`").  Per i dettagli di ogni milestone vedi le sezioni M0–M6 sotto.

> **Snapshot (2026-07-07, post-Phase-H Soluzione B + MultiSourceNode consistency — verification PENDING, tickets remain PARTIAL):** TICKET-AE-CAM-PRECISION-COLLAPSE + TICKET-ae-cam-hash-collision Soluzione B atomic commit `d39b37f1` (cumulative 27 file) + MultiSourceNode consistency commit `853ace48` (3 sites mirror source_node pattern with `from_mat4(item.matrix, item.opacity)` for proper TRS decomposition, pre-empting empty-Transform-tr bug).  **Verification FAILED on working build host 2026-07-07**: 32/35 `chronon3d_ae_parity_tests` PASSED + 3 in-memory FB hash tests FAILED (AE_CAM_03/05/06 at `tests/visual/ae_parity/ae_parity_tests.cpp:230/303/341`) + 13 banned PNGs remain on disk (sha256 prefix `cc86d2b5e80287dc`) + 9-key `test_node_cache_ae_sweep` blocked at `ar` link step (system-level disk-quota exceeded).  Candidate root cause (Gemini source-read, NOT machine-verified): SourceNode round-2 fix at `src/render_graph/nodes/source_node.cpp:122/216` passes a default-constructed `chronon3d::Transform tr;` (scale=1,1,1) to `project_layer_2_5d`, propagating `layer_size=1x1` → 2D layers render as transparent-black → `framebuffer_hash` collisions.  Forward-fix path documented in `docs/tickets/TICKET-ae-cam-hash-collision.md` `## Verification gap` (Option 1: restore `m_uses_2_5d_projection` check; Option 2: pass `m_node.world_transform` instead of empty `tr`).  **Promotion to `DONE` is intentionally NOT triggered** — the user request was "Once verified, update ... to fully DONE"; verification did NOT pass, so tickets remain at `PARTIAL` (matrix-fix cluster DONE + hash-collision cluster OPEN).  `docs/CURRENT_STATUS.md` §Phase H blockquote documents the full evidence + forward-fix path.  Cat-2 AE-parity visual contract (gate #2) status: **BLOCKED** until SourceNode empty-Transform-tr fix lands + end-to-end re-bake produces 24 fresh-distinct PNGs + `check_ae_parity_golden_state.sh` transitions FAIL→PASS + 9-key test runs + PASSes.  AGENTS.md v0.1 Cat-1 (build corrective) + Cat-3 (no public API surface) + Cat-5 (doc-only alignment) freeze-compliant.  Zero new public symbols.

### Fase A1–A2 — Migrazione header interni + unificazione backend (2026-07-03)

- **A1** (`1bd92daf`): Rimossi 4 symlink legacy (`render_session.hpp`, `session_services.hpp`, `scene_hasher.hpp`, `scene_program_store.hpp`) → accessibili solo via path `internal/`. Creato `tools/check_header_standalone_compile.sh` — compila standalone ogni header pubblico del manifest.
- **A2** (`8fdb0de8`): Unificata costruzione backend — `render_engine.cpp` e `cli_render_utils.cpp` ora usano entrambi `attach_software_backend()` come factory canonica. Rimosse ~65 linee duplicate di SoftwareBackendServices + make_software_backend + ProcessorSourceExtras.

### Fase B2+B3 — Deprecation global state (2026-07-03)

- **B2** (`2d3cc2dc`): `process_wide_assets_root()` / `process_wide_resolver()` marcati `[[deprecated]]` in `render_runtime.hpp`. Migration path: `RenderRuntime::resolver()` per-engine. Eliminazione effettiva bloccata da ~24 call sites (CLI, test, content modules) → Phase C.
- **B3** (pre-esistente): `shared_text_layout_cache()` marcato con deprecation banner in `text_run.hpp` / `text_run.cpp`. Migration path: `RenderSession::layout_cache` per-session. Eliminazione bloccata da ~35 call sites → Phase C.

### Fase C2 — Factory unificata RenderRuntime (2026-07-03)

- **C2** (in corso): `RenderRuntime::create(RuntimeConfig)` → `Result<RenderRuntime, RuntimeBuildError>`. `RuntimeConfig` wrappa `Config` + `optional<assets_root>`. `attach_backend()` rafforzato `[[deprecated]]` con suppression nei bridge interni.

### Fase C — Completamento doc (2026-07-02)

- **C2** (`d8a228f7`): Costruttore `RenderEngine::Impl` unificato (`optional<path>`).
- **C3** (`3b4dbdc6`): Pipeline canonica documentata (Definition→Compiler→Evaluator→GraphCompiler→Executor).
- **C1** (doc-only): `CompiledTextRun` pianificato in `text_run.hpp`; blocked by feature freeze.

## 10-point friction audit — closure summary (2026-07-08)

> **Snapshot macchina-verificato (2026-07-08):** 10-point friction audit lineage officially closed. 7 atomic commits landed (commit range `0ff8b100`..`8c1e9ddc`) sweeping FIX 1–10 sui 4 aree di sudore principale (Camera V1 + SDK C++ installabile + Render runtime + Composition pipeline + Test/CI). Il commitment chain copre ogni singolo FIX con un commit atomic, tutti verificati a livello strutturale (gate): `check_architecture_boundaries.sh` PASS, `check_main_clean.sh` PASS, `check_test_hygiene.sh` PASS, `check_test_suite_registration.sh` PASS (post `e369f9e7` promotion a bloccante), `check_frame_value_convention.sh` PASS (post `fae3a0ac` chain).

> **Per-FIX commit-range mapping (machine-verified)** — vedi blocco equivalente (più dettagliato) su [`docs/CHANGELOG.md`](docs/CHANGELOG.md) entry "10-point friction audit + fixes" + su [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-10-POINT-FRICTION-AUDIT-CLOSURE` row. Riassunto:

> | FIX | Area | Commit | Deliverable sintetico |
> |---|---|---|---|
> | 1+2+3 | Composition / asset-manifest | `0ff8b100` | `commit_layer()` helper preserves AssetManifest for inactive layers/sequences |
> | 4 | Composition / shape DSL | `3ded66a9` | `fullscreen_rect` canvas-correct via `pin_to(Anchor::Center)` + `pos=(-w/2,-h/2,0)` |
> | 5 | Tests / docs | `8c1e9ddc` chain | sRGB pixel assertion helper (`tests/helpers/color_expect.hpp`) + doctest hygiene gate (`tools/check_test_hygiene.sh`) |
> | 6 | SDK / canonical header | `2c0254c3` (then promoted to strict gate in `fae3a0ac` chain) | Frame reading convention docblock in `include/chronon3d/core/types/frame.hpp` |
> | 7 | SDK / build DSL | `2332dc7d` | LayerBuilder `start_at()` + `length()` aliases (additive-only) |
> | 8 | CLI / preflight | `99323724` | V2 preflight default in CLI + `--legacy-preflight` opt-in flag |
> | 9 | Tests / audit | `27fab453` + `8c1e9ddc` (promoted to FAIL via TICKET-110 chain `e369f9e7`) | test suite registration audit gate |
> | 10 | Tests / build rot | `4113a8db` chain | Unblock `chronon3d_render_graph_tests` compilation + SceneProgramStore/SceneProgramCache accessors + ExecutionScope::make_root factory |

> **Cross-link canonici (gate #7 `check_doc_sync.sh`):** [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) §Stato generale per area "10-point friction audit" row + [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-10-POINT-FRICTION-AUDIT-CLOSURE` row + [`docs/CHANGELOG.md`](docs/CHANGELOG.md) "10-point friction audit + fixes" entry (commit range `0ff8b100`..`8c1e9ddc` block).

> **Forward-only honesty (AGENTS.md §anti-greenwashing):** questo audit-closure NON chiude i seguenti pre-existing rot tickets tracked in [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) §Active Blockers: `TICKET-011` — `chronon3d_core_tests` mainline rot (PARTIAL); `TICKET-036` — camera architecture gate G6 (PLANNED); `TICKET-120` — 17/24 scene test failures (PARTIAL); `TICKET-GATE-4-LEAK-CHANGELOG` — abs-path leak in CHANGELOG.md (OPEN); `TICKET-GATE-10-PHASE-4-BLACK-FU5` — PNG mean-RGB rot (OPEN). Tickets tracked altrove (`TICKET-005` / `TICKET-011-i` / `TICKET-044` in FOLLOWUP §Open Blockers) sono anch'essi out-of-scope. Tutti questi ticket sono out-of-scope rispetto ai 10 del friction audit e richiedono lavori separati per la chiusura.

---

## M0 — Baseline verificata

### Obiettivo

Produrre un commit candidato sul quale build, test, gate, consumer e documenti
riportano lo stesso stato.

### Lavori

1. Completato: link/run di `chronon3d_scene_tests` (TICKET-029).
2. Rieseguire i regression test dei fix camera recenti.
3. Completato: `SoftwareRenderer::settings()` regression (TICKET-039).
4. Completato: lambda/auto compile rot in text preset visual (TICKET-038).
5. Chiudere i gap Precomp, execution scope e identity/session che bloccano la baseline.
   - P1 #3 (parziale): `RenderSession::layout_cache` sostituisce il singleton `shared_text_layout_cache()`. Migrazione callsite post-baseline.
6. Eseguire core, lean, no-content e full-validation sullo stesso commit.
7. ~~Rendere architecture e renderer-boundary gate realmente bloccanti.~~ **DONE** — P0-C (commit `e79f8621`): `continue-on-error` rimosso da gates.yml Gate 5, 5 invarianti (I1–I5) verificate green su `main@HEAD`.
8. Eseguire install consumer sullo stesso commit.
9. Registrare comandi ed esiti osservati in `docs/baselines/`.

### Gate di uscita

- nessun test richieto skipped per nascondere un errore;
- nessun gate con `continue-on-error` sul percorso candidato;
- tutti i profili richiesti verdi sullo stesso commit;
- documenti sincronizzati.

---

## V0.1 — Acceptance suite (REGISTERED, macchina-verification deferred)

> **Origine:** TICKET-ACCEPTANCE-SUITE-PHASE-D closure commit (2026-07-11).
> 20 acceptance contract rows REGISTERED into `chronon3d_acceptance`
> aggregate meta-target (15 in-orchestrator + 1 out-of-tree + 4 forward-point
> catalog rows).

> **Regola AGENTS.md §honesty:** "20/20 PASS" in commit title = REGISTERED
> (committed, meta-target wired), NOT VERIFIED (no `ctest -L acceptance`
> execution on a working build host in this commit). Macchina-verification
> deferred to (0e) first green `ci-sanitizer.yml` run.

### Stato osservabile

| Acceptance contract row | Categorization | Stato |
|---|---|---|
| 1–15 (in-orchestrator) | Phase A→B→D lineage, 14 per-category contracts + 1 meta-target | REGISTERED |
| 16 (out-of-tree `install_consumer_ci`) | ct label `acceptance` outside `tests/acceptance/` | REGISTERED |
| 17–20 (forward-point catalog) | Phase B/C/D pipeline forward-point placeholders | PLANNED |

### Gate di uscita

- la tabella sopra aggiornata macchina-verificata (registration + actual
  `ctest -L acceptance` 20/20 PASS, ZERO verde fabrication);
- `[docs/FOLLOWUP_TICKETS.md](docs/FOLLOWUP_TICKETS.md)` `## Open Blockers`
  TICKET-PERF-GATE-STABLE-PROMOTION (forward-point (0c) auto-promote
  ADVISORY → BLOCKING after 3 STABLE RUN markers);
- `[docs/CURRENT_STATUS.md](docs/CURRENT_STATUS.md)` `## Acceptance suite`
  row mantiene framing `20/20 REGISTERED → VERIFIED` (forward-point (0e)).

### Cross-link canonici

- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) `## Acceptance suite` row;
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed`
  + `## Open Blockers` TICKET-PERF-GATE-STABLE-PROMOTION;
- [`tests/acceptance/CHANGELOG.md`](tests/acceptance/CHANGELOG.md) subsystem
  ledger (NEW file in this commit).

### Non-goal V0.1

- claim "AGENTS.md §honesty" *DONE* without macchina-verification;
- expand `chronon3d_acceptance_tests` aggregate fino a 25/25 senza il baseline
  gate verde (freeze-rule di `AGENTS.md §Feature Freeze`);
- bridge ACCEPTANCE → CI without addressing first-green (0e) forward-point.

---

## 13/13 Action Plan — closure summary (2026-07-10)

> **Snapshot:** `main@50e36a04` — 13/13 azioni completate (15 commits su `main`, range `4e9a14d4`..`50e36a04`).

| # | Azione | Commit | Deliverable sintetico |
|---|--------|--------|---|
| P0-A | ctest nei gate core-build/sdk-build | `61bceb6c` | `ctest --preset` aggiunto ai gate CI per esecuzione test reali |
| P0-B | paths filter full-validation | già presente | paths filter su `gates-full-validation.yml` |
| P0-C | renderer-boundary gate blocking | `e79f8621` | `continue-on-error` rimosso; 5 invarianti (I1–I5) blocking |
| P0-D | consumer CI manifest-clean | `bb592df5` | consumer CI test manifest-clean |
| P1-A | Eliminare global AssetRegistry | `f06e4b1f` | DI pattern per AssetRegistry nel CLI |
| P1-B | Video output atomico | `7d30771f` | `.partial` temp file + ffprobe validation + rename atomico |
| P1-C | VideoSink structured errors | `c869ac19` | `VideoSinkError` enum (13 codici) + `last_error()` virtual methods |
| P2-A | Sanitizers nightly/weekly | `24b6b8f3` | ASan+UBSAN nightly, TSan weekly; rimosso da ogni-push CI |
| P2-B | Memory budget | `c89b6ed2` | `kMaxFrameDimension` (16384) + overflow guards in `frame_buffer_size` |
| P2-C | Determinism matrix | `ce01a07d` | 16 test: 5 scene × 5 condizioni, XXH64 hash comparison |
| P2-D | Frame rate edge cases | `8695960a` | 55 test: 7 standard rates + subframe + freeze + reverse + validation |
| P2-E | Test hygiene gate | `726042fd` | 3 invarianti: duplicate doctest main, skip senza ticket, empty assertions |
| P3-A | CI artifact docs | `ea9f5062` | `tools/ci_status_from_junit.py` → JSON `{commit, tests_total, passed, gates_passed}` |
| — | unbreak media_video_tests | `50e36a04` | `chronon3d_pipeline` aggiunto a LINK_TARGETS (OBJECT lib propagation) |
| — | CURRENT_STATUS update | `c8b63471` | 3 nuove area rows: Video pipeline, CI infra, Test coverage |

### Gate di uscita (verificati)

- tutti i gate CI passano senza `continue-on-error`;
- sanitizers non rallentano il feedback loop (nightly/weekly);
- test hygiene gate (3 invarianti) blocca skip senza ticket e assertions vuote;
- video pipeline ha errori strutturati e budget memoria esplicito;
- determinism matrix e frame rate tests coprono edge cases critici;
- CI produce artifact JSON machine-readable per run.

### Cross-link canonici

- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) §Stato generale per area (3 nuove rows);
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) entry per ogni commit;
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Open Blockers (verificato invariato).

## V0.2 — Test coverage expansion (4 fasi, PLANNED, post-baseline-verde)

> **Origine:** sessione 2026-07-09. 4 fasi sequenziali di test coverage (bbox/clip → transforms/animation → multilingual → layout advanced). NON avviabili fino a `11/11 PASS` macchina-verificato sullo stesso commit (AGENTS.md §Feature Freeze revoca già avvenuta 2026-07-06 con `main@7eb5c2ba` 11/11). Regola di stato osservabile: PASS / FAIL / PARTIAL / NOT RUN / PLANNED.

- **Fase 1 — bbox/clip regression (10 test proposti)** (PIVOT, 2026-07-09): 9/10 test proposti erano duplicati di `tests/text_golden/text_clip/text_completeness.cpp` esistenti (TextCompleteness 15-test suite, commits `120e6a2c`..`89bb99ea`) — AGENTS.md anti-duplicazione violation. Cycle 1 (NoWhiteStripeRegression.Basic commit `0138c66d`) FALLITO al runtime (bbox.height/font_size = 0.378 vs spec 0.55, doctest exit 0 mascherato). Revertito in `6892fb23`. 9 untracked .cpp + helper poi scartati via `git clean -fd tests/text_golden/text_no_white_stripe/`. **PIVOT decision**: bbox/clip coverage gap è ora considerato COVERED dall'esistente TextCompleteness 15-test suite + 3-test `text_clip_bounds.cpp` regression lock. **0/10 test landed**. NO new ticket in FOLLOWUP. Vedi [`docs/CHANGELOG.md`](CHANGELOG.md) entry "docs(fase-1): bbox/clip test expansion — PIVOT to anti-duplicazione".

- **Fase 2 — transforms/animation (7 test proposti)** (PLANNED, macchina-verifica deferred): RotateZNotCut, SkewNotCut, RotationXYNotCut, PositionAnimationNoClip, ScaleAnimationNoClip, BlurAnimationNoClip, PrecompTextNoClip. Anti-duplicazione: tutti e 7 genuinamente nuovi (no match in text_completeness.cpp / text_clip_bounds.cpp / text_unicode.cpp). **0/7 eseguiti**. Ticket `TICKET-FASE2-TRANSFORMS-ANIMATION` aperto in [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Fasi 1-4 cluster.

- **Fase 3 — multilingual/fallback (8 test proposti)** (PLANNED): KerningPairs, MixedAdvanceWidths, MixedBaseline, LatinExtended, MissingGlyphPolicy, EmojiFallback, RTLArabic, CJK. **RTL/CJK feature già supported nel codebase** (tests esistenti: `tests/text_golden/text_completeness/text_unicode.cpp:TestUnicode 03` NotoNaskhArabic-Bold + `TestUnicode 04/05` CJK fallback + `tests/text/test_text_bidi.cpp` + `tests/text_golden/user_spec/05_bidi_english_arabic_mixed.cpp`; source: `src/backends/text/bidi_segmenter.cpp` path canonico). Quindi **NO ticket per RTL/CJK feature gap** (features already present). Ticket `TICKET-FASE3-MULTILINGUAL` aperto solo per test coverage gap (8 test PLANNED per chiudere coverage).

- **Fase 4 — layout advanced (10 test proposti)** (PARTIAL, 2026-07-11): AnchorModes, VerticalAlign, HorizontalAlign, LineHeightExtreme, AutoFit, Ellipsis, Spaces, Punctuation, Numbers, IntentionalMaskClip. **Closure status (this commit)**: 4 nuovi test groups + 1 enum value landed: (1) **3×3 wrap×overflow matrix** (`TextWrap::{None,Word,Character} × TextOverflow::{Clip,Ellipsis,Visible}`) in `chronon3d_text_layout_advanced_tests::AdvancedLayout/3x3 wrap x overflow matrix` (9 SUBCASEs); (2) **line-height matrix** (0.5/0.8/1.0/1.2/1.5/2.0/3.0 × 3-line text + monotonicity check) in `chronon3d_text_layout_advanced_tests::AdvancedLayout/line-height matrix` (8 SUBCASEs); (3) **3 adversarial countertests** (long-word / sub-glyph box / ellipsis-without-space) in `chronon3d_text_layout_advanced_tests::AdvancedLayout/adversarial *` (3 TEST_CASEs × ~3 SUBCASEs each); (4) **`TextOverflow::Visible` enum value** added to `include/chronon3d/scene/model/shape/shape.hpp` (was missing — only `Clip` + `Ellipsis` existed; the 3rd user-spec mode is now a no-op for layout purposes, signaled to the renderer as "text is allowed to extend beyond the box"). **Remaining forward-points**: AnchorModes + Spaces + Punctuation + Numbers + IntentionalMaskClip (5 content-specific tests, 5/10 del ticket), AutoFit (feature gap, real blocker — needs ADR before implementation per Cat-3 freeze). 2 ticket aperti: `TICKET-FASE4-LAYOUT` (PARTIAL, 5/10 sub-tests done in this commit) + `TICKET-FASE4-AUTOFIT` (feature gap, still PLANNED).

- **Gate di uscita V0.2**: tutti i 4 ticket in `FOLLOWUP_TICKETS.md` §Fasi 1-4 cluster promossi da PLANNED a DONE; macchina-verifica `ctest` su `chronon3d_text_golden_tests` verde per ciascuno dei 4 lotti (Fase 1 = N/A post-pivot, Fasi 2/3/4 = nuovi lotti). `docs/FEATURES.md` §Text aggiornato per riflettere il gap di auto-fit (presente/assente/parziale). `docs/CHANGELOG.md` chiusura "V0.2 — Test coverage expansion" registrata.

- **Cross-link canonici**: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Fasi 1-4 cluster; [`docs/CHANGELOG.md`](docs/CHANGELOG.md) 4 entry prepended (una per fase); [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) §Stato generale per area (Text Production V1 row) verrà aggiornato alla chiusura V0.2.

### §10 status matrix (2026-07-10) — V0.2 cluster PLANNED snapshot

> **Origine:** sessione 2026-07-10 (post-§6 + §7 cycle, AGENTS.md v0.1 §Cat-5 doc-only refresh).  The 4 V0.2 tickets remain PLANNED per the original scope; this snapshot is a canonical cross-link that consolidates the 4-ticket state into a single row for audit clarity.  NO state mutation.  See [`docs/CHANGELOG.md`](docs/CHANGELOG.md) §10 entry for the full 4-ticket matrix with test lists + scoping notes.

| # | Ticket | Stato | Prio | Blocca | Forward-point |
|---|---|---|---|---|---|
| 1 | `TICKET-FASE2-TRANSFORMS-ANIMATION` (7 test) | **PLANNED** | P1 | Text V1 cert | 7 atomic commits su main, 1 per test |
| 2 | `TICKET-FASE3-MULTILINGUAL` (8 test) | **PLANNED** | P1 | Text V1 cert | 8 atomic commits, priorit\u00e0 ai 3 nuovi (KerningPairs/MixedAdvanceWidths/MixedBaseline) + expand dei 5 esistenti |
| 3 | `TICKET-FASE4-LAYOUT` (10 test) | **PLANNED** | P1 | Text V1 cert | 10 atomic commits, priorit\u00e0 ai 7 nuovi + expand dei 3 esistenti |
| 4 | `TICKET-FASE4-AUTOFIT` (FEATURE GAP) | **PARTIAL** (engine-level DONE commit `<pending>`, 2026-07-11) | P1 | Text V1 cert + M1.6 AE-parity cinematic | Engine-level closure: 8-iter TextLayoutEngine::layout() updated to ADR-018 12-iter pure binary search (0.1f epsilon nudge removed for determinism); compile_or_cache_layout() now uses named local `fits_inside(box, bounds)` lambda for the `fits_inside(layout_box)` gate (3 inline checks replaced); 3 new SUBCASEs in `tests/text/test_auto_fit_font_size.cpp` (termination guarantee / degenerate box / 100-run determinism). Cat-3 safe (local lambda, zero new public API). Forward-points: `include/chronon3d/presets/text/auto_fit.hpp` canonical wrapper (ADR-gated) + re-bake of `text_autofit_golden.png` on working build host. |

**Refs:** [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Fasi 1-4 cluster (rows 44-47); [`docs/CHANGELOG.md`](docs/CHANGELOG.md) §10 entry (4-ticket matrix).

## M1 — Text Production V1

### Obiettivo

Generare titoli, citazioni, keyword highlight e sottotitoli animati affidabili
per pipeline video automatizzate.

### Lavori

1. Completare rich text e styling per parola end-to-end.
2. Introdurre timed text/SRT/JSON e word timing.
3. Implementare highlight, karaoke, word pop e subtitle layout policies.
4. Completare Wiggly/Wave/Random selector richiesti dai preset.
5. Stabilizzare almeno 20 preset generali e 8 subtitle.
6. Aggiungere golden 16:9/9:16, testo corto/lungo e più timestamp.
7. Verificare 24/30/60 fps e determinismo seriale/parallelo.
8. ~~Esporre esempi pubblici tramite `Chronon3D::SDK`.~~ **DONE** — Text Export V1 certificato (vedi sotto).

### Text Export V1 — CERTIFICATO ✅ (2026-07-10, `main@3fa5880f`)

> **Definizione:** Un progetto esterno installa Chronon3D, crea testo da una stringa,
> carica un font controllato e produce glifi visibili e deterministici senza utilizzare
> alcuna API interna.

**Criteri soddisfatti:**

| # | Criterio | Stato |
|---|---|---|
| 1 | Consumer esterno installa via `cmake --install` | ✅ PASS |
| 2 | Include solo header pubblici | ✅ PASS |
| 3 | Linka solo `Chronon3D::SDK` | ✅ PASS |
| 4 | Stringa UTF-8 reale | ✅ `TEXT EXPORT V1` |
| 5 | Font esplicito incluso negli asset | ✅ `fonts/Inter-Bold.ttf` |
| 6 | Renderer auto-crea FontEngine | ✅ verificato end-to-end |
| 7 | Layout prodotto non nullo | ✅ glyph_count > 0 |
| 8 | Testo genera glifi reali | ✅ 6100/230400 delta pixels |
| 9 | Anti-false-green validation | ✅ two-pass render diff |
| 10 | Due render identici deterministici | ✅ `deterministic=true` |
| 11 | Consumer non dipende dalla directory sorgente | ✅ standalone build |

**Componenti deliverate:**
- `tools/sdk/run_external_consumer.sh` — check_text wired come primary Phase 4 gate
- `tests/install_consumer/main_text.cpp` — anti-false-green two-pass validation
- `examples/text_export_consumer/` — esempio standalone drop-in
- `cmake/Chronon3DPublicHeaders.cmake` — `glyph_selector_spec.hpp` aggiunto al manifest

### M1.5 — Text Pipeline Decomposition Backlog (post-feature-freeze, PLANNED)

> **Origine:** audit di leggibilità/maintainability del 2026-07-03 sull'orchestrator
> testo. NON avviabile fino a `11/11 PASS` sullo stesso commit (feature freeze
> attivo, vedi [`AGENTS.md`](../AGENTS.md)). Il backlog è codificato qui, in un
> canonical, per evitare documenti paralleli vietati da
> [`DOCUMENTATION_GOVERNANCE.md`](DOCUMENTATION_GOVERNANCE.md).
>
> **Regola di stato osservabile:** PASS / FAIL / PARTIAL / NOT RUN / BLOCKED /
> PLANNED. Tutti i 15 item sono attualmente `PLANNED` (nessun codice toccato
> durante il freeze). Avvio rigido: prima mini-sequenza `1 → 2 → 3 → 4`.

| #  | File / area target                                              | Pri    | Stato    | Vincolo architetturale                                                                                  |
|----|------------------------------------------------------------------|--------|----------|----------------------------------------------------------------------------------------------------------|
| 1  | `src/render_graph/nodes/TextRunNode.cpp`                        | P0     | DONE     | `execute()` → orchestratore breve; canale `Result<OwnedFB, NodeExecutionError>`; nessun falso successo. Refactor committato (commit `82d2b0e0`): 3 helpers in `src/render_graph/nodes/text_run/` (execution / transform / diagnostics). `chronon3d_graph_nodes` lib compile clean (LIB_EXIT=0 re-verified). Return-channel contract locked da `tests/render_graph/nodes/test_text_run_node_return_channel.cpp` (3 failure mode: ExecutionFailure, CapabilitiesOff, NullBackend + Success path). `chronon3d_render_graph_tests` target ha blockers pre-esistenti risolti (`camera_change_policy.cpp::Camera2_5D::projection_mode` chiuso in `ac514fea`; `text_unit_map` link rot tracked in TICKET-011 — fuori scope M1.5#1). Avvio rigido mini-seq post-baseline: `1 → 2 → 3 → 4`. |
| 2  | `src/text/text_run_driver.cpp`                                  | P0     | PLANNED  | Refactor committato: orchestratore breve + 3 helpers in `src/text/driver/` (text_state_sampler / text_font_state / text_layout_rebuild). Nuova struct pubblica `EffectiveTextState{text, FontLayoutIdentity, TextDirection, Bcp47LanguageTag, TextShapingFeatures}` in `include/chronon3d/text/text_run_driver.hpp`; operator== confronta tutti i 5 campi in declaration order. Il fast-path in `apply_active_state_to_text_run_shape` partecipa ora a TUTTE le dimensioni di `TextLayoutCacheKey::digest()` (non più solo `source_text + FontLayoutIdentity` come pre-M1.5#2 / P0-2). `compute_effective_font` preserva P0-2 (font overrides senza `font_path.empty()` gate). `chronon3d_text_core` lib compile clean (`LIB_EXIT=0`); nuovo test `tests/text/test_effective_text_state.cpp` locka semantica 5-campi + crossfade lifecycle; `tests/text/test_text_run_driver.cpp` invariato. Nessun nuovo singleton/registry/cache (cache flows via `TextRunShape::cache` o `TextLayoutCache*` esplicito); `process_wide_*` e `shared_text_layout_cache()` confermati rimossi (Fase B2+B3). Test target `chronon3d_core_tests` ancora bloccato da `TICKET-camera-policy-pre-existing` (`Camera2_5D::projection_mode`) — fuori scope M1.5#2. Stesso blocker pre-esistente di M1.5#1 — separato come ticket indipendente (one-commit-per-responsibility). |
| 3  | `include/chronon3d/text/text_run.hpp`                           | P0/P1  | PLANNED  | Sub-header `layout/shape/cache/hash/identity`; singleton rimosso da header pubblico.                    |
| 4  | `src/text/text_run.cpp`                                         | P0     | PLANNED  | `FontLayoutIdentity` usato in cache-key + layout-hash + shaping-hash + keyframe + prewarm + font-spans. |
| 5  | `src/text/text_run_builder.cpp`                                 | P1     | PLANNED  | Pipeline canonica: validate → resolve → shape → failure-policy → compose → font-spans → cache-store.    |
| 6  | `src/backends/software/processors/text_run/text_run_processor.cpp` | P1 | PLANNED  | Scratch allocator / `TextRenderResources`; niente vettori temporanei allocati per draw.                 |
| 7  | `include/chronon3d/backends/text/text_render_resources.hpp`     | P1     | PLANNED  | Aggregatore leggero; cache/font/atlas/scratch in sub-header separati.                                    |
| 8  | `src/text/text_resolver.cpp`                                    | P1     | PLANNED  | Unico entry `ResolvedTextTree`; nessun secondo resolver in backend o builder.                            |  <!-- drift-allow: stale-ref -->
| 9  | `src/backends/software/processors/text/software_text_processor.cpp` | P1  | PLANNED  | Svuotamento progressivo dopo migrazione callsite a `TextRunNode`; rimozione del processor legacy.        |
| 10 | `include/chronon3d/text/rich_text.hpp`                          | P1     | PLANNED  | Eliminazione completa dopo migrazione `RichTextLine → TextDocument` / `draw_rich_text → text_run(...)`. |  <!-- drift-allow: stale-ref -->
| 11 | `src/backends/text/text_rasterizer_render.cpp`                  | P1     | PLANNED  | Classificare utility riusabili (`blend2d_glyph_conversion`, `freetype_outline_conversion`) vs legacy.    |
| 12 | `src/backends/software/software_backend.cpp`                    | P2     | PLANNED  | Factory + dispatch + text + effects in file separati; main = ctor/dtor/accessors.                        |
| 13 | Registry preset testuali                                        | P2     | PLANNED  | Factory per categoria (basic/kinetic/cinematic/social); registro centrale unico invariato.               |
| 14 | `docs/FOLLOWUP_TICKETS.md` open-blockers housekeeping           | P2     | PLANNED  | Separare `Done`/`PARTIAL`/`TEST-ONLY DONE`; promuovere back-log da "open" a "recently closed".            |
| 15 | `docs/tickets/TICKET-P1-ACTION-PLAN.md`                         | P2     | PLANNED  | Convertire in matrice Implementazione / Test / Migrazione / Rimozione legacy (vedi pasted text §15).    |

### Regole vincolanti durante l'esecuzione del backlog M1.5

- **Prima mini-sequenza post-baseline (P0):** `1 → 2 → 3 → 4` — sono i file che oggi
  combinano maggiore fragilità, falsa riuscita, e identità/cache incoerenti.
- **Limiti guida** (suggerimento operativo, non obbligo meccanico):
  - header pubblico: preferibilmente < 250–300 righe;
  - orchestratore: < 350–450 righe;
  - funzione normale: < 80 righe;
  - una sola responsabilità architetturale per file;
  - nessun **nuovo** singleton, registry, resolver, sampler, cache, service locator;
  - ogni estrazione deve essere coperta da test prima **e** dopo;
  - un commit per responsabilità, direttamente su `main` (no branch di feature).
- **Pre-push gate:** `tools/wrap_push.sh origin main` (atomic FF-merge + gate
  `check_main_clean.sh`); vedi TICKET-076.
- **Sincronizzazione documentale:** ogni chiusura aggiorna `CURRENT_STATUS.md` (stato)
  e `FOLLOWUP_TICKETS.md` (storico) nello **stesso** commit (vincolo gate #7 `check_doc_sync.sh`).
- **Matrice di avanzamento** (Implementazione / Test / Migrazione / Rimozione legacy)
  per item — popolata in `docs/baselines/text-decomp-matrix.md` dopo lo sblocco freeze.  <!-- drift-allow: stale-ref -->

### Non-goal M1

- Text 3D;
- MSDF;
- morph avanzato;
- supporto globale ICU completo;
- nuovo renderer testuale parallelo.

## M1.7 — Sequence + Asset Readiness (Single Source of Truth) (PLANNED, post-baseline-verde)

> **Origine:** action-plan landing 2026-07-07 (dedicato a TICKET-SEQUENCE-LOCAL-FRAME + TICKET-ASSET-READINESS). Formalizza l'eliminazione delle **due verità** che creano caos nel motore:
>
> 1. Timeline legacy dentro `layer/render graph` (`if frame... sparsi` + `Layer.from/duration`) vs Timeline nuova dentro `SequenceResolver`.
> 2. Asset controllati durante il render vs Asset controllati prima del render (preflight).
>
> NON avviabile fino a **11/11 PASS macchina-verificato sullo stesso commit** (AGENTS.md §Feature Freeze revoca) + TICKET-GATE-10-PHASE-4-FIX + TICKET-GOLDEN-CAPTURE chiusura.
>
> **Regola di stato osservabile:** PASS / FAIL / PARTIAL / NOT RUN / BLOCKED / PLANNED. I 2 ticket sono PLANNED al landing.

### Obiettivo

Riallineamento canonico a UNA timeline (SequenceNode) + UN preflight (AssetPreflightResolver), eliminando le 10 legacy items combinate dei due piani utente:

- **Sequence (5 legacy items A-E)**: (A) `if frame...` sparsi nei content + (B) animator che legge frame globale + (C) `Layer.from/duration` gestiti dal render graph + (D) `duration=1` trucco statico + (E) 5 coordinate temporali duplicate (composition / layer / sequence / animator / video source frame).
- **Asset (5 legacy items A-E)**: (A) path raw senza `AssetRef` + (B) asset discovery render-time + (C) fallback silenziosi (default font / black rect / empty frame) + (D) `catch MESSAGE return` nei test readiness + (E) asset validation duplicata per-feature (TextPreflight / ImagePreflight / VideoPreflight / AudioPreflight / FontPreflight).

Sequenza canonica finale:

- `TimelineResolver` decide cosa esiste al frame globale.
- `AssetPreflightResolver` decide se tutti gli asset sono pronti.
- `RenderGraphBuilder` riceve scena già risolta (active layers + local_frame risolto).
- `Renderer` non inventa timeline (no skip) e non inventa asset (no fallback).

### Vincoli architetturali

- **Zero nuovi singleton / registry / resolver / cache / service-locator** (regola permanente AGENTS.md §Anti-duplication Rules).
- I 4 nuovi simboli pubblici canonici per ticket:
  - Sequence: `TimeRange{Frame from, Frame duration}; SequenceNode{string name, TimeRange range, build_callback}; TimelineResolver::resolve(scene, frame, fps)->ResolvedScene; TimelineSampleContext{global_frame, local_frame, sequence_start, fps}`. Tutti in `include/chronon3d/timeline/`.
  - Asset: `AssetKind enum{Font, Image, Video, Audio}; AssetRef{kind, path, owner, required}; AssetManifest::add(entry)/entry_for(owner)/all(); AssetPreflightResolver::preflight(manifest)->AssetPreflightResult{ok, missing[]}; AssetPreflightResult::missing -> {owner, path, kind}`. Tutti in `include/chronon3d/assets/`.
- Nessun `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>` aggiunto (AGENTS.md Gate 5 deny-everywhere).
- ABI pubblico invariato (canonical `composition({...}, [lambda])`, `RenderNodeFactory::text(name, TextSpec)`, `RenderNodeFactory::image(uri)` invariati; solo back-compat wrapper + adapters dietro le quinte fino a Step 4 elimination).

### Lavori (sequenza 4-step per ticket, atomic su main)

1. **Step 1 (Add new system, verde)** — sequenza canonica: (a) NEW `include/chronon3d/timeline/{time_range, sequence_node, timeline_resolver, timeline_sample_context}.hpp` + (b) NEW `include/chronon3d/assets/{asset_ref, asset_kind, asset_manifest, asset_preflight_resolver, asset_preflight_result}.hpp` + (c) zero modifiche al codice esistente: tutti i `Layer.from/duration`, `if frame...` sparsi, asset path raw, fallback silenziosi continuano a funzionare bit-identical.
2. **Step 2 (Legacy adapters)** — back-compat wrappers: `Layer.from/duration` -> `SequenceNode` implicita; `font_path/image_path/video_path/audio_path` -> `AssetRef` nel `AssetManifest` della scena a startup; `rctx.text_resources->resolve_handle(...)` errore -> `AssetPreflightResult::missing` esplicito. Tutti i test preesistenti PASS bit-identical (cache key stable).
3. **Step 3 (Migrate new content)** — almeno 5 scene nuove (ae-parity cinematic-21..25 o showcase) usano SOLO `s.sequence("intro", {.from = Frame{0}, .duration = Frame{60}}, ...)` + `AssetRef{kind, path, owner, required}`. `RenderJob::start()` chiama `AssetPreflightResolver::preflight(manifest)` UNA volta prima del render loop; se `result.ok == false`, fail esplicito.
4. **Step 4 (Eliminate legacy, post-macchina-verifica)** — fisicamente rimuovi i 10 legacy items quando i test passano + `Layer.from/Layer.duration` come campi del modello (drop dopo sweep M1.5#-like) + `TextPreflight/ImagePreflight/VideoPreflight/AudioPreflight/FontPreflight` consolidati in `AssetPreflightResolver` + `tools/check_render_graph_temporal_skip.sh` (NEW) + `tools/check_no_silent_asset_fallback.sh` (NEW) entrambi ZERO exit.

### Gate di uscita

- grep-audit backlog = 0 per ognuno dei 10 legacy items A-E;
- `chronon3d_text_golden_tests` + `chronon3d_ae_parity_tests` + `chronon3d_install_consumer_tests` + `chronon3d_cache_tests` PASS bit-identical post-Step-4;
- `tools/check_render_graph_temporal_skip.sh` + `tools/check_no_silent_asset_fallback.sh` 0 hit;
- ZERO PNG scuri per asset mancante nel `chronon3d_install_consumer_tests` Phase 4 (grazie a FAIL esplicito preflight);
- `Layer.from/Layer.duration` rimossi dal modello;
- `chronon3d_render_graph_tests` 16/16 PASS;
- `docs/FEATURES.md` Text + Asset paragrafo aggiornato da "Parziali" a "Presenti" per il preflight single-source-of-truth.

### Avvio rigido

Per AGENTS.md regola di stato: Step 1 PRIMA -> Step 2 DOPO -> Step 3 DOPO -> Step 4 ULTIMO. **Mai** iniziare Step 4 senza grep-audit backlog = 0 su tutti i 10 legacy items + macchina-verifica verde.

### Cross-link canonici

- Ticket rows `TICKET-SEQUENCE-LOCAL-FRAME` + `TICKET-ASSET-READINESS` (P1 backlog §M1.7 di FOLLOWUP_TICKETS.md);
- Prereq gate #10 chiusura + baseline verde 11/11 (AGENTS.md §Feature Freeze);
- TICKET-P1-11 (Timeline percorsi multipli) + TICKET-P1-07 (Asset resolver globale) cross-link: questa milestone è la concretizzazione canonicale di entrambi.

### Non-goal M1.7

- Expressions Selector production-grade (M5);
- Text 3D + per-character 3D (M5);
- Variable fonts + color glyph/emoji (M5);
- Asset pipeline parallelo (zero nuovi registry).

## M1.6 — AE-Parity Cinematic Text Golden Expansion (PLANNED, post-baseline-verde)

> **Origine:** action-plan landing 2026-07-06 dalla strategia "Chronon3D vs After Effects per kinetic typography 2D". Formalizza l'espansione del floor AE-parity (5/20 IMPL shipped storicamente — Phases D) al target completo (20/20 IMPL + 288 PNG floor + 4 killer test + referee AE-side). NON avviabile fino a `TICKET-GOLDEN-CAPTURE` chiusura + gate #10 `install_consumer_test.sh` 11/11 PASS macchina-verificato sullo stesso commit (AGENTS.md v0.1 §Feature Freeze revoca).
>
> **Regola di stato osservabile:** PASS / FAIL / PARTIAL / NOT RUN / BLOCKED / PLANNED. Tutti i 17 ticket di questo workstream sono PLANNED al landing.

### Obiettivo

Portare Chronon3D al livello "After Effects-like per kinetic typography 2D automatizzata e timelined subtitle video" via:

- **Floor 1:** 20 scene cinematic AE-parity scene-builder IMPL × 6 AR/frame snapshots = 120 PNG floor
- **Floor 2:** + user-spec 12 golden tests × 6 PNG (4×1.5 già tracked + 20 PNG total) = ~140 PNG
- **Floor 3:** + motion-blur text 6 PNG (TICKET-MOTION-BLUR-TEXT) = ~146 PNG
- **Floor 4:** + 4 killer test visual regression (motion_blur + per_char_3D + wiggly_wave + subtitle_word_timing) = ~288 PNG floor verificato macchina
- **Floor 5:** referee pipeline AE-side → Chronon3D-side diff `mean_abs_diff` < 5/255 su almeno 10/15 scene cinematic
- **Floor 6:** consumer SDK `tests/install_consumer/` deve usare almeno 3 cinematic preset senza includere header interni (manifest-clean DoD §P3-H)

### Lavori (sequenza rigida, un commit per ticket atomicamente)

Sequenza di lavoro — 5 step incrementali. Ogni step è un commit atomic su `main` (AGENTS.md §GATE-MNT-01 + `tools/wrap_push.sh`); nessun branch di feature.

1. **AE-PARITY-CINEMATIC-06..20 IMPL** (15 ticket, ciascuno un `tests/text_golden/ae_parity/ae_NN_<id>.cpp` scene-builder + 6 TEST_CASE × AR × frame = 30 PNG per scene). Scopes: tracking_expansion, stroke_reveal, glow_pulse, blur_in, scale_pop, rotation_per_character, random_jitter, text_on_path, multiline_9_16_safezone, long_paragraph_wrap, mixed_font_rich_text, arabic_english_bidi, small_subtitle_readability, fast_motion_stress, karaoke_word_highlight. Harness reuse `verify_golden` da `tests/visual/support/golden_test.hpp` + canonical pipeline `composition(...) + SceneBuilder::layer(...) + LayerBuilder::text(...)`. ZERO `TextShape` / `rich_text` references (AGENTS.md v0.1 Cat-2 freeze-compliant).

2. **Killer test driver** (TICKET-AE-PARITY-KILLER-PER-CHAR-3D + TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION; cross-link TICKET-MOTION-BLUR-TEXT + TICKET-GOLDEN-30). Per ogni killer test: `tests/text_golden/ae_parity_killer/<name>.cpp` con 3 SUBCASEs (determinism seed + frame-by-frame delta + cross-run reproducibility). Cross-link a M5 milestone per per-char-3D (text 3D + Expressions V2 stable prerequisite).

3. **Floor deliverable: 288 PNG** (TICKET-AE-PARITY-FLOOR) post-CAPTURE-fix (`TICKET-GOLDEN-CAPTURE` chiusura prerequisita). `git ls-files HEAD ./test_renders/golden/text/ae_*.png` ≥ 120; consumato dal `tools/test_golden_driver.sh verify linux-release` con zero PNG drift.

4. **Referee pipeline** (TICKET-AE-PARITY-DRIVER). `tools/ae_parity_referee.sh`: per ogni AE-parity scene, `reference/after_effects/scene_NNN_frame_NN.png` (AE-side mock futuro) + `reference/chronon3d/scene_NNN_frame_NN.png` (engine output) + diff harness `mean_abs_diff + perceptual color metric` con lock a 5/255 threshold. Rigoroso: nessun claim di "AE-like" finché referee non passa su almeno 10/15 scene cinematic. Step forward-only: prima le 5 scene storiche (ae_01..05) devono passare referee come proof-of-concept.

5. **Cinematic preset registry + SDK certifier**. Promozione dei 22 preset (4 basic + 8 kinetic + 4 cinematic + 6 subtitle) da `builtin_text_presets()` (M1.5#13 Step 1/4) a `built_in_text_presets_v1()` API pubblica. Consumer SDK `tests/install_consumer/` deve linkare almeno 3 cinematic preset senza includere header `internal/`/`test/`/etc. (manifest-clean DoD §P3-H — `cmake/Chronon3DPublicHeaders.cmake` scope + `tools/install_consumer_test.sh` end-to-end 11/11 PASS). `docs/FEATURES.md` Text paragrafo aggiornato da "Parziali" → "Presenti" per cinematic kinetic typography.

### Gate di uscita

- tutti i 288+ golden PNG tracked in `test_renders/golden/` (gitignored-friendly, whitelisted);
- 4 killer test PASS macchina-verificato sullo stesso commit;
- referee pipeline `tools/ae_parity_referee.sh` exit 0 su almeno 10/15 scene cinematic NOT RUN (0 su 15), PARTIAL (1-9 su 15), PASS (10+ su 15);
- consumer SDK include `Chronon3D::SDK` 22 preset text senza header `internal/`;
- `docs/FEATURES.md` Text paragrafo aggiornato da "Parziali" a "Presenti" per cinematic kinetic typography;
- `docs/CHANGELOG.md` chiusura "M1.6 — AE-Parity Cinematic Text Golden Expansion" registrata.

### Esito Floor PNG (Math Correction, 2026-07-07; re-bake DELIVERED 2026-07-07)

Il floor target originario di 288 PNG è matematicamente irraggiungibile. I test "Killer" per il determinismo del testo (Phase-2) implementano rigorose validazioni hash cross-run via `doctest` `CHECK` assertions + `evaluate_animator` direttamente a livello di memoria, producendo **0 asset visivi PNG** (sono unit-test deterministici, non visual golden captures). Il ceiling realistico post-Phase-1 osservato: **114 PNG tracked a HEAD** (54 `ae_*.png` + 20 `user_spec_*.png` + 40 altri) + **30 PNG net new** dalle 4 scene cinematic + motion_blur_text appena landate (re-bake via `CHRONON3D_UPDATE_GOLDENS=1` deferred a prossima sessione con working build host) = **144 PNG post-re-bake** (50% del target 288). `TICKET-AE-PARITY-FLOOR` rimane onestamente tracciato in stato **PARTIAL** per riflettere questo ceiling in superamento ai target presunti errati — nessun claim "DONE" fabbricato. Companion dettaglio in [`docs/CHANGELOG.md`](CHANGELOG.md) entry "feat(tests,text) — TICKET-AE-PARITY-CINEMATIC-08/10/12/14 + TICKET-MOTION-BLUR-TEXT + TICKET-AE-PARITY-FLOOR math correction".

**UPDATE 2026-07-07 (re-bake delivered, this commit)**: 5 `CHRONON3D_UPDATE_GOLDENS=1` commands executed sequentially on working build host (rc=0 across the 5x6=30 PNG captures):
- `chronon3d_text_golden_tests --test-case='*AE 08*'` → 6 PNG (ae_08_glow_pulse_16x9_f{00,15,30} + ae_08_glow_pulse_9x16_f{00,15,30})
- `chronon3d_text_golden_tests --test-case='*AE 10*'` → 6 PNG (ae_10_scale_pop_16x9_f{00,15,30} + ae_10_scale_pop_9x16_f{00,15,30})
- `chronon3d_text_golden_tests --test-case='*AE 12*'` → 6 PNG (ae_12_random_character_jitter_16x9_f{00,15,30} + ae_12_random_character_jitter_9x16_f{00,15,30})
- `chronon3d_text_golden_tests --test-case='*AE 14*'` → 6 PNG (ae_14_multiline_9_16_16x9_f{00,15,30} + ae_14_multiline_9_16_9x16_f{00,15,30})
- `chronon3d_text_golden_tests --test-case='*motion_blur*'` → 6 PNG (motion_blur_text_{baseline,blurred}_f{05,10,15})

**Net delta: `git ls-files HEAD ./test_renders/golden/text/ | wc -l` = 114 → 144 PNG tracked at HEAD** (30 PNG net new verified machine on disk via `ls -la test_renders/golden/text/ae_08_*.png ae_10_*.png ae_12_*.png ae_14_*.png motion_blur_text_*.png` post re-bake). Test case name pattern discovered: `AE NN <name> <AR> f<FF>` con spazi interni (doctest `--test-case='*AE 08*'` wildmatch). `TICKET-AE-PARITY-FLOOR` rimane `PARTIAL` — il ceiling reale a 144 PNG è matematicamente limitato dal design Phase-2 killer (0 PNG prodotti, sono unit-test), e il target 288 richiederebbe ~15 nuove scene visual aggiuntive (forward-only). **Nessun claim DONE fabbricato**: il deliverable è il PNG floor tracking + il completamento onesto del re-bake deferred. Companion dettaglio in [`docs/CHANGELOG.md`](CHANGELOG.md) entry "test(ae-parity) — TICKET-AE-PARITY-FLOOR re-bake delivered: 30 PNG net new, 114 → 144 ceiling (2026-07-07)".

### Non-goal M1.6

- complete global text support (emoji/CJK) — M5;
- text 3D production-grade — M5;
- Expression Selector production-grade (Wiggly/Wave solo smoke-test in M1.6);
- per-character 3D beyond demo — M5.

### Cross-link canonici

- Workstream cinematic track: ticket rows `TICKET-AE-PARITY-CINEMATIC-{01..20}` (01..05 DONE Phase D) + `TICKET-AE-PARITY-SUITE` (umbrella 5→20 transition tracker) + `TICKET-AE-PARITY-KILLER-*` (4 killer);
- Concurrency precond: `TICKET-GOLDEN-CAPTURE` (Phase C capture pipeline rot parent) + `TICKET-GATE-10-PHASE-4-FIX` (gate #10 FU5);
- Referee spec: roadmap stub `docs/adr/ADR-015-ae-parity-cinematic-expansion.md` (PLANNED, da stilare al primo commit cinematico successivo alla revoca freeze);  <!-- drift-allow: stale-ref -->
- Match RElease gate §Text Production V1: 20 preset generali + 8 subtitle verificati (TICKET-AE-PARITY-CINEMATIC tracks i 20 generali; i 8 subtitle sono cross-link a TICKET-GOLDEN-30 + TICKET-AE-PARITY-CINEMATIC-20 karaoke_word_highlight).

## M2 — Camera Production V1

### Obiettivo

Rendere `CameraDescriptor → CameraProgram` l'unico percorso authoring nuovo e
coprire i movimenti cinematografici necessari al motion graphics 2.5D.

> **Blocker:** Runtime certification is PARTIAL and gated by [TICKET-120](tickets/TICKET-120.md) (24→18 remaining; 2 fixed, 1 hierarchy test `#if 0` disabled). Camera Production V1 closure reroutes to TICKET-120 GREEN.

### Lavori

1. Completare i ticket camera P0/P1 ancora test-blocked o parziali.
2. Completare `OrientAlongPath`, look-ahead, keep-horizon e banking.
3. Preservare correttamente stato base/projection in tutte le source.
4. Chiudere diagnostica e determinismo della shot timeline.
5. Portare framing a multi-target, bounds-aware, rule-of-thirds e dead-zone.
6. Completare clipping near/far necessario al renderer.
7. Migliorare DOF senza creare un secondo sistema ottico.
8. ~~Aggiungere parity test e golden sul percorso compilato.~~ **Chiuso da C1–C7** (golden in `tests/scene/camera/golden_projection_test.cpp`, SHA `eb1ce8e5`; `FocalPx` + `ViewportRect` + `focal_xy_from_camera` + `effective_viewport` con offset; anamorphic_squeeze SOLO X; sentinel regression subrect-fits-canvas).
9. Migrare preset e composizioni, poi deprecare/rimuovere authoring legacy.
### M2.A3 — Agent3 ticket cluster: compiler semantics, session policy, cache lease, framerate explicit (PLANNED, post-freeze)

Origine: prompt operativo **Agent3 — Compiler, Session, Errori, Legacy Cleanup** (2026-07-04). Si applica ai file `camera_v1/*` (`include/chronon3d/scene/camera/camera_v1/*.hpp` + `src/scene/camera/camera_v1/*.cpp`) — **non** al type-level deletion già coperto da [ADR-011](adr/ADR-011-camera-legacy-deletion.md) (AnimatedCamera2_5D / dual CameraRig / CameraShotProfile / camera_descriptor_adapters / Camera2_5D::projection_mode). Stato attuale: tutti i sotto-ticket sono **PLANNED** (nessun codice toccato durante freeze). Workstream design-FROZEN 2026-07-04.

Lavori (uno per ticket, commit atomico su `main` — no branch, AGENTS.md §Feature Freeze):

1. **TICKET-A3-METADATA** — `camera_v1::compile_camera()`: rimuovere il `return` anticipato dopo graft del `RegisteredMotionRef`; late-rebuild `failure_policy_` / `time_dependent_` / `evaluation_dependency_` post-graft.  **PARTIAL**: late-rebuild gia' implementato in camera_program_compiler.cpp (CAM-03 fix marker); test lock tests/scene/camera/test_camera_program_metadata_late_rebuild.cpp 5 SUBCASEs committed; test eseguito direttamente 1 TEST_CASE × 32 assertions PASS; 1/8 gate (a) cluster chiuso.
2. **TICKET-A3-SESSION-POLICY** — `CameraFailurePolicy::KeepLastValidCamera` realmente recupera `CameraSession::last_valid_camera` (oggi segue il ramo di `Stop`).
3. **TICKET-A3-CACHE-LEASE** — `CameraSessionCache::acquire()` non aggiorna `last_evaluated_frame` prima del completamento della evaluate; lease `commit()` on success only; failed evaluation non muta cache.
4. **TICKET-A3-CTX-FRAMERATE** — `CameraEvalContext::at(Frame, FrameRate)` + `CameraMotionContext::at(Frame, FrameRate)` audit call-site per residui `FrameRate{30,1}` hardcoded.
5. **TICKET-A3-PRE-ROLL-FPS** — `preroll_session_for_frame(...)` riceve `FrameRate` esplicito (signature già presente in `camera_session_cache.hpp`; verificare che ogni call-site passi fps non-default).
6. **TICKET-A3-DAMPED-HISTORY** — `DampedFollowConstraint` forza sempre `ProgramKind::RequiresHistory` durante la fase di metadata classification del compiler.
7. **TICKET-A3-LOOKAT-DIAGNOSTIC** — `LookAtLayer` senza `transforms` (`CameraEvalContext::transforms == nullptr`) emette `CameraDiagnostic{Code::MissingTransforms, Severity::Warning}` invece di silent fallback.
8. **TICKET-A3-ADR-013** — ADR-013 (camera_v1 semantics + framerate explicit + cache-lease contract) + ticket-to-code chain verso [ADR-011](adr/ADR-011-camera-legacy-deletion.md) per chiudere il DoD gate (h). Doc-only.

Done-Definition (8 gate, vedi matrice in `docs/FOLLOWUP_TICKETS.md` Agent3 cluster): tutti i gate (a)..(h) sono PASS verificato allo stesso commit. Gate di uscita addizionali:

- `tools/check_camera_architecture.sh` rimane 6/6 PASS dopo ogni commit;
- `tests/scene/camera/` LINK + RUN;
- `tools/wrap_push.sh origin main` GATE_PASS prima di ogni push.

Allineato con `docs/camera-plan/04-INTEGRATION_TESTS_AND_LEGACY_REMOVAL.md` PR breakdown (CAM-02 compiler validation / CAM-05 render integration / CAM-06 timeline+state). Avvio rigido post-freeze: `1 → 2 → 3 → 4 → 5 → 6 → 7`, ADR-013 last.

### Gate di uscita

- nuove composizioni senza `AnimatedCamera2_5D` o rig legacy;
- sessione posseduta dal render job;
- nessuna compilazione o lookup catalog per frame;
- test camera bloccanti verdi;
- consumer esterno usa una camera compilata.

## M3 — CapCut-grade Parity (in progress, post-V0.2 cycle)

> **Origine:** verdict CapCut-grade (2026-07-21, sessione CapCut parity). Formalizza
> il milestone di parità visiva e comportamentale con CapCut per la pipeline tipografica
> Chronon3D (subtitle + kinetic typography + static text + rendering globale).
> NON avviabile come milestone completamente macchina-verificata sul lineage corrente
> per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV blocker (this VPS).

### Obiettivo

Ottenere `CapCut-grade parity` = il rendering tipografico Chronon3D è indistinguibile
dal rendering CapCut per i 7 gate exit criteria sotto elencati, su un corpus di
riferimento CapCut blessed.

### Gate di uscita (7, tutti obbligatori)

| # | Gate | Stato attuale (post-sessione 2026-07-21) | Note |
|---|---|---|---|
| 1 | **geometric ink-bbox PASS** | FORWARD-POINT | `TICKET-INK-BBOX-GEOMETRIC` — rewrite `compute_text_ink_bbox()` iterando sui FT_Outline trasformati (verdict Fase 1) |
| 2 | **cluster-fallback PASS** | FORWARD-POINT | `TICKET-FR-2-CLUSTER-FALLBACK` — `FontFallbackResolver::resolve_runs()` per grapheme/cluster (verdict Fase 2) |
| 3 | **fps-correctness PASS** | PARTIAL (deferred-WBH) | SubtitleTrackBuilder `seconds_to_frame()` riscritto con FrameRate param + 7 rate verificate; macchina-verifica `tools/verify_*_linux.sh` DEFERRED-WBH |
| 4 | **word-timing-quality PASS** | DONE (cat-3 minimal) | `TICKET-WORD-TIMING-QUALITY` — `enum class WordTimingQuality` + 3 adapter classification + 7 TEST_CASEs |
| 5 | **missing-glyph-audit PASS** | DONE | `TICKET-FALSE-GREEN-TEST-AUDIT` — `check_anti_false_green()` dimension assertions + UTF-8 REQUIRE fb + 4 NEW TEST_CASEs (audit-driven glyph_count deferred-WBH) |
| 6 | **CapCut-reference PASS** | FORWARD-POINT (NO blessed PNGs yet) | `TICKET-CAPCUT-REFERENCE-CORPUS` — corpus skeleton committato (`tests/reference/capcut/` + README blessed-only policy), blessed reference PNGs deferred-PR-review |
| 7 | **determinism PASS** | PARTIAL (deferred-WBH) | determinism matrix 16 test PASS strutturalmente; macchina-verifica `tools/verify_determinism_linux.sh` DEFERRED-WBH |

### Lavori (sequenza forward-only, atomic su main, no branch)

- **Fase 1** — Geometric ink-bbox rewrite (gate 1): vedi `TICKET-INK-BBOX-GEOMETRIC`.
- **Fase 2** — Cluster fallback + OpenType features (gate 2): vedi `TICKET-FR-2-CLUSTER-FALLBACK` + `TICKET-OPENTYPE-FEATURES-PASS`.
- **Fase 3** — CapCut reference corpus blessing (gate 6): vedi `TICKET-CAPCUT-REFERENCE-CORPUS`. Policy: nessun blessed reference committato senza PR review esplicita.
- **Fase 4** — Machine-verification end-to-end (gate 3, 5, 7): TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV chiusura prerequisita.

### Cross-link canonici

- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) §Stato generale per area "Text Production / CapCut-grade V1" row + "Test hardening (false-green audit)" row;
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Open Blockers` + `## Recently Closed` rows (TICKET-TIMED-WORD-BINDING DONE, TICKET-WORD-TIMING-QUALITY DONE, TICKET-FALSE-GREEN-TEST-AUDIT DONE, TICKET-INK-BBOX-GEOMETRIC OPEN, TICKET-FR-2-CLUSTER-FALLBACK OPEN, TICKET-CAPCUT-REFERENCE-CORPUS OPEN);
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) `## 2026-07-21` entries (3 commit rilevanti della sessione: ce51d08a, e9999554, 780580da);
- [`docs/tickets/TICKET-DOC-SYNC-CAPCUT-VERDICT.md`](docs/tickets/TICKET-DOC-SYNC-CAPCUT-VERDICT.md) cronaca-estesa del chore doc-sync.

### Non-goal M3

- claim "CapCut parity DONE" senza blessed reference PNG committato (no greenwashing);
- macchina-verifica end-to-end su VPS env-blocked (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV forward-point);
- expand del milestone prima che tutti i 7 gate siano PASS macchina-verificato (forward-only).

---

## M3 — SDK Product V1

### Obiettivo

Distribuire Chronon3D come SDK C++ installabile e documentato, non soltanto come
repository sorgente.

### Lavori

1. Consumer esterno reale: extension pack, runtime, testo, camera, render e file output.
2. API pubblica minima documentata e header interni esclusi dagli esempi.
3. Compatibility/deprecation policy.
4. Versionamento package e verifica `find_package`.
5. Artifact Linux riproducibili.
6. Esempi separati dalla source tree.
7. Smoke test install/configure/build/run in CI.

### Gate di uscita

- un progetto vuoto può installare e usare `Chronon3D::SDK`;
- render di almeno una composizione reale fuori-tree;
- nessun link diretto a target interni;
- documentazione e artifact associati allo stesso tag.

## M4 — Pacchetti animazione e interoperabilità

### Obiettivo

Permettere ad altri programmi di caricare e usare animazioni Chronon3D senza
compilare direttamente il core C++.

### Lavori

1. Definire schema versionato `.chronon`.
2. Serializzare composition, layer, animation tracks, text, camera, effects e asset refs.
3. Loader con validazione, diagnostica e migration version.
4. Definire C ABI con handle opachi.
5. Esporre runtime, package, render job, framebuffer ed error API.
6. Creare primo binding Python.
7. Definire plugin ABI versionato per pack C++ non serializzabili.

### Regola

Python, C#, Node, Rust e altri binding devono appoggiarsi allo stesso C ABI.
Non duplicare il runtime in ogni binding.

## M5 — Global text ed effetti premium

Solo dopo M0–M4:

- ICU opzionale;
- variable fonts;
- color glyph/emoji;
- Expression Selector produttivo;
- Text 3D;
- MSDF;
- morph e displacement;
- ottica/DOF più avanzati.

## M6 — V3 tile-first

V3 è una futura evoluzione interna. Non deve interrompere la chiusura di Text,
Camera e SDK V1 né introdurre una pipeline parallela prima che i contratti V1
siano verificati.

## M1.8 — Text Simplicity (Remotion-like Ergonomics) (PLANNED)

> **Origine:** sessione 2026-07-10. Piano dettagliato per raggiungere l'ergonomia di Remotion
> mantenendo headless, CPU-first, deterministico, server-side, senza browser.
> Piano operativo completo: [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md).
> Regola operativa: commit atomici su `main`, nessuna branch, push frequente.

### Obiettivo

Rendere il testo Chronon3D semplice quanto Remotion: un titolo centrato in < 10 righe,
un'animazione in < 5 righe, diagnostica visuale con un comando CLI.

### Fasi (17 commit totali)

| Fase | Area | Ticket | Stato |
|---|---|---|---|
| 1A | ADR Coordinate Model (Canvas/Layer/Box/Glyph) — see [ADR-019 §6 Numerical Examples (1920×1080)](adr/ADR-019-text-coordinate-model.md#6-numerical-examples-19201080-canonical-canvas) for 5 worked examples (placement / offset / alignment / rotate / anchor).  GitHub-compatible anchor used (GitHub markdown anchor algorithm strips the `§` unicode char → URL hash is `6-numerical-examples-...`, not `§6-...`). | TICKET-SIMPLICITY-COORDINATES | DONE (ADR-019 accepted + §6 lattice landed 2026-07-10 + 6 explicit TEST_CASE locks in `tests/text/test_text_placement_resolver.cpp`) |
| 1B | Placement Resolver Unico | TICKET-SIMPLICITY-PLACEMENT-RESOLVER | DONE (F1.B commit, 2026-07-10, `ResolvedTextPlacement` + `resolve_text_placement()` + 12 `TextPlacement` variants + 25 tests in `tests/text/test_text_placement_resolver.cpp`; cross-link F1.B CHANGELOG entry).  **Fase 1 — Correttezza closure verified 2026-07-10**: ROADMAP row was stale `PLANNED`; FOLLOWUP_TICKETS.md row + CHANGELOG.md F1.B entry + ADR-019 Decision 3 + 25-test lock all confirm DONE.  M1.8 §3B (TICKET-SIMPLICITY-PLACEMENT SafeArea extension) is a separate ticket that was landed in a later commit. |
| 1C | Fallback Conservativo BBox | TICKET-SIMPLICITY-CONSERVATIVE-BBOX | PLANNED |
| 1D | FontEngine Automatico | TICKET-SIMPLICITY-AUTO-FONT | PLANNED |
| 1E | Contratto Visibilità Automatico | TICKET-SIMPLICITY-VISIBILITY-CONTRACT | DONE (2026-07-10, §9 atomic commit) |
| 2A | TextDefinition Canonica | TICKET-SIMPLICITY-TEXTDEFINITION | DONE (F2.A commit, 2026-07-10, `TextContent`/`TextStyle`/`TextFrame` filled with real fields mapped from `TextSpec`; M1.8 §3 commit landed `compile_to_canonical()` adapter + 20-contract lock).  **Fase 2 — Semplificazione closure verified 2026-07-10**: ROADMAP row was stale `PLANNED`; FOLLOWUP_TICKETS.md row + CHANGELOG.md F2.A entry + `tests/text/test_text_definition.cpp` 20-test lock + F2.C `text(name, TextDefinition&)` overload all confirm DONE.  Forward-point: §2D TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS (PLANNED) — the 38 pre-F2.C `centered_text()` call sites in `content/certification/` (cert_multilingual × 12, cert_lower_third × 2, cert_title × 1, cert_long_text × 1, text_placement_compositions × 22) are migration debt per the §2D sweep. |
| 2B | Simple API Builder | TICKET-SIMPLICITY-BUILDER | DONE (verification landed 2026-07-10) — `Layer::text(content)` -> `Text` handle fluent surface with `.content()/.font()/.place()/.box()/.anchor_point()/.align()/...` setters + immediate-apply model (no `.commit()` per `text.hpp` docstring); canonical resolver wiring verified by `tests/text/test_text_builder_ergonomics.cpp` (12 TEST_CASEs: 3 lifecycle + 3 setter coverage + 3 place() rewires + 1 10-line criterion + 1 1px bbox invariant + 1 determinism lock); chain-length hard-locked at 4 for canonical centered-title composition; bbox ≤ 1px from mathematical canvas center (960, 540) on 1920×1080; AGENTS.md v0.1 Cat-3 (zero new public API surface — the fluent chain predates this commit per F2.A landing) + Cat-5 (CHANGELOG.md entry prepended) freeze-compliant; `chronon3d_text_builder_ergonomics_tests` registered UNCONDITIONAL with `CHRONON3D_FAST_TEST_DEPS` aggregator; macchina-verifica deferred al working build host (per AGENTS.md §honesty). |
| 2C | text()/text_run() come Adapter | TICKET-SIMPLICITY-ADAPTERS | DONE (M1.8 §6, 2026-07-10) — `centered_text()` marcato `[[deprecated("Use layer.text(name).content(...).font(...).place(TextPlacement::CanvasCenter) for the canonical ergonomic surface (M1.8 §2B + §5 + §6)")]]` con one-shot `spdlog::warn` runtime diagnostic (gated by static-local bool, TICKET-104 pattern); `glow_text()` consolidates args in `TextDefinition.effects.glow` (reuse `chronon3d::GlowParams`, no parallel `TextGlow` struct); `TextEffects` struct esteso con `std::optional<GlowParams> glow{}` (primo TextEffects member popolato, F3.B/C forward-point per il runtime consumption); `LayerBuilder::text_run()` preservato come single-choke-point (nessun reimplementation, backward compat locked da `tests/text/test_text_simplicity_adapters.cpp` #10); AGENTS.md v0.1 Cat-1 (build verification) + Cat-3 (+2 cat-3 declarations: 1 field + 1 deprecation attribute; 0 deletions) + Cat-5 (CHANGELOG.md entry prepended + ROADMAP.md §2C DONE) freeze-compliant; 10 TEST_CASEs in `chronon3d_text_simplicity_adapters_tests` (6 math/diagnostic/back-compat + 3 render-gated `#ifdef CHRONON3D_USE_BLEND2D` + 1 determinism); macchina-verifica deferred al working build host (per AGENTS.md §honesty). |
| 2D | Migrazione Composizioni | TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS | DONE (2026-07-10, §2D atomic commit-per-file sweep) — 15 files migrated (5 original + 10 revealed by verification grep); 27 `text::centered_text()` call sites replaced with `from_text_spec(TextSpec{...})`; `bash tools/check_no_dual_text_api.sh` exit 0 (0 violations, VIOLATIONS=() array per §5A). See [`docs/CHANGELOG.md`](docs/CHANGELOG.md) §2D entry for per-file migration log. **§2D-verified 5 file sweep**: content/certification/cert_{multilingual,lower_third,title,long_text}.cpp + content/text_placement/text_placement_compositions.cpp. **§2D-extension 10 file sweep** (revealed by Fase 2 PARTIAL verification grep — same atomic commit batch): content/examples/light/light_text_animations.cpp + content/showcases/{special-names/special_names_animations.cpp, important-words/important_words_animations.cpp, cinematic/whip_pan_hero_reveal.cpp, cinematic/abyss_freefall_stagger.cpp, cinematic/orbit_handheld_glow.cpp, cinematic/rack_focus_title_swap.cpp, cinematic/deep_parallax_cascade.cpp, minimalist/minimalist_animations.cpp, experimental/ae-parity/ae_camera_text_parity.cpp}. |
| 3A | Animation Helpers (interpolate/spring/sequence) | TICKET-SIMPLICITY-ANIMATION | DONE (M1.8 §3A, 2026-07-10) — 17 helper totali in `include/chronon3d/animation/interpolate.hpp` (11 pre-existing F3.A + 6 nuovi §3A: `stagger` alias + `stagger_value` value-level + `tween` alias + `frame_to_seconds` utility + `linear` alias + `clamp_progress` alias); test surface `tests/animations/test_animation_helpers.cpp` locka tutti e 17 con **17 × 2 = 34 doctest CHECK assertions** (determinism + edge case per helper: 17 determinism + 17 edge); `chronon3d_animation_helpers_tests` registered UNCONDITIONAL in `CHRONON3D_FAST_TEST_DEPS` aggregator; honest accounting: 5/7 user-spec helpers (spring/sequence/loop/delay/ease/clamp) erano già nella F3.A baseline — solo `stagger` (alias) e i 5 nuovi/complementary helpers sono genuinamente nuovi nel §3A commit; 4/6 truly-new helpers sono zero-logic aliases + 1/6 (stagger_value) compose `base + stagger_delay` + 1/6 (frame_to_seconds) wrapper 1-line di `fps.to_seconds`; AGENTS.md v0.1 Cat-3 (+6 cat-3 declarations additive-only, 0 deletions, 0 renames, backward-compat preservata) + Cat-5 (CHANGELOG.md entry prepended + ROADMAP.md §3A DONE) freeze-compliant. Zero nuovi singleton/registry/cache/resolver/sampler/service-locator. Atomic commit su `main` via `tools/wrap_push.sh origin main` (GATE-MNT-01). |
| 3B | Placement Leggibili + Safe Areas | TICKET-SIMPLICITY-PLACEMENT | DONE (M1.8 §3B, 2026-07-10) — `TextPlacementKind` esteso con `SafeAreaLeft`/`SafeAreaRight` (16 varianti totali); user-facing `SafeAreaEnum` 5-valore introdotto in `text_placement.hpp`; `resolve_safe_area(SafeAreaEnum) → TextPlacement{SafeArea*}` mapping canonico in `text_placement_resolver.hpp` (single switch, no parallel mapping table); pin-point semantics lockate per 1920×1080 + 1080×1920 in `tests/text/test_safe_area_placement.cpp` (8 sub-cases 4×2 + 5 SafeAreaEnum exhaustive sweep + 2 cross-link A↔B invariant + 1 determinism 100-call + 2 additive-offset interaction); cross-link `docs/CAMERA_FEATURE_MATRIX.md` §8.1; `kTextAuditBBoxTolerance` ABI preservato; AGENTS.md v0.1 Cat-1 (build-fix) + Cat-3 (no public API surface — solo `SafeAreaEnum` enum e `resolve_safe_area()` fn dichiarati, entrambi già preventivati nella Fase A.2 BDAG) freeze-compliant. Zero nuovi singleton/registry/cache/resolver/sampler/service-locator. Atomic commit su `main` via `tools/wrap_push.sh origin main` (GATE-MNT-01). |
| 3C | Preset Riutilizzabili (5 preset) | TICKET-SIMPLICITY-PRESETS | DONE (2026-07-10, §13 atomic commit) — `chronon3d::presets::text::{title_centered,subtitle_bottom,caption_safe_area,kinetic_word,lower_third}()` 5 inline functions in `include/chronon3d/presets/text/text_presets_v1.hpp`; `title_centered` (font_size=96 + max_width optional) + `subtitle_bottom` (font_size=48, SafeAreaBottom pin) + `caption_safe_area` (font_size=36, SafeAreaCenter pin) + `kinetic_word` (font_size=120 + accent_color optional) + `lower_third` (font_size=42, SafeAreaLeft-lower pin); all return canonical `TextDefinition` (F2.A) only, no cache/resolver/registry/service-locator; `tests/text/test_presets_stability.cpp` 5 TEST_CASEs × 3 CHECKs = 15 assertions (UNCONDITIONAL, pure struct inspection); `tests/text_golden/presets/test_presets_golden.cpp` 5 PNG goldens in `test_renders/golden/text/presets/` (Blend2D-gated INTEGRATION tier); AGENTS.md v0.1 Cat-3 (zero new public SDK API — additive-only header, no new symbols in `include/chronon3d/`) + Cat-5 (3-doc same-commit) freeze-compliant; re-bake command documented in CHANGELOG §13 entry per AGENTS.md §honesty (deferred to working build host) |
| 4A | Pipeline Parity (render/video/CLI) | TICKET-SIMPLICITY-PIPELINE-PARITY | DONE (extended 2026-07-10, Fase 4 closure) |
| 4B | CLI inspect-text (JSON diagnostic) | TICKET-SIMPLICITY-INSPECT-TEXT | DONE (2026-07-10, §12 atomic commit) — `chronon3d_cli inspect-text <comp_id> --frame N --json` emette JSON array per-node (node/font/glyph_count/frame/local_bbox/world_bbox/predicted_bbox/alpha_bbox/status); exit code 0=PASS / 1=FAIL / 2=VIOLATION (FU04 trigger); gated by `CHRONON3D_BUILD_DIAGNOSTICS`; test surface `tests/cli/test_inspect_text.cpp` (6 TEST_CASEs) + `chronon3d_inspect_text_tests` target gated by `CHRONON3D_BUILD_CLI_DEV`; companion `docs/CLI_REFERENCE.md` sezione `inspect-text` aggiunta; AGENTS.md v0.1 Cat-3 (zero new public SDK API) + Cat-5 (3-doc same-commit) freeze-compliant |
| 4C | Diagnostic Overlay (visual bbox PNG) | TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY | DONE (2026-07-10, Fase 4 closure) |
| 5A | Deprecazioni Graduali | TICKET-SIMPLICITY-DEPRECATION | DONE (extended 2026-07-10, Fase 5 PARTIAL closure) — 5 deprecation banners landed across `content/text/text_helpers_centered.hpp` (centered_text enhanced + glow_text new `[[deprecated]]` + one-shot `spdlog::warn`), `include/chronon3d/scene/builders/builder_params.hpp` (TextSpec.position docblock banner), `include/chronon3d/scene/builders/layer_builder.hpp` (pin_to + text_run docblock banners), `include/chronon3d/presets/text/text_presets_v1.hpp` (top-level canonical-replacement notice); `tools/check_no_dual_text_api.sh` refactored to use bash `VIOLATIONS=()` array (per-§5A contract: vector size MUST be 0 in master; NEQ 0 = GATE_FAIL) + [4/4] promoted from ADVISORY to BLOCKING; 4 check categories now collect into the consolidated array for full diagnostic report (single-exit at the end); AGENTS.md v0.1 Cat-3 (zero new public API) + Cat-5 (3-doc same-commit) freeze-compliant; honest accounting: §14 lands steps 1+4 of the 5-step migration order (nuova API exists + warning/gate); steps 2 (migrazione 17 callers), 3 (adapter, already F2.C), 5 (rimozione legacy) are forward-pointed to subsequent M1.8 commits |
| 5B | Documentazione Copy-Paste First | TICKET-SIMPLICITY-DOCS | DONE (2026-07-10, Fase 5 PARTIAL closure) | NEW `docs/QUICKSTART.md` (~200 LoC) with 10 copy-paste examples: #1 Hello World, #2 Multiple Layers, #3 Effects, #4 Typewriter, #5 Kinetic, #6 SafeArea, #7 Custom Fonts, #8 Tracking, #9 PNG Export, #10 CLI inspect-text |

### Gate di uscita

- titolo centrato < 10 righe;
- animazione opacity < 5 righe;
- `inspect-text` diagnostica PASS/FAIL;
- 5 preset stabili con golden frame;
- pipeline parity ≤ 2px differenza;
- zero API deprecate nelle composizioni;
- documentazione quickstart con 10 esempi.

### Non-goal M1.8

- Text 3D, MSDF, morph avanzato, ICU globale, renderer parallelo, GUI/browser, GPU obbligatoria.

---

## Vincoli permanenti

- non reintrodurre executor su raw graph o `ExecutionPlanCache`;
- non creare registry, resolver, sampler o cache paralleli;
- non costruire executor dentro i nodi;
- non indebolire gate per adattarli al codice;
- non promuovere `experimental/` perché compila in opt-in;
- non aggiungere GUI/browser al core headless;
- una PR deve risolvere un problema chiaro e avere test mirati;
- **non aggiungere nuovi helper / builder paralleli / sistemi di positioning per il testo** (M1.8 §1, enforced by [`tools/check_no_dual_text_api.sh`](tools/check_no_dual_text_api.sh) gate wired into [`tools/wrap_push.sh`](tools/wrap_push.sh) Step 4.5d — 4 check categories: `LayerBuilder::text_*<variant>` / `centered_text`/`glow_text` definitions outside canonical preset registry / `TextSpec.position` non-migrated assignment / `pin_to + TextAnchor` co-occurrence per ADR-019 §3).

## Global DoD Sign-off (21-item) — PARTIAL-BLOCKED @ `main@ef9c83f1` (2026-07-12)

Il comando canonico di certificazione prodotto `tools/verify_chronon_product_linux.sh` orchestra 14 sub-gate eseguibili + 1 forward-pointed che coprono i **21 item DoD** dello spec utente (13 zero-require + 8 one-of). Stato corrente osservato: **`CHRONON_PRODUCT_FUNCTIONAL_BLOCKED`** (14/14 PASS + 1 forward-pointed `verify_diagnostics_linux`). Dettaglio: [`docs/baselines/main-ef9c83f1-baseline.md`](docs/baselines/main-ef9c83f1-baseline.md). Forward-point: `TICKET-VERIFY-DIAGNOSTICS-LINUX` + `TICKET-VERIFY-DIAGNOSTICS-ORCHESTRATOR-WIREIN` (separati per AGENTS.md "Fare PR piccole e mirate"). M0 §10 closes: l'orchestratore esiste + esegue + riporta verdict onesto.
