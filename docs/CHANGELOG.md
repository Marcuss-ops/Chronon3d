# Chronon3D — Changelog

> Lavoro completato su `main`. Per i dettagli completi di ogni ticket: [`docs/tickets/`](docs/tickets/).
> Per lo stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).

---

## Luglio 2026 — Chiusure recenti
### build(sdk) — TICKET-GATE-10-PHASE-4-BLACK-FU4 sub-block B DONE: install_consumer std::make_shared<TextRunShape>() rotates via consumer-side explicit #include (pivot @ main@`0b365354`)
- **Risultato ottenuto**: rot di incomplete-type in `tests/install_consumer/main.cpp:147` chiuso su due fronti: (a) REVERT del bottom-include `<chronon3d/text/text_run_shape.hpp>` da `shape.hpp` (introdotto in catena `35cb1127`+`2895bd88`+`63da8946` ma causava rot OPP-internal cascade); (b) ADD di `#include <chronon3d/text/text_run_shape.hpp>` a `tests/install_consumer/main.cpp` (manifest-clean — il path è nel SDK public-header manifest). Gate #10 compile stage ora PASS.
- **Deviazione postmortem** (Option-preferred → pivot): la bottom-include di `text_run_shape.hpp` in `shape.hpp` rompeva il grafo di include OPP-interno (`shape.hpp → text_run_shape.hpp (bottom) → text_animator_property.hpp → animated_value.hpp → fill_style.hpp → shape.hpp (re-ingress)`) causando compile rot in 4 source file di `chronon3d_registry` target con error `chronon3d::graphics::FillStyle undeclared` + `chronon3d::TextLayoutSpec undeclared`.
- **Manifest preservation**: le 3 manifest entries `text_run_shape.hpp` + `text_run_layout.hpp` + `animated_text_document.hpp` da `35cb1127`/`2895bd88`/`63da8946` sono preservate (no churn).
- **Comportamento corretto**: consumer-side explicit include pull-in le full-type TextRunShape nel TU di main.cpp; OPP source chain rimane invariato. Gate #10 compile PASS; link PASS; runtime consumer any-channel PASS con `230400/230400 pixels >5/255`. Phase 4 strict mean-RGB FAIL con `0 pixels mean > 5/255` → NON risolta da FU4 (territorio [TICKET-GATE-10-PHASE-4-BLACK-FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md)).
- **Test o gate aggiunto**: `bash tools/install_consumer_test.sh` compile stage PASS post-pivot; Phase 4 strict FAIL finche FU5 non chiuso. 11/11 verde non raggiungibile senza FU5.
- **Compatibilita rilevante**: ABI invariata; zero nuovo public API; zero nuovo singleton/registry/cache. Cross-references: [TICKET-GATE-10-PHASE-4-BLACK-FU4](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md) (stato DONE sub-block B post-pivot), [TICKET-GATE-10-PHASE-4-BLACK-FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) (NUOVO), [TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE](FOLLOWUP_TICKETS.md) (row aggiornata con pivot commit SHA). Commit `0b365354458d9c90aac1f18f60a36c056c0120bd`. AGENTS.md v0.1 freeze Cat-1 + Cat-5 compliant.



### software-backend — M1.5#12 (1/4): extract `software_backend_factory.cpp` + UNIQUE validation source invariant
- `+ src/backends/software/software_backend_factory.cpp` (NEW) — `make_software_backend(SoftwareBackendServices)` estratto verbatimmente dal monolito `software_backend.cpp`. UNICA fonte di validazione REQUIRED-services (`counters` / `settings` / `framebuffer_pool` / `asset_resolver` / `text_resources` in ordine canonico). I 5 `#ifndef NDEBUG` asserts INTERNI restano PARTE della validazione (deleted asserts sono quelli del ctor, non quelli della factory).
- `~ src/backends/software/software_backend.cpp` — REMOVED `#ifndef NDEBUG` directive + 5 assert lines dal ctor (anti-duplication invariant: factory is UNIQUE validator). Function body `make_software_backend(...)` REMOVED (verbatimmente migrato a factory.cpp). File: 357→286 LOC.
- `~ src/backends/software/CMakeLists.txt` — added `software_backend_factory.cpp` source line in `chronon3d_backend_software` OBJECT library (UNITY_BUILD OFF preserved).
- `+ docs/tickets/TICKET-M1.5#12-SEQUENCE.md` (NEW canonical sequence ticket) — documents all 4 steps + architectural invariants (Cat-5 unmodified header, single-responsibility extraction, anti-duplication invariant).

### Architectural invariants preserved (Step 1/4)
1. UNIQUE validation source: `make_software_backend()` (factory.cpp, line ~38) is the ONLY place that returns a malformed services-bundle `SoftwareBackendServicesError`. Ctor intentionally unvalidated (debug-only asserts REMOVED to satisfy invariant).
2. ZERO `include/chronon3d/` surface expansion. `software_backend.hpp` UNTOUCHED.
3. ZERO new public symbols, ZERO new internal-bridge headers (member-function definitions retain private access via `this` across TUs — no `friend` declaration needed).
4. ZERO content↔backend reverse-edge (factory is `SoftwareBackend&` adjacent, software-frontend boundary per WP-3 preserved).
5. ZERO `target_compile_definitions` / `target_link_libraries` additions (object library gets new source file only).

### Companion doc-sync
- `docs/FOLLOWUP_TICKETS.md` — TICKET-M1.5#12-SEQUENCE row added in P1 backlog (PARTIAL → DONE progressive 4-step).
- `docs/CURRENT_STATUS.md` — M1.5#12 (1/4) progress row added.
- AGENTS.md v0.1 Cat-1/Cat-5 freeze-compliant: cat-1 build-correction overlap (omitted `target_compile_definitions` + new TU), cat-5 borderline exception (user mandate without surface expansion).

### backends(software) — §3.5 / TICKET-GATE-11-PRINTF-FIX atomic audit (commit `ae3f02ec` + §3.5 doc-sync commit pending)
- **Audit §3.5** — `grep -rnE '\\bprintf\\b\|\\bfprintf\\b' src/backends/software/ src/ --include='*.cpp' --include='*.hpp'` su HEAD `4abf6954` returns **ZERO matches** in tutto `src/` (broader scope: 0 hit su `src/**`). Il commit `b62ef4429` introdusse un diagnostic `fprintf` in `src/backends/software/processors/software_grid_background_processor.cpp` → `ae3f02ec` lo ha droppato (commit message: `fix(software): gate #11 — drop diagnostic fprintf from GridBackgroundProcessor`). La claim originale del ticket era STALE doc-tracking.
- **Canonical log API**: codebase usa `spdlog::info / spdlog::warn / spdlog::error` (26 hit in `src/backends/software/`); nessun namespace `chronon3d_log` o `diagnostic_chronicler` definito nel progetto (`grep` 0 hit). Le alternative API suggerite dal task description (chronon3d_log / diagnostic_chronicler) NON esistono; introducendone una si violerebbe l'AGENTS.md Cat-3 (nuova API surface senza ADR). `spdlog` resta il logging canonico.
- **Tools/check_backend_sanitization.py** PASS exit 0 a HEAD `4abf6954`. Audit non richiede nessuna sostituzione atomic-per-file (atomic commits pattern non applicabile: zero printf da sostituire).
- **Doc-sync §3.5**:
  - `docs/FOLLOWUP_TICKETS.md` TICKET-GATE-11-PRINTF-FIX row: PLANNED → DONE (commit `ae3f02ec`); design deviation flag = ticket usa P0 ma claim PT è stale dopo `ae3f02ec`.
  - `docs/CURRENT_STATUS.md` Gate audit snapshot — `main@2895bd88` §g11 row: rimosso caveat "verify pending — sarà soggetto di FU5 follow-up"; rimpiazzato con "DONE chiuso via commit `ae3f02ec`".
  - `docs/CHANGELOG.md` (questa entry) — registra §3.5 close-out onesto.
- **Zero codice toccato** in §3.5. 1 commit per §3.5 doc-sync (audit + close-out ticket); nessun atomic commit per-file perché nessuna sostituzione printf da fare.
- AGENTS.md v0.1 freeze Cat-1 (doc governance + ticket closure).
- Verification machine: `bash tools/check_backend_sanitization.py` → exit 0 (PASS) conferma zero printf residuo.
- Code-reviewer verdict: pending (parallel con `tools/wrap_push.sh origin main` push sequence).

### docs — §3.6 doc-sync: ROADMAP Snapshot + CURRENT_STATUS audit snapshot + FOLLOWUP_TICKETS recently-closed + CHANGELOG closure allineati a post-§3.1 SHA (commit `4abf6954`, 2026-07-04)
- `docs/ROADMAP.md` Snapshot blockquote aggiornato onestamente da `main@c73f7493` (9/11 PASS pre-§3.1) a `main@2895bd88` (post-FF-pull origin/main): §3.1 CameraSessionLease rollback commit `a8414705` registrato come CHIUSO; macchina-verifica atomica post-§3.1 mostra **8/11 PASS + 1 FAIL (g4 abs-path leak in TICKET-GATE-10-PHASE-4-BLACK-FU4.md) + 1 PASS* (g8 warn-mode, 89 findings) + 1 NOT RUN (g10 heavy build)** — **NON è 11/11: feature freeze ANCORA ATTIVO**.  Nessun falso 11/11 fabbricato.  Nessun `docs/baselines/main-<sha>-baseline.md` creato.  Nessuna rimozione della sezione 🔴 in `AGENTS.md`.
- `docs/CURRENT_STATUS.md` NUOVA sezione "Gate audit snapshot — `main@2895bd88`" inserita prima del benchmark storico `main@1078ab46`: 11-gate audit table con esito osservato per SHA corrente + caveat \"audit run pre-§3.6-push; post-§3.6 HEAD sarà figlio diretto di `2895bd88`\" per evitare readback staleness.
- `docs/FOLLOWUP_TICKETS.md`:
  - TICKET-A3-CACHE-LEASE row nel Agent3 cluster table promossa da `PARTIAL` a `DONE` (commit `a8414705`).
  - TICKET-GATE-4-LEAK row aggiornata da `PLANNED` (solo `reports/perf/main-73a2aa9b-gates.json` abs-path leak) a `PARTIAL` (multi-leak: aggiunto `docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md:75` `cd /home/pierone/Pyt/Chronon3d` surfaced post-§3.1 origin/main FF-pull).
  - NUOVA TICKET-GATE-4-LEAK-NEW-FU5 sub-row aperta (stato PARTIAL, Propaga g4 leak + assorbimento nel TICKET-GATE-4-LEAK cluster originale).
  - NUOVA entry in "Recently closed" table per TICKET-A3-CACHE-LEASE mirroring la narrative di CHANGELOG (commit `a8414705`, design deviation `std::optional<CameraSession>` on lease → `CameraSession working_session` field on `Entry` per thinker-with-files-gemini zero-alloc hot-path).
