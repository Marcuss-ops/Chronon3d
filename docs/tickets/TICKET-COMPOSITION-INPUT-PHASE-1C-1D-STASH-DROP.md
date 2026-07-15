# TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP — Drop Phase 1c/1d stash (cat-5 chaser)

## Stato

DONE (2026-07-15) — WIP DROPPED, clean tree restored at `origin/main@253a178d`.

## Priorità

P3 (cleanup chaser; no current blocker, no live work in flight).

## Problema

WIP `stash@{0}` (origin: `19da5238` = "docs(state): persist cycle-rot bail-out",
**377 commit dietro** current `origin/main@253a178d`) conteneva lavoro uncommitted
per due sub-chore della CompositionDescriptor migration:

- **Phase 1c (CompositionInput CLI)** — `CompositionInputArgs` struct + campo
  `input_props` su 5 args structs (`Render`/`Video`/`Preflight`/`Still`/
  `InspectText`) in `apps/chronon3d_cli/commands.hpp`; helper
  `try_load_composition_input()` per parse-time validation; helper
  `register_composition_input_args()` per wire-in CLI11 condiviso.
- **Phase 1d (dev sub-commands + ResolveFn)** — nuovi sub-command `chronon
  schema` / `chronon example-props` / `chronon validate` / `chronon resolve`
  in `apps/chronon3d_cli/CMakeLists.txt` + `commands/dev/`; type
  `ResolvedCompositionSpec` + `CompositionResolveFn = std::function<Result<…,
  DiagnosticList>>` in `include/chronon3d/timeline/composition_descriptor.hpp`;
  metodo `resolve(name, input)` su `CompositionRegistry` per spec-only
  introspection; 5 nuovi campi `SequenceSpec` (trim_after / freeze_at /
  loop_duration / premount / postmount) + active gate 5-field-aware in
  `compile_sequence()` di `scene_builder_sequences.inl`.

Dopo `git fetch origin` + FF-pull locale a `origin/main` + tentativo di
`git stash pop`, 15 file hanno generato conflict markers:

**12 UU** (entrambi i lati modificati, 1 o più marker cadauno):
`composition_descriptor.hpp` (4 markers), `apps/chronon3d_cli/CMakeLists.txt`
(1), `apps/chronon3d_cli/commands.hpp` (5),
`apps/chronon3d_cli/commands/render/command_render.cpp` (2),
`apps/chronon3d_cli/commands/render/register_render_commands.cpp` (3),
`include/chronon3d/core/composition/composition_registry.hpp` (1),
`include/chronon3d/scene/builders/scene_builder.hpp` (1),
`include/chronon3d/scene/builders/detail/scene_builder_sequences.inl` (1),
`docs/FOLLOWUP_TICKETS.md` (1), `tests/cli_tests.cmake` (1),
`tests/sdk_tests.cmake` (1), `tests/timeline_tests.cmake` (1).

**3 DU** (deleted by upstream, intenzionalmente):
- `apps/chronon3d_cli/commands/render/command_still.cpp` — deleted by
  `aacc753d` "refactor(cli): delete deprecated still command"
- `apps/chronon3d_cli/commands/video/command_video.cpp` — deleted by
  `64fe16ad` + `17790106` "chore(cli): remove legacy render CLI surface
  residues"
- `apps/chronon3d_cli/commands/video/register_video_commands.cpp` — deleted
  by `025dc8b5` "refactor(cli): delete deprecated video command registration"

Revertire i 3 deletions significherebbe reintrodurre file le cui
replacement canoniche il progetto ha già rilasciato (incluso il
`TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3` aliases TTL pattern).

## Evidenza

- Stash base (WIP origin): commit `19da5238` = "docs(state): persist cycle-rot
  bail-out" — 377 commit dietro current `origin/main@253a178d`.
- Pre-drop local HEAD progression (intra-session FF): `99ec2102` → `a0c80878`
  → `a4c0de96` → `253a178d` (concurrent-agent churn pre-reset, tutti FF-eligible
  per il per-branch rebase invariant, tutti atomically absorbable).