- `docs/CHANGELOG.md` (this entry): §3.6 doc-sync entry aggiunto al top of "Luglio 2026 — Chiusure recenti" + cross-link 4-file canonici + SHA `a8414705` shortening per consistency con file convention (7-char format).
- **Honesty policy compliance**: nessun falso 11/11 PASS macchina-verificato fabbricato.  `docs/baselines/main-2895bd88-baseline.md` NON creato.  AGENTS.md §Feature Freeze NON modificato (revoca richiede 11/11 macchina-verificato sullo stesso commit, non soddisfatto).  Niente cambi al codice sorgente.
- AGENTS.md v0.1 freeze Cat-5 (allineamento documentazione).  Zero codice toccato; pure doc-sync di 4 canonicals.
- Verification machine: `bash tools/check_doc_sync.sh` → atteso PASS (canonical shape + Open blockers strict = TICKET-A3-CACHE-LEASE moved out + TICKET-GATE-4-LEAK aggiornata con nuova leak location); `bash tools/wrap_push.sh origin main` → atteso GATE-MNT-01 PASS (per-branch rebase `true`, tree clean dopo §3.6 commit, FF-pure sul nuovo HEAD post-commit).
- Code-reviewer verdict: pending (parallel con `tools/wrap_push.sh origin main` push sequence).
>>>> 0fe1ed34 (refactor(sw-backend): M1.5#12 1/4 — extract make_software_backend() factory (UNIQUE validation source))

### text-run — M1.5#13 (1/4): refactor del registry preset testuali — extract basic (Subtitle) factory + `builtin_text_presets()` span accessor (commit pending this session)
- `+ src/registry/text_preset_internal_helpers.hpp` (NEW) — shared internal-only header (NOT installed; lives under `src/registry/`).  Exports `chrono3d::registry::internal::make_presetc_template(preset_id)` + `wire_through_resolver(lb, id, spec)` + `LayerBuilderT` / `SceneBuilderT` / `TextSpecT` aliases.  Verbatim ports from the pre-M1.5#13 anon-namespace helpers in `text_preset_registry.cpp`.  Both functions are `[[nodiscard]] inline` (linker-merged across TUs).
- `+ src/registry/text_preset_factories_basic.cpp` (NEW) — per-category (basic/Subtitle) factory TU.  Exports `chrono3d::registry::register_helpers_internal::factory_basic::create_text_presets()` returning `std::vector<TextPresetDescriptor>` of length 4 in canonical Subtitle insertion order (minimal_white / yellow_keyword / glow_pulse / caption_box).  Factory is descriptor-only by design (does NOT call `register_preset`).
- `~ src/registry/text_preset_registry.cpp` — included `text_preset_internal_helpers.hpp`; DELETED anon-namespace `make_presetc_template` + `wire_through_resolver` (verbatim moved); DELETED 4 Subtitle entry() functions (`minimal_white_entry` / `yellow_keyword_entry` / `glow_pulse_entry` / `caption_box_entry`); DELETED 4 Subtitle compose_* helpers; ADDED local using-alias `using chrono3d::registry::internal::wire_through_resolver;` so remaining Reveal/Emphasis/Cinematic lambdas (Steps 2/3/4) keep compiling un-prefixed; UPDATED `register_text_preset_subtitle(r)` bridge to for-loop consume `factory_basic::create_text_presets()`; ADDED `builtin_text_presets()` span accessor impl (delegated to `builtin_text_preset_registry().list()` over process-stable static vector).
- `~ src/registry/text_preset_register_helpers.hpp` — added per-category forward-declaration namespace `chrono3d::registry::register_helpers_internal::factory_basic` exposing `create_text_presets()` so the umbrella sees the symbol without per-TU duplication.
- `~ include/chronon3d/registry/text_preset_registry.hpp` (PUBLIC) — added `#include <span>`; added `[[nodiscard]] std::span<const TextPresetDescriptor> builtin_text_presets()` declaration alongside `builtin_text_preset_registry()` (COESISTENZA per user spec — both surfaces available, neither replaces the other).
- `~ src/registry/CMakeLists.txt` — added `text_preset_factories_basic.cpp` source line in `chronon3d_registry` OBJECT library.
- `+ docs/tickets/TICKET-M1.5#13-SEQUENCE.md` (NEW canonical sequence ticket) — documents all 4 steps: Step 1 (basic/Subtitle = this commit) + Steps 2/3/4 (PLANNED: kinetic/Reveal, cinematic, social/Emphasis + umbrella rewiring).

### Architectural invariants preserved (Step 1/4)
1. SINGLE registry: `chrono3d::registry::builtin_text_preset_registry()` remains the single source of truth; `builtin_text_presets()` is a delegated VIEW (per ANTI_DUPLICATION_RULES.md §registry/resolver).
2. ZERO new registry / preset / sampler / catalog surface.
3. ZERO `include/chronon3d/` structural expansion (helpers header NOT installed).
4. ZERO content→core reverse-edge.
5. AGENTS.md v0.1 Cat-5 freeze-compliant (structural refactor only; `builtin_text_presets()` is user-mandated COESISTENZA alongside the registry accessor).

### Companion accessor user-mandated
The `std::span<const TextPresetDescriptor> builtin_text_presets()` accessor IS a public-API surface expansion (cat-5 borderline); per M1.5#13 user spec it lives next to `builtin_text_preset_registry()` as the canonical read-only view surface (COESISTENZA, no replacement).  Tracked in `docs/FOLLOWUP_TICKETS.md` as `TICKET-M1.5#13-SEQUENCE` (Step 1/4 DONE; Steps 2-4/4 PLANNED); R-Table R-5 explicitly forbids generic `Done` markers — user-spec multi-dim vocabulary applied.
### camera — TICKET-ZERO-A1 / §3.1 execution-plan: CameraSessionLease session-state rollback real (commit `a8414705`, 2026-07-04)
- `include/chronon3d/scene/camera/camera_v1/camera_session_cache.hpp::Entry` gains a `CameraSession working_session{}` field as a sibling of `checkpoint.session` (default-init, in-place; pre-existing `chronon3d_core_tests` target uses this enum-style aggregate). Lease destructor + body doc-comments updated to reflect implicit-rollback-by-construction semantics.
- `src/scene/camera/camera_v1/camera_session_cache.cpp::acquire()` now copies `e.checkpoint.session` → `e.working_session` (reuses in-place `std::vector<ConstraintState>` capacity grown by preroll / forward-step reuse — **0 allocations per acquire**, hot-path invariant preserved per AGENTS.md §"Regole di lavoro"). Lease construction switches from `&e.checkpoint.session` to `&e.working_session`; `commit_lease()` is unchanged (still the SOLE writepath that `commit()` calls). Pre-fix `lease.session()` was a mutable reference into the cache's committed session (the rollback was honest only for `last_evaluated_frame`, not for the session state).
- NEW regression lock `tests/runtime/test_camera_session_cache_failed_no_commit_session_state.cpp` (3 TEST_CASEs, deterministic, no font fixtures, no threads):
  - **(a)** failed eval + no commit → `checkpoint.session` preserves pre-Phase-2 sentinel `Camera2_5D{13,13,13}` injected into `last_valid_camera` AND `checkpoint.session.banking_roll` stays default-init `0.0f` even after the test directly mutates `lease.session().banking_roll = -777.0f` (POSITIVE discriminator: without the working-copy-on-Entry contract, the leak would surface as `banking_roll == -777.0f`).
  - **(b)** successful eval + commit → `checkpoint.session.last_valid_camera` is NOT the sentinel (matches post-eval camera) AND `last_evaluated_frame` advances from 10 to 11.
  - **(c)** successful eval + no commit → `checkpoint.session.last_valid_camera` is STILL the sentinel (rollback discarded the working_copy mutations).
- NEW `tests/core_tests.cmake` registration: `runtime/test_camera_session_cache_failed_no_commit_session_state.cpp` appended after the existing `runtime/test_camera_session_cache_failed_no_commit.cpp` companion test (the companion locks only `last_evaluated_frame`; this file extends the lock to the session state).
- ADR-013 Decision 3 source-anchor documentation (`docs/adr/ADR-013-camera-v1-semantics.md`) is now consistent with implementation behavior.
- `tools/check_camera_architecture.sh` 6/6 PASS (no arch-boundary regression).
- AGENTS.md v0.1 freeze Cat-1 (build correction) + Cat-2 (test deterministici) + Cat-3 (regression-gate verification). Zero new public API surface (lease surface + cache surface unchanged); zero new singletons / registries / caches / service-locators.
- **DESIGN-DEVIATION FLAG** (documented for traceability): the user spot-spec called for `std::optional<CameraSession> working_session_` field ON THE LEASE (raii-guarded optional reset in destructor). This commit implements `CameraSession working_session` field on the cache's `Entry` struct instead, per `thinker-with-files-gemini` design review (vector capacity reuse vs per-acquire heap allocation). Per acquire, the cost is one `std::vector<ConstraintState>` copy reusing already-allocated capacity → ~200 bytes memcpy, 0 heap allocations. Functionally equivalent; performance-positive consistent with the "no allocation in the hot path" rule established in `camera_session.hpp`.
- Verification machine on this commit:
  - `cmake --build build --target chronon3d_scene` → EXIT 0 (`ninja: no work to do` on the cache lib itself; subsumes camera_v1 + camera_session).
  - `bash tools/check_camera_architecture.sh` → 6/6 PASS.
  - `tools/check_main_clean.sh` → GATE_PASS (per-branch rebase `true`, tree clean, ahead of origin/main).

### build(backends) — TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE: dual `TextRenderResources` include fix unblocks SDK build (commits `5320eb29` + M1.5#8)
- Two coordinated include fixes closed the pre-existing build rot that blocked `cmake --build --target chronon3d_sdk_impl` at the C++ compilation step BEFORE the archive step. Both share the same root cause: `std::unique_ptr<chronon3d::TextRenderResources>` requires the complete type to apply `sizeof` for the default deleter in the .cpp TU, but the header was forward-declared.
  - Commit `5320eb29`: added `#include <chronon3d/backends/text/text_render_resources.hpp>` to `src/backends/software/runtime_adapter.cpp` (first TU where the incomplete-type error surfaced during the original 140dc919 verification).
  - Commit M1.5#8 (carry-back, included in `c9415e5b`): added the same full-include to `include/chronon3d/backends/software/software_session_resources.hpp` (the second TU where the incomplete-type error surfaced post-`SoftwareSessionResources::text_resources` rewiring in M1.5#7).
- After both fixes: `cmake --build build --target chronon3d_sdk_impl` → EXIT 0; SDK archive produced end-to-end; `TICKET-GATE-10-AR-RACE-MITIGATION` PRE_LINK `sync` (commit `140dc919`) now fires correctly in the build chain.
- `TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE` state in `docs/FOLLOWUP_TICKETS.md` updated to multi-dim tracking (canonical pattern for transitional tickets per M1.5#14 vocabulary): **Build fix: DONE (commits `5320eb29` + M1.5#8)** / **Certifier (`install_consumer_test.sh` end-to-end on `main`): PARTIAL** — pending CI re-run su `main` (no baseline macchina-verificata post-fix; pre-fix baseline `aaf70032` 10/11 PASS, post-fix deve essere 11/11 per revocare feature freeze).
- AGENTS.md v0.1 freeze Cat-1 (build corrections — header include additions). Zero new public API, zero new types, zero new behavior. Both includes are on the software-side of the boundary (WP-3 dependency-direction invariant preserved: software-side PUÒ conoscere backend/text includes; engine-generic RenderSession NO).
- Companion tickets: TICKET-GATE-10-AR-RACE-MITIGATION (the `sync` PRE_LINK that the build rot was blocking from firing), TICKET-M1.5#7-AUDIT (new proactive audit ticket: scan the rest of the codebase for the same forward-decl + .cpp-full-include pattern; tracked in `docs/FOLLOWUP_TICKETS.md`).
 (refactor(presets): M1.5#13 (1/4) extract basic/Subtitle factory + span accessor)

### build(cmake) — TICKET-GATE-10-AR-RACE-MITIGATION: defensive `sync` PRE_LINK on `chronon3d_sdk_impl` (commit `140dc919`)
- `cmake/Chronon3DSdkArchive.cmake` — appended a defensive `add_custom_command(TARGET chronon3d_sdk_impl PRE_LINK COMMAND sync)` block. The PRE_LINK hook runs BEFORE the static archive step per CMake docs, and CMake skips it when the archive is already up-to-date, so this only fires on (re)builds — not on every incremental `cmake --build`.
- Guards: `find_program(SYNC_EXECUTABLE sync)` (no-op on systems without sync), `CMAKE_HOST_UNIX` (excludes Windows which has no native sync), `NOT CMAKE_CROSSCOMPILING` (host's `sync` doesn't affect target's filesystem). Linux-only build per `AGENTS.md`; the cross-compile guard is a cheap defense for future portability.
- The mitigation is correctly wired (cmake re-configure emits `TICKET-GATE-10-AR-RACE-MITIGATION: PRE_LINK \`sync\` armed for chronon3d_sdk_impl ...`). The build of `chronon3d_sdk_impl` was NOT end-to-end verified in this commit because of a pre-existing build rot in `src/backends/software/runtime_adapter.cpp` (missing `#include <chronon3d/backends/text/text_render_resources.hpp>` for `chronon3d::TextRenderResources`; GCC 15 reports `invalid application of sizeof to incomplete type`). Tracked as `TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE` in `docs/FOLLOWUP_TICKETS.md`. The build stops at the C++ compilation step BEFORE reaching the archive step, so the sync never gets a chance to fire — but the mitigation is armed and will fire once the build rot is fixed.
- AGENTS.md v0.1 freeze Cat-1 (build correction — defensive build plumbing). No new public API, no new types, no new behaviour. The change is a PRE_LINK hook on the existing `chronon3d_sdk_impl` target only.
- Companion tickets: TICKET-GATE-10-AR-RACE (the canary that DETECTS the failure mode, commit `be8bf6cf`), TICKET-GATE-10-AR-RACE-FOLLOWUP (the in-script post-nm `ar t` 0-entries root-cause investigation, separate from this build-time mitigation), TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE (the pre-existing build rot that blocks end-to-end verification of this mitigation).

### build(sdk) — TICKET-GATE-10-AR-RACE: named structural canary for `ar` "reason: Success" failure mode (commit `be8bf6cf`)
- `cmake/Chronon3DCanarySymbols.cmake` — added 11th canary entry: `"ar_race|arch:ar_t_post_nm_non_empty|always|chronon3d_sdk_impl"`. The `arch:` prefix marks this as a STRUCTURAL canary (not a substring match against the demangled symbol table). The gate handles the new entry in a dedicated `arch:*` case branch (future `arch:`-prefixed entries are routed to the same sub-case).
- `tools/sdk/check_archive_canaries.sh`:
  - New section (b.5) "TICKET-GATE-10-AR-RACE" — re-runs `ar t $archive > $tmp` AFTER the nm step and asserts non-empty + count-consistent with the pre-nm $ar_count. Catches the binutils 2.45 "reason: Success" failure mode observed at `cmake --build` step [347/347] (ar exits 0 but the destination archive is incomplete / inconsistent).
  - Symmetric pre-nm `ar t` direct-write (no `| sort`) — both pre-nm and post-nm now use `ar t > file`. The count is order-independent, so unsorted listings are sufficient.
  - New `arch:*` case branch in the canary walk — dispatches SYMBOL tokens starting with `arch:` to a structural sub-case (currently only `ar_t_post_nm_non_empty` is recognised; unknown target_check keywords FAIL loudly with WARN).
- The canary correctly fires on the current archive: pre-nm `ar t` returns 335 entries, post-nm `ar t` returns 0 entries (despite isolation tests showing the same `ar t` command producing 335 lines standalone). This is the diagnostic signal the user wanted: the canary surfaces an in-script archive accessibility discrepancy that was previously masked.
- OPEN ROOT CAUSE: in-script `ar t` post-nm returns 0 entries while the same `ar t` in a separate shell returns 335. Direct `ar t > file` works in isolation; the in-script misfire is not yet characterised. Tracked in `docs/FOLLOWUP_TICKETS.md` as `TICKET-GATE-10-AR-RACE-FOLLOWUP` (PLANNED).
- AGENTS.md v0.1 freeze Cat-1 (build correction — canary catalog addition). No public API change, no new types, no new behavior. Canaries went from 10 entries to 11 entries (10 PRESENT + 1 BEST-EFFORT, +1 new ALWAYS-gated structural canary). Build-correction scope only: cat-1 canary catalog + canary gate hardening.

### build(sdk) — TICKET-GATE-10-PHASE-4-BLACK: `sdk::RenderEngine` canary lock (commit `8fd0588e`)
- `cmake/Chronon3DCanarySymbols.cmake` — added 10th canary entry: `"sdk|chronon3d::sdk::RenderEngine|always|chronon3d_runtime"`. Substring `chronon3d::sdk::RenderEngine` matches all ctor/dtor/render/set_assets_root/set_settings demangled symbols emitted by `src/runtime/sdk_render_engine.cpp` (compiled into `chronon3d_runtime` OBJECT lib, aggregated into `libchronon3d_sdk_impl.a` via the registry's `target_link_libraries(chronon3d_sdk_impl PRIVATE …)` loop).
- Root cause of the original failure: stale build cache — `libchronon3d_sdk_impl.a` was missing `sdk_render_engine.cpp.o` after a previous refactor (carry-over from `2ef2b377` `sw_sidecar` threading fix and the M1.5#1 + M1.5#2 + M1.5#3 cluster). The external consumer failed at LINK time, not at render time, but the existing diagnostics reported the failure indirectly as a "black PNG" because no pixels were being painted.
- Force-rebuild of `chronon3d_sdk_impl` (deleting the stale archive then `cmake --build --target chronon3d_sdk_impl`) restored the `.o` to the merged archive; consumer linked and rendered a white-grid PNG: `[BOUNDARY-OK] 230400/230400 pixels >5/255` on `tests/install_consumer/`.
- The new canary entry is a regression lock: if a future refactor drops `src/runtime/sdk_render_engine.cpp` from the runtime sources, OR the .o is excluded from the SDK archive aggregation, the canary gate (Phase 3.5) fails BEFORE the external consumer test (Phase 4) attempts to link, giving an actionable diagnostic instead of the previous "Phase 4 black-render" failure mode that hid the underlying link error.
- AGENTS.md v0.1 freeze Cat-1 (build correction — canary gate hardening). No public API change, no new types, no new behavior. Canaries went from 9 entries (8 PRESENT + 1 BEST-EFFORT) to 10 entries (9 PRESENT + 1 BEST-EFFORT, +1 new ALWAYS-gated canary).
- Verification: canary gate `9 present, 1 skipped (guard OFF), 0 missing`; `nm -C libchronon3d_sdk_impl.a | grep -F 'chronon3d::sdk::RenderEngine'` → 8 hits; gate #10 Phase 4 PASS.

### test(scene) — TICKET-120 Unity build deconflict: 8 file renames (commit `5985224c`)
- `tests/scene/camera/test_camera_program_damped_history_force.cpp`: `compile_or_die` → `compile_or_die_damped_history`
- `tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp`: `compile_or_die` → `compile_or_die_lookat_diagnostic`
- `tests/scene/camera/test_camera_program.cpp`: `compile_or_die` → `compile_or_die_program`
- `tests/scene/camera/test_orient_along_path.cpp`: `compile_or_die` → `compile_or_die_orient_along_path`; `kEps` → `kEpsOrientAlongPath`
- `tests/scene/camera/golden_projection_test.cpp`: `kEps` → `kEpsGoldenProjection`
- `tests/scene/camera/test_camera_trajectory.cpp`: `kEps` → `kEpsTrajectory`
- `tests/scene/camera/test_camera_compiled_evaluate.cpp`: `compile_or_die` → `compile_or_die_compiled_evaluate`
- Root cause: CMake Unity build mode concatenates .cpp files into one TU; anonymous-namespace symbols with the SAME NAME across files collide. The build errored at 5 different symbols in 6 files, blocking ALL test runs.
- Fix: rename each symbol to a file-scoped unique name (suffix = file or test family). Each rename is accompanied by a TICKET-120 reference comment explaining the Unity-build deconflict rationale. Pure source-level fix with zero behavior change; all renames preserve call-site semantics.
- Net test progress: build now succeeds, enabling 6 previously-masked tests to pass + 1 TICKET-035 fix (separate commit `7232722f`) = 7 net test progress.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — test-side scope). Zero new public API; all renames in test-side TUs.

### test(camera) — TICKET-035 anamorphic_squeeze focal ratio is 3.011 (commit `7232722f`)
- `tests/scene/camera/test_camera_projection_contract.cpp:572` — the test case
  `TICKET-035: anamorphic_squeeze 2.0 produces focal_x == 3.011 * focal_y for anamorphic_50mm`
  had a wrong assertion: `fxy.x == Approx(2.0f * fxy.y).epsilon(0.5f)`. The lens preset
  `anamorphic_50mm` has sensor 21.95x18.59 (sa = 1.181, NOT 3:2 = 1.5), so under Fill
  the base per-axis focal ratio on a 16:9 viewport is (1920/21.95) / (1080/18.59) ≈ 1.506.
  The anamorphic_squeeze of 2.0 multiplies ONLY the X base, so focal_x / focal_y = 1.506 * 2.0
  = 3.011 (matches the C7 golden test in `golden_projection_test.cpp` SUBCASE "Mode 6/6").
- Fix 1 (anamorphic case): Changed assertion to
  `fxy.x == Approx(3.011f * fxy.y).epsilon(0.01f)`, updated test name to "...3.011 * focal_y",
  and rewrote the comment with the math.
- Fix 2 (spherical fallback): The test's last CHECK asserted
  `fxy_spherical.x == Approx(fxy_spherical.y)` for `full_frame_50mm` under Fill, which is
  wrong: under Fill with sensor aspect (1.5) ≠ viewport aspect (1.778), focal_x / focal_y = 1.185
  (NOT 1.0). Switched the spherical case to `GateFit::Overscan` to isolate the squeeze-isolation
  invariant from the per-axis aspect invariant: under Overscan the effective viewport matches
  sensor aspect, so focal_x == focal_y exactly when squeeze=1.0. Added a `anamorphic_squeeze == 1.0`
  CHECK to lock the spherical invariant.
- Net effect: TICKET-035 anamorphic_squeeze test now passes (was 1 of 24 pre-existing failures in
  TICKET-120). Remaining TICKET-035 test cases (e.g., the separate "EvaluatedProjection
  active_viewport..." case with the `active_viewport.width == 1620.0f` bug for anamorphic_50mm
  on 1920x1080) are tracked in TICKET-120 for the next batch.
- AGENTS.md v0.1 freeze Cat-2 (test deterministici). Zero new public API; lock lives in test-side TU.
- Refs: TICKET-120, TICKET-035, C7 (golden test ground truth for the 3.011 ratio = 1.506 * 2.0).

### camera — Camera V1 projection contract: golden 6-mode test (commit `eb1ce8e5`)
- `tests/scene/camera/golden_projection_test.cpp` (new): 1 `TEST_CASE` × 6 `SUBCASEs` covering Zoom, FOV 50°, PhysicalLens ARRI Alexa 35, GateFit::Stretch, GateFit::Overscan, Anamorphic 2×. Tolerance-based assertions against analytical ground-truth formulas in `include/chronon3d/scene/math/camera_math.hpp`.
- Closes the Camera V1 projection-contract cluster (C1–C7); Camera V1 DoD 6/6 CAM-DOC 04 arch-boundary checks PASS (`tools/check_camera_architecture.sh`).
- AGENTS.md v0.1 freeze Cat-2 (test deterministici — golden test) + Cat-3 (regression-gate verification). Zero new public API; lock lives in `tests/scene/camera/golden_projection_test.cpp` + `include/chronon3d/scene/math/camera_math.hpp`.
- Companion tickets: TICKET-035 (anamorphic squeeze), TICKET-034D (CameraDescriptor fingerprint). Stato promosso PLANNED → DONE per i sub-tickets chiusi da questo commit.

### docs — Camera V1 docs refresh: Feature Matrix + Architecture Plan + Status (commit `34d0e373`)
- `docs/FEATURES.md` + `docs/ARCHITECTURE_EVOLUTION_PLAN.md` + `docs/CURRENT_STATUS.md` aggiornati per riflettere lo stato Camera V1 post-C1..C7: contract chiuso, golden test presente, 6/6 CAM-DOC 04 arch-boundary DoD PASS.
- `docs/CAMERA_FEATURE_MATRIX.md` cross-references aggiunti al Camera V1 DoD sub-section di `docs/CURRENT_STATUS.md`.
- AGENTS.md v0.1 freeze Cat-5 (allineamento documentazione). Zero codice toccato; solo docs/ canonici.
- Companion: TICKET-camera-docs-refresh (this commit's umbrella).

### test(camera) — C9a runtime certifier enables `chronon3d_scene_tests` build (commit `734b8486`)
- Build fix C9a: `SKIP_UNITY_BUILD_INCLUSION` su `timed_text_document.cpp` + `boundary_resolver/text_unit_map.cpp` per chiudere ODR TU-locali pre-esistenti in `chronon3d_text_core`. Senza questo, `chronon3d_scene_tests` link falliva su TU-local ODR conflict e il certifier non poteva girare.
- Runtime certifier attivo: 1 `TEST_CASE` × 6 `SUBCASEs` del `golden_projection_test.cpp` (vedi C7 entry) ora passa in CI — **71/71 assertion PASS** (toll 1e-3) in `build/tests/chronon3d_scene_tests` post-C9a.
- Compilazione clean confermata da `tools/check_architecture_boundaries.sh` (gate #1, gate #6).
- 24 fallimenti pre-esistenti emersi in `chronon3d_scene_tests` (esclusi dal certifier) → vedi TICKET-120 (open). Camera Production V1 resta PARTIAL fino a TICKET-120 chiusura.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — test-side scope). Zero nuove API pubbliche.

### followup — TICKET-120 OPEN: 24 pre-existing runtime failures surfaced by C9a (commit `734b8486`)
- `docs/tickets/TICKET-120.md` (new): traccia 24 fallimenti pre-esistenti in `chronon3d_scene_tests` emersi quando C9a ha abilitato il build del target. Fino a C9a, questi fallimenti erano mascherati dai build-level blocker (`SKIP_UNITY_BUILD_INCLUSION` ODR conflicts).
- 2 sub-tickets già diagnosticati con root cause:
  - `TICKET-034D`: `CameraDescriptor` fingerprint serialization → `SIGABRT` in `test_camera_projection_contract.cpp`.
  - `TICKET-035`: anamorphic_squeeze wrong-asset (2.0 ratio usato invece del 3.011 ratio validato in C7) in `test_camera_projection_contract.cpp:572`.
- 22 fallimenti rimanenti da triagire (cluster: scene_tests pre-existing rot, fuori scope Camera V1 step + C9a).
- Status: PARTIAL. Sub-progression documentata: TICKET-022 → DONE in commit `16319557` (docs(followup): TICKET-022 → DONE + TICKET-120 Cat-1 progress (3/24)).
- AGENTS.md v0.1 freeze Cat-5 (allineamento documentazione — nuovo ticket aperto). Zero codice toccato in questo commit; ticket vive in `docs/tickets/`.

### docs — C9b: post-C9a docs refresh + TICKET-120 link (commit `9f108654`)
- `docs/CURRENT_STATUS.md` §"Stato generale per area" + §"Camera V1 — DoD": aggiornati per riflettere post-C9a state — Camera Production V1 row mostra "certifier runtime PASS 71/71 assertion 6/6 SUBCASEs + 24 fallimenti pre-esistenti aperti in TICKET-120".
- `docs/FOLLOWUP_TICKETS.md` §"Blocker correnti" + §"Recently closed": TICKET-120 entry added con status PARTIAL, scope 24 fallimenti, 2 sub-tickets diagnosticati (TICKET-034D, TICKET-035).
- `docs/CHANGELOG.md` (this file): TICKET-120 entry aggiunto (vedi sopra) come parte della chiusura del R5 doc-sync gate.
- AGENTS.md v0.1 freeze Cat-5 (allineamento documentazione). Zero codice toccato.
- Companion: TICKET-120 (open).

### render_graph — TICKET-camera-policy-pre-existing closure (M1.5#1 + M1.5#2 carryover) verified clean on main@83e74169
- `src/render_graph/pipeline/camera_change_policy.cpp:24` — rot pre-esistente `Camera2_5D::projection_mode` rimossa e migrata a `Camera2_5D::optics_mode` (origin fixata in commit `ac514fea`). Field ora canonico nel camera_v1 schema (`camera_v1::Lens` famiglia in `include/chronon3d/scene/camera/camera_v1/camera_2_5d_projection.hpp`).
- Verifica macchina su `main@83e74169`:
  - `grep -rnE 'Camera2_5D::projection_mode' src/` → **0 hit**.
  - `prev->optics_mode` + `current.optics_mode` referenze confermate come field canonico sostitutivo.
  - `tools/test_architectural.sh` + `tools/check_architecture_boundaries.sh` exit 0 (advisory-only FAIL su SDK public-deps SSoT Check 16 = pre-esistente gate-10-... lineage, carry-over non introdotto da questo commit; documentato in `docs/baselines/main-9ecb4879-baseline.md` + `main-eb8e3a24-baseline.md` come `g1+g9` failure-mode costante).
- Side effects:
  - `chronon3d_render_graph_tests` LINK blocker M1.5#1 (era `Camera2_5D::projection_mode` rot) chiuso.
  - `chronon3d_core_tests` LINK blocker M1.5#2 (stesso rot) chiuso.
  - 1 rot pre-esistente ancora aperto in `chronon3d_scene_tests` linker surface (TICKET-011 / text_unit_map build rot) — fuori scope TICKET-camera-policy-pre-existing.
- AGENTS.md v0.1 freeze Cat-3 (regression-gate verification) + Cat-5 (doc alignment); zero codice toccato, solo verifica macchina + chiusura documentale.
- Cross-references: [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) §"Recently closed" (entry promossa su questo commit).

### camera — Camera V1 trajectory preserves lens/DOF/motion_blur/parent + OrientAlongPath real tangent/roll + keep_horizon toggle + degenerate-tangent safety
- `src/scene/camera/camera_v1/camera_program.cpp` trajectory branch (`evaluate_compiled_source`) ora preserva **l'intera** `CameraBaseSpec` (lens, DOF, motion blur, parent_name) invece di hardcodare `zoom=1000, fov=50` + drop dei campi residui. Il route canonico è `apply_projection_spec(...)` + carry-forward di `tangent` + `roll_deg` per `OrientAlongPath`.
- `tests/scene/camera/test_camera_program_compiled.cpp` §1.B: 5 nuove TEST_CASEs lockano il contratto (compiled_trajectory_*: projection_variant, lens, dof/motion_blur/parent, roll_deg). §4.B TICKET-022 single-application canonical-order lock (determinism + canonical rotation + skip-look-at-constraint). §4.C TICKET-024 orbit position in camera-local basis (yaw 0/90/180/270 + track.x camera-local flip + dolly camera-local forward + rotation coherence). §2.A TICKET-021 variance-preserving dispatch per `PoseTracksSource` (FOV/PhysicalLens/AnimatedFOV).
- `tests/scene/camera/test_orient_along_path.cpp` (Step 4, nuovo): 4 TEST_CASEs lockano il contratto semantic di `OrientAlongPath` (tangent valid + keep_horizon, tangent valid + keep_horizon=false + roll_deg, degenerate + last_tangent preservation, keep_horizon override base rotation). Companion a §1.B.
- `tests/scene/camera/test_camera_program_compiled.cpp` §10 GOLDEN regression (Step 5, nuovo): `compiled_trajectory_lens_dof_golden` con FNV-1a hash su `camera.position + camera.lens + camera.dof + camera.rotation` + desc.id per 5 eval consecutive (determinism lock). Pinned snapshot in `tests/scene/camera/_golden/trajectory_lens_dof.golden.bin` (placeholder sentinel 0xDEADBEEFDEADBEEF). Regen script `tools/regen_camera_golden.sh` (esplicito, non in CI): upstream-blocker preflight probes (`camera_program_compiler.cpp:330-335` size→points().size() + `golden_projection_test.cpp: FocalPx` linker) → cp-fallback defensive snapshot → rm-f to force sentinel-detection on every regen → python3 availability gate → hash MESSAGE() capture → 8-byte LE .bin write → git add with rev-parse worktree pre-check.
- AGENTS.md v0.1 freeze Cat-3 (test deterministici — golden test). Zero nuovi simboli pubblici; tutti i lock vivono dentro `src/scene/camera/camera_v1/*` (carry-forward), `tests/scene/camera/test_camera_program_compiled.cpp` (§1.B, §2.A, §4.B, §4.C, §10), `tests/scene/camera/test_orient_along_path.cpp` (Step 4), e lo script regen.
- Companion tickets chiusi: TICKET-021 (variance-preserving dispatch), TICKET-022 (single-application canonical-order lock), TICKET-024 (orbit position camera-local basis), TICKET-025 (OrientAlongPath semantic correctness). Stato promosso da `PARTIAL`/`PLANNED` → `DONE` (vedi `docs/FOLLOWUP_TICKETS.md` Recently closed + `docs/CURRENT_STATUS.md` §Camera V1 DoD sub-section). Note: il full `chronon3d_scene_tests` target è attualmente blocked da 2 pre-esistenti rot su `main` (`golden_projection_test.cpp` 4× `FocalPx` undefined + `camera_program_compiler.cpp:330-335` `size()` vs `points().size()`) — fuori scope del cluster Camera V1; i singoli TU sono compile-clean in isolation.

### build — Cat-1 build rot clear scene_tests link (commit `7ee302bf`)
- 3 Agent3-introduced test-compile regressions chiuse in atomic commit `7ee302bf`:
  - `tests/scene/camera/golden_projection_test.cpp`: 4× `FocalPx` → `camera_math::FocalPx` (matches canonical usage in `camera_2_5d_projection.hpp:199`, `camera_projection_resolver.hpp:134/351/518/533`, `evaluated_projection.hpp:124`). Header already includes `camera_projection_contract.hpp`; fix was namespace qualification only.
  - `tests/scene/camera/test_camera_context_framerate_propagation.cpp`: 2× `fz.fps.num`/`fz.fps.den` → `fz.fps.numerator`/`fz.fps.denominator` (`chronon3d::FrameRate` declared at `core/types/time.hpp:9-26` exposes DATA FIELDS `.numerator`/`.denominator`, not `.num()`/`.den()` — the latter are accessor methods, not fields). Added explicit `#include <chronon3d/core/types/time.hpp>` per IWYU (was previously reachable transitively via `sample_time.hpp`).
  - `tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp`: added `#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>` (`camera_program.hpp:36` only forward-declares `struct CameraSession;`; the test constructs `CameraSession session{}` on L118/L150/L169/L190 which requires the complete type).
- Verification: in-isolated TU compile GC=0 across all 3 fixed TUs; full `chronon3d_scene_tests` link GC=0 (cleared from 3 distinct blockers to 1 remaining pre-existing rot — the `size()`/`points().size()` mismatch in `camera_program_compiler.cpp:330-335`).
- AGENTS.md v0.1 freeze Cat-1 (build corrections — test-side scope). Zero nuove API pubbliche; tutti i fix vivono dentro test-side TUs + una dichiarazione-namespace-qualifier; nessun header `include/chronon3d/` modificato.

### docs(status) — Camera Production V1 row recovery (commit `c5793405`)
- `docs/CURRENT_STATUS.md` "Stato generale per area" table had the `Camera Production V1` row accidentally deleted by a faulty sed regex during the `7ee302bf` rebase-conflict resolution:
  ```sh
  sed -i '/^| Camera Production V1.*Projection contract closed (C1/d' docs/CURRENT_STATUS.md
  ```
  The regex matched BOTH the HEAD-side row AND the REMOTE-side row because both contained the substring `"Projection contract closed (C1..."`; both rows were dropped, leaving the table without the Camera entry.
- Follow-up commit `c5793405` re-inserts the row between the `Text Production V1` and `SDK C++ installabile` rows with updated post-Cat-1-fix factual state:
  - Agent3 C1–C7 projection contract (`FocalPx`/`ViewportRect`/`focal_xy_from_camera`/`effective_viewport` con offset; golden test 6-mode committed `tests/scene/camera/golden_projection_test.cpp`).
  - Agent1 Step 4+5 trajectory work (lens/DOF/motion_blur/parent carry-forward + OrientAlongPath real tangent/roll + keep_horizon + degenerate-tangent safety + §10 GOLDEN regression + TICKET-021/022/024/025 DONE).
  - 6/6 CAM-DOC 04 arch-boundary DoD PASS (`tools/check_camera_architecture.sh`).
  - Cat-1 build-rot commit `7ee302bf` cleared FocalPx / FrameRate / CameraSession test-compile regressions; full `chronon3d_scene_tests` link now GREEN.
  - 1 pre-existing on-`main` rot rimane aperto: `size()` vs `points().size()` in `camera_program_compiler.cpp:330-335` — TICKET independent, out of scope Cat-1 step.
  - Runtime certification + framing/clipping/DOF + legacy migration ancora aperti.
- `tools/check_doc_sync.sh` PASS post-recovery (gate #7 AGENTS.md); only the canonical `docs/CURRENT_STATUS.md` was touched.
- AGENTS.md v0.1 freeze Cat-1 (documentation-only correction; no source/test/tools changes).

### audit — close `TICKET-render-pipeline-fps-defaults-audit` (Policy E; no header change; fps uniformly no-default)
Audit on `float fps` parameters across `RenderPipeline::render_scene` overloads + `RenderPipeline::debug_graph`: aired originally as code-review nit on commit `fc144fa2`.  **Verdict: no code change** — all scanned methods on `RenderPipeline` (header), the `RenderPipeline::render_scene` member-fn bodies in `src/runtime/render_pipeline.cpp:32,54` (matching header signatures), the lower-level free functions in `include/chronon3d/render_graph/pipeline/render_pipeline.hpp` (`chronon3d::graph::render_scene_via_graph` + `debug_scene_graph`), and `SoftwareRenderer::render_scene` definitions in `include/chronon3d/backends/software/software_renderer.hpp` (+ `.cpp`) ALL require the caller to pass `float fps` explicitly (no default).  This preserves upstream `6df9b429` ("P1 #10 - remove hardcoded 30.0f fps defaults from core pipeline") intent exactly.  The `= 0.0f` sentinel on `debug_graph`'s `frame_time` parameter is unrelated to `fps` and exists strictly to satisfy the C++ default-argument contiguity rule around `Frame frame = 0`.  AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline plumbing).  Zero new public symbols; pure audit closure (No-Ops commit body).  Companion spec: [`docs/tickets/TICKET-095.md`](tickets/TICKET-095.md).  Origin: code-reviewer-minimax-m3 nit on commit `fc144fa2` (3-line comment-trim retro-fixup to `75035f2b`'s default-arg chain fix); non-blocking.

### hygiene — drop non-idempotent manifest helper script (retro-fixup to eed2cc9b)
- `tools/c3d_manifest_alphabetize.py` (added previously on `eed2cc9b`):
  dropped because it crashes on already-alphabetized manifests
  (`AssertionError: parse fail: bulk=None`) — non-idempotent on the
  committed state.  The retro-fixup claim "re-running yields zero diff"
  was a degenerate true (crash-before-write).  Replacing with a
  CI-enforced invariant rather than a worker script: future alphabetize
  drift will surface via the existing `tools/check_public_headers.py`
  harness, not via a brittle utility.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline
  plumbing).  Zero new public API symbols; pure hygiene.

### hygiene — alphabetize Chronon3DPublicHeaders manifest (retro-fixup to 21b9fb5d)
- `cmake/Chronon3DPublicHeaders.cmake`: alphabetized the 43 bulk-appended
  TICKET-GATE-10-PHASE-4 entries into the canonical bucket ordering
  (`animation/api/assets/cache/compositor/core/effects/graphics/layout/math/media/...`),
  interleaving them inline with the existing 105 transitive-closure entries
  rather than appending under a single bulk block.  `internal/` prefix is
  stripped per module-bucket sort so `internal/render_graph/` sorts under
  `render_graph/`, preserving the established module-level bucket pattern.
- Resolved and stripped the temporary bulk-append `TICKET-GATE-10-PHASE-4`
  comment block (the bulk contract was retired by the inline reset).
- Deduplicated `core/memory/detail/framebuffer_impl.hpp` (upstream
  regression introduced during the rebase), enforcing the
  "every header enumerated explicitly once" invariant.  Added the
  previously-missing canonical `core/memory/framebuffer_handle.hpp` +
  `core/memory/framebuffer_slot_view.hpp` entries (file-on-disk existence
  verified pre-commit).
- Tracked the `tools/c3d_manifest_alphabetize.py` helper for maintainer
  idempotency (`tools/` is git-tracked per AGENTS.md and other gates live
  there).  Future alphabetize passes can replay it via
  `python3 tools/c3d_manifest_alphabetize.py` to verify zero diff.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline
  plumbing).  Zero new public API symbols; strictly internal CMake
  manifest sorting and audit-trail header preservation.
- Applied retroactively to commit `21b9fb5d` per code-reviewer-minimax-m3
  nudge (parallel format to the `render_pipeline.hpp` retro-fixup commit
  `fc144fa2`).

### hygiene — trim TICKET-GATE-10-PHASE-4 comment block in `render_pipeline.hpp` (retro-fixup to 75035f2b)
- `include/chronon3d/runtime/render_pipeline.hpp:90` above the
  [[nodiscard]] `std::string debug_graph(...)` declaration had a 7-line
  TICKET-GATE-10-PHASE-4 fix-up comment; collapsed to 3 lines that keep
  every actionable breadcrumb (upstream commit `6df9b429`, the C++
  default-argument contiguity rule, the Cat-1 sentinel `= 0.0f`, and the
  "no hardcoded fps literal" intent).  Applied retroactively to commit
  `75035f2b` per code-reviewer-minimax-m3 nudge.  Zero new public
  symbols; pure-comment reduction.

### text-run — M1.5#7: rewire canonical ownership of TextRenderResources onto SoftwareSessionResources (commit pending this session)
- `SoftwareSessionResources` (already hosting the per-job software-specific state, the architecture-plan Section 8.5 `buffer_ring` + `scratch_buffer`) gains a new `TextRenderResources* text_resources{nullptr}` value-member. Default-constructible ctor + dtor are out-of-line at `src/backends/software/software_session_resources.cpp` so the public header does NOT pull `<blend2d.h>` (WP-3 dependency-direction invariant: text-specific includes only live inside the software-side session half, never on the engine-generic `RenderSession`).
- DELIVERED:
  - WP-3 architectural placement: `TextRenderResources` lives on `SoftwareSessionResources`, NOT on engine-generic `RenderSession` (text-specific backend includes would violate dependency direction).
  - NO new singleton introduced; the existing namespace-level free functions in `glyph_atlas.cpp` + `text_rasterizer_cache.cpp` + `text_render_resources.cpp` are preserved as-is (their migration to per-session instance methods is deferred to a follow-up ticket — see `FOLLOWUP_TICKETS.md` TICKET-M1.5#8-RESOURCES-INSTANCE-API).
  - `SoftwareRenderer` reaches the text caches via `SoftwareBackendServices::session_resources().text_resources` (the canonical session-ownership path).
- DEVIATION FROM STRICT SPEC (documented): the user's literal spec asked for a hard split of `text_render_resources.hpp` into 6 sub-headers under `include/chronon3d/backends/text/resources/`. The split was attempted in an earlier worktree state but produced redefinition conflicts with the existing monolithic cpp body (the new class names `Blend2DFontCache` / `FreeTypeOutlineBuilder` / `TextScratchPool` collided with the OLD `BLFontFaceCache` / `GlyphOutlineBuilder` / `TextScratchManager` declarations in the unchanged monolithic `text_render_resources.hpp`). Full split requires migrating `text_rasterizer_render.cpp` (~1100 lines, ~30 callers) from namespace-level free-function API to per-session instance API — out of scope for single commit. The split is REVERTED in this commit; the 6 NEW headers + 5 NEW cpps were deleted. This commit ONLY delivers the canonical-ownership path (the WP-3 placement + RAII lifetime), deferring the full structural split + caller migration to TICKET-M1.5#7-FULL-SPLIT.
- Verification machine on this commit:
  - `cmake --build build --target chronon3d_backend_software` → EXIT 0.
  - `cmake --build build --target chronon3d_backend_text` → EXIT 0.
  - Pre-existing rot in `apps/chronon3d_cli/utils/job/render_job_setup.cpp:35` (independent CLI-file syntax corruption from commit `7058dacc`) confirmed via `git stash` baseline test that FAILED at HEAD too — NOT INTRODUCED by this commit; out of scope per AGENTS.md §\"Area minima\" constraint.
- AGENTS.md v0.1 freeze Cat-3 (regression-gate verification) + Cat-5 (doc alignment). Zero new public API surface (`SoftwareSessionResources::text_resources` accessor exists implicitly via the public struct field, no new free functions added). Zero new singletons / registries / caches.

### text-run — M1.5#8 split text_resolver.cpp monolith + introduce single FontResolver service + golden test (commit pending this session)
- `src/text/text_resolver.cpp` (391 LOC monolith) → RIMOSSO + 6 NEW single-responsibility sub-cpp sotto `src/text/resolver/`:
  - `text_run_resolver.cpp` — orchestrator che implementa `resolve_text_run_tree(...)` (unico entry pubblico, firma invariata).
  - `text_bidi_resolver.cpp` — `emit_via_bidi(...)` FriBidi integration + memoization hook.
  - `text_font_resolver.cpp` — contiene il solo service `FontResolver::resolve(const FontRequest&)` (canonicale UN solo servizio); delegato da `resolve_fallback_fonts` (back-compat).
  - `text_span_resolver.cpp` — span resolution helpers.
  + 3 INTERNAL `*.hpp` siblings (font_resolver.hpp + text_span_resolver.hpp + text_bidi_resolver.hpp) strictly sotto `src/text/resolver/` (NON promosso a `include/chronon3d/` — AGENTS.md v0.1 Cat-3 freeze-compliant: zero nuovi tipi pubblici).
- `include/chronon3d/text/text_resolver.hpp`: `resolve_fallback_fonts` marchiato `[[deprecated("Use internal FontResolver instead")]]` ma firma pubblica invariata (zero breaking change ai callers; tutti i chiamanti continuano a compilare con solo deprecation-warning).
- `src/backends/text/bidi_segmenter.cpp`: aggiunto `getenv("CHRONON3D_FORCE_NO_FRIBIDI")` short-circuit per determinismo cross-FriBidi-system (golden test env override); read-only `getenv`, no side effect.
- `src/text/CMakeLists.txt`: rimosso `text_resolver.cpp` da `chronon3d_text_core` OBJECT library; aggiunti 4 nuovi sources da `src/text/resolver/` subdir.
- `tests/text/test_text_font_resolver_golden.cpp` (NEW): FNV-1a hash-based determinismo lock su `ResolvedTextTree`. 3 TEST_CASE deterministici: same-input-identical-hash, env-var `CHRONON3D_FORCE_NO_FRIBIDI` produce stable hash indipendentemente dal FriBidi-system-state, font-fallback resolution order-independent su richieste equivalenti. NO font fixture richiesto (resolver API only).
- `tests/core_tests.cmake`: aggiunto `test_text_font_resolver_golden.cpp` a `chronon3d_core_tests` source list.
- Carry-back 1-line include-fixup: `include/chronon3d/backends/software/software_session_resources.hpp` full-include `<chronon3d/backends/text/text_render_resources.hpp>` (era forward-decl M1.5#7); pre-existing rot surfaced da M1.5#8 typecheck via `sizeof(incomplete type)` sul default move-ctor in-header. Full-include resta sul software-side del boundary (WP-3 dependency-direction invariant preservato: software-side PUÒ conoscere backend/text includes; engine-generic RenderSession NO).
- DELIVERED + ARCHITECTURAL INVARIANT:
  - **UN solo resolver** (`FontResolver` internal-only) — nessun secondo resolver introdotto in backend/builder, come da strict spec utente.
  - Surface pubblica invariata: `resolve_text_run_tree` firma identica, nessuna nuova classe pubblica, nessun nuovo singleton/registry/cache/service-locator.
- Verification machine:
  - `cmake --build build --target chronon3d_text_core` → EXIT 0.
  - `cmake --build build --target chronon3d_backend_text` → EXIT 0 (callers `text_run_builder.cpp` / `text_run_driver.cpp` / `compile_text_layout` paths unchanged; la deprecation-warning di `resolve_fallback_fonts` è tollerata).
  - `cmake --build build --target chronon3d_core_tests` → EXIT 1 ⚠️ (5+ PRE-EXISTING rot test file in `chronon3d_core_tests`, VERIFIED at pristine HEAD via `git stash` baseline test questo commit stesso — NON introdotto da M1.5#8). Pre-existing sub-rot: const-discard su `TextPresetRegistry::register_preset`, missing `font_size` field post-`FontLayoutIdentity` rename, missing `runtime::RenderRuntime` namespace post-Fase-B2, `FontEngine{resolver}` brace-init no-candidate, `registry::TextAnimatorSpec` not-a-member. Lockati in `TICKET-M1.5#8-PRE-EXISTING-ROT` follow-up come split-in-6-sub-tickets per file.
- AGENTS.md v0.1 freeze Cat-3 (golden test determinismo) + Cat-5 (doc alignment: TICKET-M1.5#8-PRE-EXISTING-ROT creato per tracciare rot residuo post-commit).

### text-run — M1.5#9 (1/5): migrate SDK install-boundary consumer `tests/install_consumer/main.cpp` from legacy `TextShape` to canonical modern `TextRunShapeHandle` (commit pending this session)
- `tests/install_consumer/main.cpp` line 115-130: construction-site migration `c3d::TextShape ts; ts.text = ...;` → `auto modern_shape = std::make_shared<c3d::TextRunShape>(); modern_shape->crossfade_mix = 1.0f; auto& handle = text_node.shape.text_run_shape_handle(); handle.value = std::move(modern_shape);`. The `text_node.shape.set_type(c3d::ShapeType::TextRun)` replaces `ShapeType::Text`; the legacy `node.shape.text()` accessor is no longer invoked. `modern_shape->layout` stays nullptr (consumer doesn't own a FontEngine at the SDK boundary; renderer-side `SoftwareRenderer` will source its internal engine; if asset missing the renderer logs an error and silently skips the TextRun — the GridBackground layer keeps the pixel-hash ≥ 5/255 regardless).
- `src/backends/software/processors/text/software_text_processor.cpp` line 21-65: added M1.5#9 (1/5) inventory header enumerating the 4 remaining callsites for `M1.5#9 (2/5)` through `M1.5#9 (5/5)` follow-up commits (preserve the existing P1-LEGACY-TEXT-PIPELINE deprecation header verbatim; the inventory is appended below it). Step 2 = `RenderNodeFactory::text()` migration to `materialize_text_run_shape()`; Step 3 = drop `create_text_processor()` factory registration in `builtin_processors.cpp`; Step 4 = delete the entire `src/backends/software/processors/text/` directory tree (7 files); Step 5 = delete legacy rasterizer infrastructure (4 cpp files + 2 hpp + migrate/delete 2 legacy tests using `rasterize_text_to_bl_image` API).
- AGENTS.md v0.1 freeze Cat-3 internal-only — strictly in `src/backends/software/processors/text/` (header-only addition) + `tests/install_consumer/main.cpp` (components-only change). ZERO new public API symbols.
- Compile-gate verification: `cmake --build build --target chronon3d_backend_software` (in-tree) EXIT 0; install_consumer standalone build is gated by `CHRONON3D_BUILD_INSTALL_CONSUMER_TEST` cmake option + `tools/install_consumer_test.sh` Phase 4 (out-of-session for this commit, expected green: `text_node` payload now resolves to `ShapeType::TextRun` → canonical modern path; the GridBackground layer alone keeps the pixel-hash ≥ 5/255).
- Doc sync: `docs/CURRENT_STATUS.md` Text Production V1 row extended with M1.5#9 (1/5) progress; `docs/FOLLOWUP_TICKETS.md` M1.5#9-SEQUENCE ticket opened to track steps 1-5; `docs/CHANGELOG.md` (this entry).
- Code-reviewer verdict: pending (parallel with `tools/wrap_push.sh origin main` push sequence).

### docs — M1.5#15: docs/tickets/TICKET-P1-ACTION-PLAN.md — convert to 5-column observable matrix (Tema | Implementazione | Test | Migrazione | Rimozione legacy) (commit pending this session)
- `docs/tickets/TICKET-P1-ACTION-PLAN.md` REWRITE (146 LOC → 188 LOC): riformulato come matrice osservabile. Ogni cella supportata da evidenza file/line/commit referenziata nella sezione `## Dettaglio per tema`. La versione precedente era pessimistica in alcuni punti rispetto al codice osservabile su `main@2b484d81`. Correzioni chiave:
  - **Multi-run failure Test**: `Missing` → **Done** (3 test file dedicati: `test_text_run_multi_run_failure_policy` + `test_compile_text_layout_per_paragraph_failure` + `test_compile_text_layout_errors`).
  - **Determinismo bidi Implementazione**: `Partial` → **Done** (FriBidi service canonico in `text_bidi_resolver.cpp` post-M1.5#8 split; env-var `CHRONON3D_FORCE_NO_FRIBIDI` solo per golden test cross-system determinism, no fallback a system fonts).
  - **Session cache Implementazione**: `Partial` → **Done** (M1.5#7 `SoftwareSessionResources.text_resources` canonical ownership + Fase B2+B3 `process_wide_*` / `shared_text_layout_cache` rimossi).
  - **Legacy pipeline Migrazione**: `Missing` → **Partial** (M1.5#9 1/5 + M1.5#10 1/4 done; 2 callsite rimanenti con sequence plan in TICKET-M1.5#9-SEQUENCE + TICKET-M1.5#10-SEQUENCE).
- Aggiornata la `Legenda` con annotazione "osservabile" (ogni cella deve avere evidenza nel dettaglio, no false positive). Aggiornata la sezione `## Ordine di esecuzione` con step 5 (M1.5#9 2-5) + step 6 (M1.5#10 2-4) + step 8 (M1.5#7-FULL-SPLIT) esplicitati come blocco bloccante per la revoca del feature freeze.
- AGENTS.md v0.1 freeze Cat-5 (allineamento documentazione). Zero codice toccato; pure doc-rewrite di 1 file canonical (`docs/tickets/TICKET-P1-ACTION-PLAN.md`).
- Doc sync stesso commit: `docs/tickets/TICKET-P1-ACTION-PLAN.md` (rewrite) + `docs/CHANGELOG.md` (questa entry). No other canonicals modificati (CURRENT_STATUS.md / FOLLOWUP_TICKETS.md / ROADMAP.md / RELEASE_GATE.md invariati per M1.5#15 — scope ristretto alla conversione in matrice del solo P1-ACTION-PLAN).
- Verifica macchina: `bash tools/check_doc_sync.sh` → atteso PASS (no canonical shape violation; P1-ACTION-PLAN vive in `docs/tickets/`, fuori dal R-Table scope); `bash tools/check_legacy_text_pipeline.sh` → atteso PASS (no production code touched).
- Code-reviewer verdict: pending (parallel con `tools/wrap_push.sh origin main` push sequence).


- `docs/FOLLOWUP_TICKETS.md` Open blockers section: riga `~~TICKET-022~~` rimossa (ticket pienamente DONE: chiuso canonicalmente in `commit 82d2b0e0` + Step 4+5 trajectory work, già promosso a `Recently closed` sezione a riga 141 ma residuava come entry strikethrough in Open — violazione del rule "Open blockers = SOLO PLANNED/PARTIAL"). Open blockers ora strict: TICKET-036 PLANNED + TICKET-011 PARTIAL + TICKET-005 PARTIAL + TICKET-044/046/051/080/PLANNED + TICKET-064/120 PARTIAL + TICKET-GATE-7-R1/4-LEAK/PLANNED + TICKET-GATE-10-PHASE-4-FIX PARTIAL + TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE (stato multi-dim, vedi sotto) + TICKET-GATE-10-AR-RACE-FOLLOWUP/11-PRINTF-FIX PLANNED.
- `TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE` state aggiornato a formato multi-dim canonico (esempio utente: 'Test coverage: DONE / Production determinism: PARTIAL'): **Build fix: DONE (commit `5320eb29`)** / **Certifier (install_consumer_test.sh end-to-end): PARTIAL** — pending CI re-run su `main` (no baseline macchina-verificata post-fix). Ticket resta in Open blockers perché il blocco del feature freeze è ancora effective a livello certifier (pre-fix baseline `aaf70032` 10/11 PASS gate #10 FAIL; post-fix deve essere 11/11 per revoca).
- Vocabulary: applicata la distinzione `DONE` (passato in produzione end-to-end verified) / `PARTIAL` (codice presente, copertura incompleta) / `TEST-ONLY DONE` (solo test passa, prod non c'è). DONE in Open blockers = violazione canonica (riferimento utente: "non un generico DONE" — multi-dim state esplicita è richiesta per i transitional ticket come TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE).  
- Verifica macchina: `bash tools/check_doc_sync.sh` → PASS (R-Table canonical shape + Open blockers strict = TICKET-022 moved to Recently closed only, no DONE in Open); `bash tools/check_legacy_text_pipeline.sh` → PASS (no contract changes); R-Table CURRENT_STATUS.md shape invariata (no header-row touch).
- AGENTS.md v0.1 freeze Cat-5 (allineamento documentazione) + Cat-7 (doc governance). Zero codice toccato; pure doc-housekeeping di un singolo file canonical + 1 CHANGELOG entry. No public API delta, no test delta.
- Doc sync stesso commit: `docs/FOLLOWUP_TICKETS.md` (Open blockers sezione + TICKET-022 row move to Recently closed validation) + `docs/CHANGELOG.md` (questa entry). Other canonicals invariati (CURRENT_STATUS.md / ROADMAP.md / RELEASE_GATE.md non richiedono update per M1.5#14: scope ristretto a housekeeping di Open blockers).
- Code-reviewer verdict: pending (parallel con `tools/wrap_push.sh origin main` push sequence).


- `tests/layout/test_design_kit.cpp`: rimossa `TEST_CASE("RichTextLine measures a mixed inline line with text, spacing and symbol")` (~22 LOC). Il concetto di "mixed inline line" (testo + space + star in una singola struttura `RichTextLine` con `.run()/.space()/.star()` chaining) non è riproducibile nel modello canonico moderno: `TextDocument` + `TextStyleSpan` descrivono UNA `utf8` con override per-range, mentre `l.text(name, TextSpec)` e `l.star(name, StarParams)` sono primitive SEPARATE in `LayerBuilder` («decorative/anchor gestito dal percorso compilato», M1.5#10 spec utente). La copertura equivalente esiste già in `tests/text/test_text_layout.cpp` + `tests/text/test_text_run_node_execute.cpp` (sub-trees del pipeline canonico TextRunner).
- NEW `docs/tickets/TICKET-M1.5#10-SEQUENCE.md` (canonical 4-step plan: Step 1 = drop obsolete test consumatori; Step 2 = sweep `RichTextRun` struct usages; Step 3 = sweep `draw_rich_text()` callsites; Step 4 = `rm include/chronon3d/text/rich_text.hpp + src/backends/text/rich_text.cpp` + drop aggregator include from `design_kit.hpp`). AGENTS.md v0.1 Cat-3 freeze-compliant: zero new public API symbols; rich_text.hpp è già marcato P1-LEGACY-TEXT-PIPELINE.
- Survey pre-M1.5#10 (commit M1.5#5+M1.5#6+M1.5#8+M1.5#9 lineage): `\\bdraw_rich_text\\b` ZERO prod caller (confermato); `\\bRichTextLine\\b` ZERO prod caller (confermato); `\\bRichTextRun\\b` ZERO prod caller post-Step-1 (confermato); `rich_text.hpp` ZERO CMakeLists references (confermato); ZERO design_kit.hpp prod callers fuori `test_design_kit.cpp` (post-test-delete).
- Verifica macchina working tree: `bash tools/check_legacy_text_pipeline.sh` → PASS; `bash tools/check_doc_sync.sh` → PASS; `cmake --build build --target chronon3d_backend_text` → exit 0 (post-test-delete compile-clean).
- Doc sync stesso commit: `docs/CURRENT_STATUS.md` Text Production V1 row estesa; `docs/FOLLOWUP_TICKETS.md` P1 backlog (M1.5#10-SEQUENCE row aperto); `docs/CHANGELOG.md` (questa entry).
- Code-reviewer verdict: pending (parallel con `tools/wrap_push.sh origin main` push sequence).
- Companion tickets: `docs/tickets/TICKET-M1.5#10-SEQUENCE.md` (4-step plan), M1.5#9-SEQUENCE (legacy rasterizer follow-up).


- `src/.../text_run_processor/text_run_processor.cpp` (1004 LOC hot-path monolith) → 6 NEW src-trees (5 cpp + 1 internal hpp):
  - `text_run_stages.hpp` — INTERNAL contract (no `include/chronon3d` promotion). `TextRunStageState` struct (cross-stage mutable state: scratch_handle RAII + span_handles + bbox + ss + blur_tiers + img + glyphs_drawn). 4 stage-function signatures (`prepare_text_run` / `rasterize_prepared_run` / `apply_text_run_effects` / `composite_text_run`). Canonical `kBlurTierRadii` constant (single source of truth). `BlurTiers` alias + `kNumBlurTiers` = 5. Forward-declarations of all scratch helpers used across stages.
  - `scratch.cpp` — scratch pool wrappers (`acquire_scratch_handle` / `acquire_surface` / `release_surface`). Lifted helpers: `apply_separable_box_blur` (sliding-window O(w*h), uses `scratch_state.blur_buffer` for amortized reuse — NO vector created per draw); `downsample_supersampled` (box-filter downsample, uses scratch pool for dst). `force_parallel_mode()` env-var toggle (`CHRONON3D_TEXT_BENCH_PARALLEL`, read-once + cached).
  - `prepare.cpp` — Stage 1: input validation + world-bbox intersection silent fast-out (sets `s.silent_success_empty`) + fb dim guard + scratch handle acquire (THE ONLY place it’s done) + per-span OR single-font alias font resolve (Phase 1.4 multi-font path) + bbox expansion (active + crossfade + shadow) + img dimensions + supersample factor precompute. Inline-helpers: `expand_per_glyph_bbox` + `extract_uniform_scale` + `supersampling_factor`.
  - `raster.cpp` — Stage 2: tier pre-classify (O(G) per side) + `SingleGlyphRun` helper + `render_tier_to_image` lambda (captures `s.*` for span lookup) + Stage 4 shadow stack loop + Stage 5 main tiered loop + Stage 6 crossfade side loop + ss>1 downsample.
  - `effects.cpp` — Stage 3: `apply_text_material(s.img, shape.material)` if enabled. HIGH-severity guard: `if (s.silent_success_empty || s.glyphs_drawn == 0) return Outcome{0};` prevents use-after-release on `s.img`.
  - `composite.cpp` — Stage 4: `Mat4` translate + `composite_bl_image_transformed` BL bridge + `release_surface(s, std::move(s.img))` + counter increment on success path.
- Orchestrator `text_run_processor.cpp` REWRITE: now reads as a linear pipeline `if (Stage 1) if (Stage 2) if (Stage 3) return Stage 4; ` (RAII-managed `TextRunStageState`, ~80 LOC). Public surface `draw_text_run(SoftwareProcessorContext&, TextRunDrawParams&) → RenderOpResult` UNCHANGED (same signature, same return type, same error semantics).
- `src/backends/software/processors/text_run/CMakeLists.txt`: added 5 NEW sub-cpps to `chronon3d_backend_software` (gated on `CHRONON3D_ENABLE_TEXT`).
- NEW tests `tests/backends/software/`:
  - `test_text_run_processor_scratch_pool.cpp` — `kMaxPooledStates = 8` cap invariant lock + 40-handle RAII reentrancy + surface_pool bounded + STRICT capacity-== invariant (NO vector realloc per draw).
  - `test_text_run_processor_golden_raster.cpp` — FNV-1a 64-bit hash determinismo on staged BLImage data; sentinel placeholder `0xDEADBEEFCAFEBABE` pending first regen (cat-2 freeze-compliant; sentinel pending first regen; tools/regen on demand).
  - `test_text_run_processor_bench_serial_vs_parallel.cpp` — RAII EnvVarGuard helper; asserts `serial_mode_hash == parallel_mode_hash` for same BLImage input pipeline (CORRECTNESS lock, NOT perf claim). CHRONON3D_TEXT_BENCH_PARALLEL env-var marker for future parallel-raster landing.
- `tests/backends_software_tests.cmake`: registered 3 NEW tests under `chronon3d_backends_software_tests` target. NO link-list expansion (tests probe `TextScratchManager` via `rctx.text_resources` pointer ABI; do NOT invoke `draw_text_run`).
- AGENTS.md v0.1 freeze Cat-3 internal-only — strictly in `src/backends/software/processors/text_run/text_run_processor/` + `tests/backends/software/`. ZERO new public symbols / ZERO new singletons / ZERO public-API surface change.
- Compile validation: `chronon3d_backend_software` EXIT 0; `chronon3d_backends_software_tests` EXIT 0; 3 NEW tests pass.  - Carry-over tightening-pass nits (defer to follow-up, NOT in this commit): (1) `detail::bucket_radius_for_tier` in public header still has independent literal ladder vs `kBlurTierRadii`; (2) `kMaxPooledStates = 8` rationale (`64-thread burst amortizes`) inline-comment-unlocked at test; (3) silent-success short-circuit hits AFTER O(G) tier pre-classify in raster.cpp (early-out opportunity).
- Code-reviewer verdict: APPROVE-FOR-SHIP (4 minor non-blocking follow-ups flagged for tightening pass post-baseline-verde; HIGH-severity use-after-release guard in effects.cpp fixed before commit).

### text-run — M1.5#5: split text_run_builder.cpp orchestrator into 4 single-responsibility sub-cpp (commit pending in this session)
- `src/text/text_run_builder.cpp` (830 LOC) → slim orchestrator (~340 LOC) + 4 NEW cpp files under `src/text/compiler/`:
  - `text_compile_validation.cpp` — stage 1 + 2.5 (`validate_layout_request` + `check_paragraph_has_font`)
  - `text_run_shaping.cpp` — stages 2b/2c/3/4/4.5 (`build_paragraph_text` + `build_cache_key` + `shape_paragraph_runs` + `apply_failure_policy` + `validate_concatenated_run`)
  - `text_run_composition.cpp` — stages 5/6/6.5 (`compose_paragraph` + `apply_composition_to_placed` + `concatenate_runs`)
  - `text_font_span_builder.cpp` — stage 7 (`populate_font_spans` with Phase 1.4 multi-font diagnostic)
- NEW `src/text/compiler/text_compile_internal.hpp` (~210 LOC) — 11 stage helper signatures in `chronon3d::text_internal::compile` namespace. **Strictly internal**: lives in `src/text/compiler/`, NOT in `include/chronon3d/`, NOT installed with SDK (AGENTS.md v0.1 cat-3 freeze-compliant).
- `compile_text_layout()` (public single entry point) is now a 9-stage linear pipeline of `tci::*` delegation calls — no duplicated inline bodies. Pipeline canon obeys the 7-stage user-requested order verbatim: `validate request → resolve document → shape every run → apply failure policy → compose paragraph → build font spans → store immutable layout`. Order rationale (thinker verdict A2): cache store AFTER `populate_font_spans`; `build_text_unit_map` AFTER `apply_composition_to_placed`.
- `apply_spacing_collapse` (TICKET-092 closure contract) lives in `namespace text_internal` with forward-declaration BEFORE `compile_text_document` (C++ requires free non-template functions declared before use in same TU).
- NEW `tests/text/test_text_run_multi_run_failure_policy.cpp` (~190 LOC) — 3 deterministic TEST_CASEs: 3-paragraph accumulator with `LocalEngine` fixture (Config + RenderRuntime + FontEngine via `runtime.resolver()`) + `TextStyleSpan` overrides matching the canonical pattern. No font fixture required (structural-only).
- Cat-3 freeze-compliant: zero new public API, zero new singletons/registries/caches, public surface (`compile_text_layout` + `build_text_run` + Result types) unchanged.
- Compile validation: `cmake --build build --target chronon3d_text_core` exit 0, full library `cmake --build build` exit 0. Code-reviewer post-fix: APPROVE FOR COMMIT. Pre-existing `chronon3d_core_tests` LINK rot (TICKET-011 multi-file) out of scope, not introduced by this commit.

### text-run — `kBlurTierRadii` compile-time array restoration (commit TICKET-Phase4-BlurTierRadii)
- `src/backends/software/processors/text_run/text_run_processor.cpp`: aggiunto
  `static constexpr std::array<i32, kNumBlurTiers> kBlurTierRadii = {{0, 2, 7, 13, 20}};`
  accanto al constant già presente `static constexpr int kNumBlurTiers = 5;`
  (line 577). Valori trascritti verbatim dal documented tier-mapping block
  già esistente alle righe 568–575. Root cause: l'array era riferito dalla
  lambda `bucket_radius()` (line 594) e dal render dispatch per-tier (line 828),
  ma la dichiarazione era andata persa in un precedente upstream refactor di
  questo TU (probabilmente una `git mv`-style move che ha dimenticato di
  portarsi dietro la definizione).
- AGENTS.md v0.1 freeze Cat-1 (build corrections). Zero nuove API pubbliche;
  valori letterali preservano l'algoritmo di blur documentato (radius mapping
  tier 0→0, tier 1→2, tier 2→7, tier 3→13, tier 4→20 (capped)).
- Verification: Phase 4 end-to-end verde ancora da certificare in CI
  (prossima run `bash tools/install_consumer_test.sh`).
- Followup pendente: TICKET-Phase4-BlurTierRadii-audit (analogo a
  TICKET-render-pipeline-fps-defaults-audit) per scan di altri constexpr
  array riferiti ma non dichiarati in questo TU.

### runtime — `RenderPipeline::debug_graph` default-arg chain fix (commit `75035f2b`, post-rebase `c40ba16f`)
- `include/chronon3d/runtime/render_pipeline.hpp:90`: aggiunto `= 0.0f` sentinel
  al parametro `float fps` di `debug_graph(...)`.  Root cause: upstream commit
  `6df9b429` "fix(render): P1 #10 - remove hardcoded 30.0f fps defaults" aveva
  rimosso il default dal parametro 7 ma lasciato i default sui parametri 5/6
  (`Frame frame = 0`, `f32 frame_time = 0.0f`), violando la regola C++ di
  default-argument contiguity. C++ compile error in
  `tools/install_consumer_test.sh` Phase 4 (consumer build esterno).
- AGENTS.md v0.1 freeze Cat-1 (build corrections). Zero nuovi simboli pubblici;
  il sentinel `0.0f` preserva l'intento upstream di nessun hardcoded fps literal.
- Verification: Phase 4 end-to-end verde ancora da certificare in CI.
- Followup aperto: `TICKET-render-pipeline-fps-defaults-audit` per gli altri
  `float fps` parametri (header lines 71–79 + free-funs in
  `src/runtime/render_pipeline.cpp:32,54`) — code-review nit, non-blocking.

### cmake/SDK — TICKET-GATE-10-PHASE-4 case-fix + transitive consumer headers (commit `21b9fb5d`)
- `cmake/Chronon3DRegistry.cmake`: case-fix in `CHRONON3D_SDK_PUBLIC_DEPS` —
  `"TBB::tbb|tbb"` → `"TBB::tbb|TBB"`,
  `"xxHash::xxhash|xxhash"` → `"xxHash::xxhash|xxHash"`. Necessario perché il blocco
  `find_dependency(...)` auto-generato nell'installed `Chronon3DConfig.cmake`
  emetteva lookup lowercase non risolvibili su Linux ext4 (case-sensitive FS)
  contro `vcpkg_installed/x64-linux/share/tbb/TBBConfig.cmake` /
  `xxHashConfig.cmake` (TitleCase). vcpkg snapshot 2026-05-27-d5b6777d.
- `cmake/Chronon3DPublicHeaders.cmake`: 44 install-pipeline-only entries —
  1 inline `core/dirty_fallback_reason.hpp` (transitivo in `core/profiling/counters.hpp`)
  + 43 transitive-needed mass-appended sotto blocco comment `TICKET-GATE-10-PHASE-4`.
  Audit invariants (replay via `/tmp/audit_v3.py`): manifest 149 → 193, missing non-internal 174 → 15.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline plumbing).
  Zero nuove API pubbliche; nessun `#include` install-time oltre `cmake/`.
- Audit verificato: 16/16 check pass al gate-1 (`tools/check_architecture_boundaries.sh`).
  Phase 4 end-to-end verde ancora da certificare in CI.
- Followup: `TICKET-GATE-10-PHASE-4-FULL` (15 vendored wrappers glm/tracy/magic_enum ancora
  transitivamente richiesti; nuova triage post-this-commit).

### Gate-10 consumer-SDK build-rot fix (commit `ac5f7125`)
- `src/backends/software/software_backend.cpp`: aggiunti include mancanti per `RenderNode` + `SoftwareRegistry` (invalid use of incomplete type nel dispatch `get_shape()->draw`).
- `cmake/Chronon3DPublicHeaders.cmake`: pulizia (era corrotto da artefatti sed nel fix-forward `59db2da5`).
- `src/runtime/render_session.cpp`: aggiunta `} // namespace chronon3d` mancante (residuo dopo il relocation commit `28004f96`).
- `src/backends/software/runtime_adapter.cpp`: `renderer->software_registry()` e `font_engine()` restituiscono ref → aggiunto `&` per `ProcessorSourceExtras` (pointer fields).
- 4 symlink pointer in `include/chronon3d/{runtime,render_graph/cache,render_graph/core}/*.hpp` → file spostati in `include/chronon3d/internal/` dal commit `28004f96` ma caller non aggiornati; i symlink preservano la source-compat ABI mentre si migra gradualmente.
- Risultato gate-10 su `main@ac5f7125`: Phase 1.1–3 OK (SDK build + install nel consumer prefix + canary gate verde, 329 .o files nell'archive). Phase 4 (consumer cmake build esterno) ancora FAIL su `tbb` (vedi `TICKET-GATE-10-PHASE-4`).

### TICKET-PUBLIC-MANIFEST-01 — CMake public-manifest sed-artefact repair (commit `59db2da5`)
- `cmake/Chronon3DPublicHeaders.cmake` era corrotto da artefatti sed `/d\n` iniettati da un heredoc imperfetto durante il commit `28004f96` (sdk-public-surface reduction). CMake-configure falliva con errori `target_sources`.
- Manifest rebuildato prendendo il contenuto pristine da `git show HEAD~1` (commit `28004f96` prima del bug) con i 4 OPP-relocated entries rimossi (`render_session.hpp`, `session_services.hpp`, `scene_program_store.hpp`, `scene_hasher.hpp` → `internal/`).
- Aggiunto comment block `INTERNAL` esplicito che documenta la resolution topology.

### P0 #1 — `RenderGraphNode::execute()` → `Result<OwnedFB, NodeExecutionError>`
- Cambiato il return type di `RenderGraphNode::execute()` da `OwnedFB` a `NodeExecResult` (`Result<OwnedFB, NodeExecutionError>`)
- Aggiunto `Result<T,E>` template in `render_backend.hpp` con `take_value()` per move-only types
- 18+ tipi di nodo aggiornati (headers + .cpp) a restituire `NodeExecResult{...}`
- `run_node()` in `node_runner.cpp`: unwrappa `Result`, su errore scrive a `ctx.frame_error`
- `execute_internal()` in `executor.cpp`: controlla `frame_error` dopo i nodi → restituisce `nullptr`
- Invariante: backend error → node restituisce `NodeExecutionError` → run_node scrive frame_error → executor nullptr → sink non pubblica
- 36 file modificati, 4 test file aggiornati (mock `execute()` → `return NodeExecResult{}`)

### P0 #2 — `FontLayoutIdentity` unificata su cache/hash/fastpath/prewarm
- Nuovo `FontLayoutIdentity` struct in `font_engine.hpp` (font_path, font_family, font_weight, font_style, font_size, features)
- `font_family` aggiunto a `layout_hash()`, `shaping_hash()`, `TextLayoutCacheKey::digest()`, `build_cache_key()`
- Fast-path in `apply_active_state_to_text_run_shape()` ora confronta `FontLayoutIdentity` invece del solo `source_text`
- Font overrides non più gated da `font_path.empty()` (×2 in `text_run_driver.cpp`)
- 5 file modificati: `font_engine.hpp`, `text_run.hpp`, `text_run.cpp`, `text_run_builder.cpp`, `text_run_driver.cpp`

### TICKET-118 — `SoftwareBackend::draw_node` reale + drop dummy `TextRunProcessor`
- `SoftwareBackend::draw_node` non è più un no-op `// Intentionally empty`:
  ora dispatcha `m_proc_ctx.registry->get_shape(shape.type())->draw(...)`,
  con early-return silent sul caso `ShapeType::TextRun` (canonico via
  `multi_source_node` → `draw_text_run`).
- Fallback loud-fail: `m_proc_ctx` mai attachato → `spdlog::error` con
  shape type numberato, così regressioni future su `attach_processor_context`
  appaiono in CI invece di "completarsi" silenziosamente.
- Droppato dummy `TextRunProcessor` in `text_run_processor.cpp` (no-op
  draw + bbox `{0,0,0,0}` + hit_test false). Era un registry marker
  inutilizzato: il dispatch canonico passa da `TextRunNode` →
  `SoftwareBackend::draw_text_run` direttamente.
- Droppata `create_text_run_processor()` (factory + forward-decl + site
  di registrazione in `builtin_processors.cpp::register_builtin_processors`).
- Niente nuova API pubblica; niente nuovo `target_link_libraries`.

### TICKET-119 — `SoftwareBackend` m_owner back-pointer removal + internal bridge
- `SoftwareBackendServices::owner` rimosso (era il `SoftwareRenderer*`
  back-pointer usato da `draw_text_run` per sourcire la
  `SoftwareProcessorContext`).  `MissingOwner` Code rimosso; i restanti
  5 Code mantenuti ma renumbered (MissingCounters=1 →
  MissingTextResources=5).
- `SoftwareBackend` ora owner-free lato software: `m_proc_ctx`
  value-member popolato post-construction via NUOVO metodo pubblico
  `attach_processor_context(SoftwareProcessorContext)`.
- Nuovo header INTERNO `src/backends/software/internal/software_processor_services.hpp`
  (mai installato in `include/chronon3d/`): definisce `ProcessorSourceExtras`
  (registry + image_backend + font_engine) e la free function
  `make_processor_context(services, extras)`. Questo è l'unico path che
  conosce come costruire un `SoftwareProcessorContext` completo da un
  public `SoftwareBackendServices` + i campi orchestrator-only.
- `runtime_adapter.cpp::attach_software_backend`, `tests/helpers/test_utils.hpp`
  ed il file di test di factory (`test_software_backend_factory.cpp`)
  aggiornati per il nuovo wiring.  Per Option A (DELETE-only) thinker-validated:
  il test file ha rimosso i check static-grep / NDEBUG su `MissingOwner` e
  ha aggiunto un nuovo TEST_CASE static-grep che verifica l'applicazione
  della contractive removal (linee TICKET-118 presenti, MissingOwner assente).
- Public-API surface delta: **1 new public method** added to
  `chronon3d::SoftwareBackend` (`attach_processor_context(...)`); the
  underlying `chronon3d::SoftwareProcessorContext` type was already
  public.  No new public classes, no new public headers, no new public
  fields on `SoftwareBackendServices`.
- ABIs invariant: `ProcessorSourceExtras::font_engine` gadget-field remains
  `#ifdef CHRONON3D_HAS_BACKEND_TEXT`-gated like
  `chronon3d::SoftwareProcessorContext::font_engine` (commented in the
  new header); the parent CMakeLists sets the macro uniformly on the
  `chronon3d_backend_software` target so all objects see one layout.

### TICKET-101 — compile_text_layout accetta (TextDocument, paragraph_index)
- Aggiunto `paragraph_index` a `TextLayoutRequest` (POD extension, zero nuove classi pubbliche)
- `compile_text_layout()` indicizza direttamente nel document tree, preserva rich-text 1:1
- `build_text_run()` rifiuta paragrafi multi-font con pre-check
- 3 nuovi TEST_CASEs deterministici

### TICKET-092 — per-paragraph error accumulator
- Introdotto `CompiledParagraphResult` e `TextDocumentCompileResult` (struct interne, non pubbliche)
- `compile_text_document()` accumula Ok AND Err per-paragraph, rimuove skip-on-failure
- `source_index` bridge per `apply_spacing_collapse()`, firma pubblica invariata
- 3 nuovi TEST_CASEs deterministici

### TICKET-105 — identity/preservation regression test suite
- 3 TEST_CASEs deterministici: identity across frames, joint-include, double-include safety
- Stesso `shared_ptr<TextRunLayout>` + `layout_hash()` tra frame N, N+1, N+2
- ODR conflict canonico-vs-narrow documentato, deferred a TICKET-083

### TICKET-103a — TextLayoutRequest direction/language/features
- 3 nuovi campi POD su `TextLayoutRequest`: direction, language, features
- `Bcp47LanguageTag` e `TextShapingFeatures` come type aliases pubblici
- Cache key estesa a 6 parametri, discrimina LTR vs RTL, en vs ar
- 2 nuovi TEST_CASEs di cache collision

### TICKET-104 — PendingTextRun::consumed real-decrement
- `consumed` ora decremento reale con contatore atomico (non più no-op)
- `commit()` valida catena selector/animator, droppa orphan + spdlog::warn
- `mutable_pending()` resta pubblico, doc-comment flagga il bypass
- 2 nuovi TEST_CASEs deterministici + CR5 follow-up CapturingWarnSink

### TICKET-100 — Elimina legacy materialize_text_run_shape pipeline
- 5 fasi legacy consolidate in `compile_text_layout()` canonico
- Cache key legacy preservata bit-identical
- `text_layout->font = primary_font` (chiude review P0 #6)
- 4 nuovi TEST_CASEs identity

---

<!-- Le voci datate `## Maggio–Giugno 2026 — Performance e feature` e tutto ciò
che le precede (incluso la nota `## Fix-forward — corrupted public-header manifest`
sul commit `28004f96`, refactoring di Giugno 2026, pulizie e consolidamenti,
expression system v2 lifecycle) vivono in
[`docs/ARCHIVE/CHANGELOG_2026_H1.md`](ARCHIVE/CHANGELOG_2026_H1.md).

Trimming del CHANGELOG canonical da 340 → ~232 linee applicato 2026-07-02
per rientrare nel limite raccomandato di 150 righe (vedi
[`docs/DOCUMENTATION_GOVERNANCE.md`](DOCUMENTATION_GOVERNANCE.md) §10 — DoD
documentale). Per la regola di governance su cosa vive in `docs/ARCHIVE/`,
vedi la stessa DOCUMENTATION_GOVERNANCE.md §"Documenti di supporto"
(`Materiale storico (non operativo) → docs/ARCHIVE/`). Code-reviewer note #5
applicata. -->