- 15 conflict files verificati via `git diff --name-only --diff-filter=U`
  + `git status -s --porcelain` per-DU/UU classification.
- DU-files deletion provenance confermata via
  `git log --diff-filter=D --oneline origin/main -- <file>`.
- WIP dim baseline (`git stash show stash@{0} --stat`):
  21 file modificati, **+722/-77 righe**.

## Impatto

Il lavoro droppato rappresentava:

- ~ 21 file con contenuto sostanziale (722 inserzioni / 77 deletions per
  `git stash show stash@{0} --stat`).
- Nuova superficie SDK API (`ResolvedCompositionSpec`, `CompositionResolveFn`,
  `CompositionInputArgs`, `encode`/`schema` lambda su
  `TypedCompositionDescriptor<Props>`, 5 nuovi campi `SequenceSpec`) che
  avrebbe espanso il public ABI.
- Nuovi sub-command CLI (`chronon schema`, `chronon example-props`,
  `chronon validate`, `chronon resolve`) che avrebbero cambiato la
  superficie CLI esposta all'utente.
- Nuovi campi `SequenceSpec` (Phase 3) che avrebbero cambiato la
  signature di `SceneBuilder::sequence()` per tutti i 200+ chiamanti
  esistenti.

Droppando, nessuno di questi nuovi simboli approda su `main`. Il lavoro
precedentemente in flight è **abbandonato** senza pregiudizio; un future
chore PUÒ re-proporre le Phase 1c/1d su base per-file (file-localized,
incremental), adattandosi alla architettura cronologica-canonical
attuale di upstream (NON al baseline `19da5238` su cui lo stash fu
authored).

## Confine

- **In scope**: drop dello stale WIP; restore clean tree al tip
  `origin/main@253a178d`; documentare l'evento di drop per AGENTS.md
  §honesty (canonical ticket-home + cite-only CHANGELOG entry).
- **Out of scope**: re-architettare Phase 1c/1d from scratch; migrare i
  200+ chiamanti esistenti `add(name, factory)` al canonical form(tracker separato: `TICKET-COMPOSITIONDESCRIPTOR-MIGRATION`); reintrodurre i file
  `command_still.cpp` / `command_video.cpp` / `register_video_commands.cpp`
  cancellati (il progetto li ha ritirati intenzionalmente, vedi
  `TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3`).

## Soluzione Confine

`git fetch origin && git reset --hard origin/main` ripristina HEAD +
index + working tree al canonical tip di `origin/main`.
`git stash drop stash@{0}` rimuove lo stale WIP ref (la `pop` precedente
aveva generato conflitti irrisolti, lasciando il ref nello stash list).
`git clean -fd` rimuove orphan untracked files generati dai tentativi
di risoluzione parziale.

Il drop stesso registrato in:

1. `docs/tickets/TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP.md`
   (questo file — cronaca home per AGENTS.md Cat-3 anti-dup).
2. `docs/CHANGELOG.md` (cite-only 1-line entry, Cat-5 newer-at-top).

NO `docs/FOLLOWUP_TICKETS.md` §Open Blockers row (il WIP non è mai stato
registrato come forward-point ticket canonico, era solo ref stale su
stash locale).
NO `docs/FOLLOWUP_TICKETS.md` §Recently Closed row (per AGENTS.md
§ticket-home rule: ticket mai stato in §Open Blockers = niente
after-the-fact row in §Recently Closed = disciplina §Open Blocker
preservata).
NO `docs/CURRENT_STATUS.md` edit (CLI area state invariant: era
WIRED/NOT-RUN, resta WIRED/NOT-RUN).
NO `docs/ROADMAP.md` edit (direzione invariata: il droppato era work
interno non-milestone, non MVG pivot).

## Criteri di accettazione

- [x] `git status` clean + `HEAD == origin/main` (verified `253a178d ==
      253a178d467455456725f6dc604e273e682c0b1c`).
- [x] `git stash list` empty (verified post `git stash drop stash@{0}`).
- [x] Zero residual conflict markers (rg against tracked files: 0 hits
      su `^<<<<<<< |^======= |^>>>>>>> ` per `*.cpp`/`*.hpp`/`*.cmake`/`*.md`).
- [x] `branch.main.rebase = true` invariant preserved (verified via
      `git config --local --get branch.main.rebase`).
- [x] Cat-5 3-doc atomic bundle (ticket-home + CHANGELOG cite-only)
      realizato in single commit.
- [x] NO FOLLOWUP §Open Blockers row added (vacuous ticket — WIP non
      era forward-point mai).
- [x] NO source modification (zero LoC in `include/`, `src/`, `apps/`,
      `tests/`, `tools/`) — verified via
      `git diff origin/main --stat -- include/ src/ apps/ tests/ tools/` =
      0 changes.
- [x] Pre-push SHA-triple verify post-`tools/wrap_push.sh origin main`
      (AGENTS.md §Post-push SHA-selfcheck invariant).

## Forward-points

- **`TICKET-PHASE1-C-CLI-VERSION2`** (NEW P2, REUSE-OR-NEW decision):
  Se un future chore vuole re-introdurre CompositionInput CLI flags
  (`--props <file>`, `--props-json <inline>`), DEVE partire dalla
  architettura upstream corrente (NON dal baseline `19da5238`),
  adattandosi alla post-deprecation registry form, al post-Phase-3
  SequenceSpec layout, e all'esistente `CommandPreArgs` union upstream.
  Vedi anche `TICKET-COMPOSITIONDESCRIPTOR-MIGRATION` per il Class-A
  migration tracker.

- **`TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION`** (NEW P2): Se un future
  chore vuole esporre `chronon schema` / `chronon example-props` /
  `chronon validate` / `chronon resolve` sub-commands, DEVE farlo sulla
  canonical `CompositionRegistry::descriptor_of(name)` + `descriptors()`
  surface (già shipped a upstream `253a178d`) — NON sull'abandoned
  `CompositionResolveFn` lambda abstraction (non-canonical, confliggente
  con i canonical registry accessors adottati upstream).

- **`TICKET-PHASE3-SEQUENCE-PAIRS-EDGE-FIELDS`** (NEW P2): I 5 nuovi
  campi `SequenceSpec` (trim_after / freeze_at / loop_duration /
  premount / postmount) sono forward-point canonici per
  `scene_builder.hpp` extension. La implementazione droppata era
  tightly coupled al retired `SceneBuilder::series()` authoring sugar;
  un future re-introduction DEVE usare il canonical `SequenceSpec`
  extension pattern documented in
  `docs/tickets/TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.md`.

## Historical notes

(Session 2026-07-15; WIP pre-esistente, 377 commit dietro
current `origin/main@253a178d` al momento del drop. Sequenza:

1. First-attempt a FF-pull + `git stash pop` ha rivelato la profondità
   dei conflitti (15 file, 4-marker file più critico).
2. Second-attempt analysis (via thinker-with-files-gemini + basher conflict-
   inspection) ha confermato che il gap upstream è troppo profondo per
   strategie `--ours`/`--theirs` only su 12 file strutturali (incluso
   `apps/chronon3d_cli/CMakeLists.txt` con restructuring major verso
   `chronon3d_cli_core`/`_render`/`_video`/`_telemetry`/`_dev`/`_bench`
   library splitting, post-Phase 1d aggiunte di
   `chronon schema`/`example-props`/`validate`/`resolve` source entries).
3. Third-attempt resolution path = DROP per AGENTS.md §honesty-discipline
   (evitare rot-pattern da merge misrepresentation: il DROP è un evento
   di stato osservabile che merita traccia canonica).
4. macchina-verifica deferred-WBH per
   `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` env-block precedent su
   questo VPS — verified safe via zero source-delta pre-commit (clean
   tree restored a origin/main tip, no SDK/CLI/timeline compilation
   regression risk).

Subject envelope `chore(stash): drop Phase 1c/1d (cat-5 chaser)` = 47
chars, ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit.
Forward-points documentedi atomicamente al chaser-chore closure per
AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point
tickets` rule (adattato: drop-with-forward-points chaser-chore).)
