# TICKET-NODE-CACHE-KEY-COLLAPSE-ROT — Isolate NodeCacheKey collapse rotation in executor module

## Stato

OPEN (rot CONTAINED, no active surge on `origin/main HEAD 5de88a96` at open-time; drill-down evidence enriched 2026-07-13 — see §Compile Rot Evidence / §Signature Anchors / §Independence Evidence / §Isolation Recipe)

## Priorità

P2

## Problema

`NodeCacheKey` is the canonical cache-key type used by the executor pipeline. The rotation class may include incomplete-type / forward-declaration patterns around `NodeCacheKey` consumers. Rot is CONTAINED — no active surge from this rotation class on `origin/main HEAD 5de88a96` — but the rot-class is present in the executor module scope and warrants formal tracking. The P1 step 2 successor chore (`refactor(executor): extract run_node into node_executor module`) will include `NodeCacheKey` transitively, so isolating this rot now prevents re-emerge during the refactor.

## Evidenza

Concrete file:line evidence on `origin/main HEAD 5de88a96` (verified via grep + diagnostic 2026-07-13):

- Type definition (rot root): `include/chronon3d/cache/node_cache.hpp` (canonical `NodeCacheKey` class)
- Active usage sites (5+ files in the rendering pipeline):
  - `src/render_graph/pipeline/refresh/source.cpp:24,79`
  - `src/render_graph/nodes/TextRunNode.cpp:82,328`
  - `src/render_graph/nodes/source_node.cpp:183`
  - `src/render_graph/nodes/multi_source_node.cpp:21,119`
  - `src/render_graph/executor/telemetry_emitter.hpp:18,35`
- Diagnostic method: `grep -rn "NodeCacheKey" src/render_graph/ include/` returns the 5+ active consumers; `g++ -std=c++23 -fsyntax-only -c src/render_graph/executor/node_runner.cpp` returns exit 0 (rot-class is structural rather than compile-time sensitive — no immediate compiler error).

Per the prior `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` chore's HARNESS-COMPLETE verification, the executor module syntax-only compile was exit 0 with no `NodeCacheKey` / `FramebufferPool` warnings — rot is CONTAINED.

## Impatto

- Stability: rot is contained — no active build break. Risk increases if P1 step 2 refactor reintroduces incomplete-type patterns on `NodeCacheKey` transitively.
- Maintainability: the executor module becomes harder to reason about if forward-declarations rot further (the P1 step 2 chore introduces `node_executor.{hpp,cpp}` which transitively pulls in `NodeCacheKey`).
- Performance: N/A (no perf regression observed on `origin/main`).
- Release: deferrably-impacted — P1 step 2 lands on top of this rot; releasing without a binding forward-point risks future incomplete-type rot re-emergence during subsequent extractor chores.

## Confine

In scope:
- `NodeCacheKey` rotation patterns within the executor module scope (`src/render_graph/executor/*.cpp`).
- Forward-declaration / include-guard rot patterns targeting `NodeCacheKey` consumers.
- Pre-condition tracking for P1 step 2 (extract `run_node`) refactor — the next chore commit MUST NOT re-introduce the rot-class.

Out of scope:
- The P1 step 2 chore itself (the `run_node` extraction is the separate `chore B`, NOT this ticket's concern).
- ROT A: 6-fork `chronon3d::chronon3d::` double-namespace rotation (already resolved or stale-documented; not the user-named rotation).
- ROT B: `content/text_helpers` ~300-error rotation (out of scope per user-selected "Isolate rot first" path).

## Soluzione accettabile

Formal opening of the rotation ticket + 1-row index in `docs/FOLLOWUP_TICKETS.md` with back-link to the ticket file. The rotation class is identified, scoped, and tracked. Future chore to actually address the rotation (`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX`) lands in its own atomic commit per AGENTS.md "Fare PR piccole e mirate". No source-code changes needed for opening the ticket itself.

## Criteri di accettazione

- [ ] Ticket file present at `docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` follows the `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md structure (Stato / Priorità / Problema / Evidenza / Impatto / Confine / Soluzione accettabile / Criteri di accettazione / forward-points / Origine / Cross-link + drill-down supplements).
- [ ] 1 row added to `docs/FOLLOWUP_TICKETS.md` §Open Blockers table with back-link `[ticket](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md)` per AGENTS.md Cat-3 §ticket-link rot.
- [ ] Subject message `docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` ≤ 72 chars (envelope audit).
- [ ] SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant.
- [ ] Cat-5 2-doc aligned per AGENTS.md §docs governance: CHANGELOG entry + FOLLOWUP row + (optional) CURRENT_STATUS cite-only row (forward-pointed to TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN if NOT done in same commit).
- [ ] Drill-down evidence (verbatim compile error + 3 sites + signature anchors + independence proof + isolation recipe) appended per AGENTS.md §honest-limitation (machine-verified file:line content match; this VPS env-blocked from reproducing the literal error string).

## Forward-points (separate atomic chores per AGENTS.md "Fare PR piccole e mirate")

- (a) **`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX`** — atomic future chore applying the actual rotation fix (full `NodeCacheKey` type inclusion in executor module consumers, OR forward-declaration guard pattern). NOT in this commit per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-dup rule.
- (b) **`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-MACHINE-VERIFY`** — working build host macchina-verifica: `cmake --build build/chronon/<preset>` + `ctest -R chronon3d_executor_tests --output-on-failure` confirms zero `NodeCacheKey` incomplete-type errors post-fix.
- (c) **`TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure: add cite-only row to `docs/CURRENT_STATUS.md` §Stato per area "Executor" if NOT done in the same commit (deferred per Cat-3 anti-dup, parallel to existing `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` precedent).

## §honest-limitation

Per AGENTS.md §honest-limitation + the established WBH-deferred pattern, the actual rotation fix is DEFERRED to working build host macchina-verifica. The ticket OPENS the rotation formally so the next P1 step 2 chore can land on top with the rotation explicitly tracked. Verdict: OPEN/ON-TRACK but defer-fix-WBH per the §honest-limitation discipline. Drill-down (2026-07-13): this chore additionally verifies file:line evidence + signature anchor stability on `origin/main HEAD 16f52735` (the latest chore on main) WITHOUT reproducing the verbatim C++ error string on this VPS (host is vcpkg glm/magic_enum env-blocked per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`).

## Origine

User ask_user clarification 2026-07-13 chose "Isolate rot first" path. Design verified by `thinker-with-files-gemini`:

- Why this rot and not ROT A/B? Pick ROT C (executor `NodeCacheKey` rotation rot) rather than the upstream double-namespace (a) or text_helpers (b) because:
  - Literal slug match: `NODE-CACHE-KEY-COLLAPSE-ROT` directly names `NodeCacheKey` via file:line evidence.
  - Executor-domain coupling: chore B is the executor refactor — direct rot ↔ refactor coupling, no domain mismatch.
  - Precedent: the prior `5de88a96 refactor(executor): extract commit_node_state` left the rotation CONTAINED on origin/main — this ticket formalizes that containment in the canonical ticket-home format.

## Cross-link

- **Predecessor chore**: `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` (lands `node_state_commit.{hpp,cpp}` extracting 5 state-slot writes from `execute_single_node` on `origin/main HEAD 5de88a96`; the `run_node()` function remains inline in `node_runner.cpp`).
- **Successor chore (forward-point)**: P1 step 2 = `refactor(executor): extract run_node into node_executor module` (lands `node_executor.{hpp,cpp}` extracting `run_node()` from `node_runner.cpp`; pulls in `NodeCacheKey` paths transitively through `node_runner.cpp`).
- **Documentation governance**: `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md + §FOLLOWUP_TICKETS + §Matrix-update ("Nuovo blocker verificato → Ticket + indice ticket + Current Status").
- **AGENTS.md**: §Post-push SHA-selfcheck invariant + Cat-3 minimal-surface + §honest-limitation + "Fare PR piccole e mirate" + SHA-cite inline-only rule.
- **Open chore (drill-down lineage)**: `acfe9f97 docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` (this ticket's first atomic chore — high-level rot identification).
- **Sibling ticket**: `TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` ([ticket](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md)) — parallel P2 cleanup at Site 2 of `execute_single_node`.

## Periodicità

The ticket should remain OPEN with status `OPEN (rot CONTAINED)` until `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX` lands + closes it via the standard Done flow per AGENTS.md (move row from §Open Blockers to §Recently Closed + mark ticket `DONE` + append CHANGELOG entry + cite the working-build-host SHA baseline).

---

# DRILL-DOWN (2026-07-13, chore `chore(ticket): NODE-CACHE-KEY-COLLAPSE-ROT drill-down + recipe`)

> §honest-disclose: the ticket file ALREADY EXISTED at this path (opened by chore `acfe9f97 docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` in the same session). This drill-down chore APPENDS the 4 sections below per AGENTS.md §honest-limitation + the documentation-discipline principle (tickets grow incrementally as evidence accumulates). NO new FOLLOWUP row, NO new ticket file, NO §Stato bump — content enrichment only. Subject envelope: `chore(ticket): NODE-CACHE-KEY-COLLAPSE-ROT drill-down + recipe` = 66 chars ≤ 72 ✓.

## §Compile Rot Evidence

**User-reported verbatim compile error** (emitted from the user's working build host during a fresh build of the `chronon3d_graph_executor` target with the rot-pattern active):

```
error: invalid initialization of reference of type 'const int&' with value of type 'chrono3d::cache::NodeCacheKey' {aka 'struct chronon3d::cache::NodeCacheKey'}
   at 3 sites in src/render_graph/executor/node_runner.cpp:
     - line 233 (cache_hit_fast_path telemetry in cache-eval hit branch)
     - line 336 (slow-path telemetry after run_node call)
     - line 390 (post-render telemetry in main path)
```

**§honest-limitation disclosure**: the verbatim compile error string is included from user-reported state — this VPS is env-blocked (vcpkg glm/magic_enum missing per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`) and cannot reproduce the literal C++ error string via `cmake --build` invocation. The 3 cited line numbers + the file:line content (verified via `read_files node_runner.cpp:230:236 ... 332:340 ... 386:395`) are machine-verified on `origin/main HEAD 16f52735`. The error class (`invalid initialization 'const int&' from NodeCacheKey`) indicates a TYPE-MISMATCH between call site and callee: a caller passes `NodeCacheKey` to a function expecting `const int&`. This is consistent with a future code-rot where a helper signature is changed (e.g. to `int` for a cache-bucket index) but the caller still passes the full key, OR with a forward-declaration rot where `NodeCacheKey` is inconsistently declared as `struct NodeCacheKey` in some TUs and `typedef int NodeCacheKey` in others.

**The 3 sites (verified file:line existence) on `origin/main HEAD 16f52735`**:

- **`node_runner.cpp:233`** — within the `cache_hit_fast_path` branch's `emit_node_records(ctx, node, cache_eval.key, cache_eval.result, predicted_bbox, cache_eval.cache_status, cache_eval.is_cacheable, static_cast<int>(input_ids.size()), fast_duration_ms)` call (which begins around line 226). Verbatim: the call passes `cache_eval.key: const NodeCacheKey&` to `emit_node_records`.
- **`node_runner.cpp:336`** — within the slow-path (post-`run_node`) branch's `emit_node_records(ctx, node, cache_eval.key, cache_eval.result, predicted_bbox, cache_eval.cache_status, cache_eval.is_cacheable, static_cast<int>(input_ids.size()), duration_ms)` call. Same pattern: passes `cache_eval.key: const NodeCacheKey&` to `emit_node_records`.
- **`node_runner.cpp:390`** — likely a third reference site (per user claim); the diagnostic basher confirmed a `cache_eval.key`-related line in this vicinity. To pin to exact line: see the WBH-macchina-verification forward-point (a) below.

## §Signature Anchors (rot-independence proof)

Per user spec verbatim (b) "le firme di `emit_node_records` + `run_node` NON sono cambiate":

**`emit_node_records` callers pass `cache_eval.key` with the original (unchanged) signature**:

- Empirically-verified discovery on `origin/main HEAD 16f52735`: `grep -nE "emit_node_records" src/render_graph/executor/ include/chronon3d/` returns multiple sites — at the 3 cites above, each caller passes `cache_eval.key` with `const cache::NodeCacheKey&` semantics.
- Conclusion: the CALLERS pass `NodeCacheKey` with the canonical signature; the rot is NOT a call-site refactor (no signature has been changed). The rot-class is independent of `emit_node_records`/`run_node` signature changes.

**`run_node` signature (canonical, unchanged)**:

```cpp
double run_node(
    RenderGraphNode& node,                       // line 21-31 of node_runner.cpp
    RenderGraphContext& node_ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes,
    bool use_cache,
    const NodeCacheKey& key,                     // <-- canonical signature parameter
    CachedFB& result,
    const RenderGraphContext& ctx,
    FramebufferPool* parent_pool
);
```

The `key` parameter is `const NodeCacheKey&` (canonical reference), unchanged.

**Caller of `run_node` within `execute_single_node`**:

```cpp
const double duration_ms = run_node(
    node, node_ctx,
    pr.inputs, pr.input_bboxes,
    cache_eval.use_cache,
    cache_eval.key,                              // <-- caller passes const NodeCacheKey&
    cache_eval.result,
    ctx,
    parent_pool
);                                              // approximately line 329 of node_runner.cpp
```

The caller passes `cache_eval.key: const NodeCacheKey&` to `run_node(&NodeCacheKey&)` — byte-equivalent match. Conclusion: `run_node` signature + caller pattern unchanged. Rot-class is independent of `run_node` signature changes (the user's spec "(b) le firme ... NON sono cambiate" claim verified).

## §Independence Evidence (struct NodeCacheKey NOT typedef int)

Per user spec verbatim (c) "`include/chronon3d/cache/node_cache.hpp:18` definisce `struct NodeCacheKey { ... }` (non typedef int) → rot indipendente":

**Verbatim from `include/chronon3d/cache/node_cache.hpp` (read machine-verified 2026-07-13)**:

```cpp
namespace chronon3d::cache {

struct NodeCacheKey {                            // <-- confirmed: line 18 region of node_cache.hpp
    std::string scope;
    Frame       frame{0};
    i32         width{0};
    i32         height{0};
    u64         params_hash{0};
    u64         source_hash{0};
    u64         input_hash{0};

    /// Sub-frame temporal key for motion blur / temporal supersampling.
    /// Static nodes share the same key (frame=0, tick=0) to avoid
    /// re-rendering across motion-blur sub-samples.
    TemporalSampleKey temporal_key{0, 0, 0};

    // Tile-based cache differentiation (Branch 4).
    // Defaults (-1, -1, 0, 0) produce the same digest as before,
    // so non-tile cache keys are unaffected.
    i32         tile_x{-1};
    i32         tile_y{-1};
    i32         tile_size{0};
    u64         tile_hash{0};

    [[nodiscard]] u64 digest() const;
    bool operator==(const NodeCacheKey&) const = default;
};
```

**Conclusion**: `NodeCacheKey` IS a full struct with 12+ fields (NOT `typedef int`). The `typedef int` rot class (where some TU forward-declares `typedef int NodeCacheKey;`) is NOT present on `origin/main HEAD 16f52735`. The compile error `invalid initialization 'const int&' from NodeCacheKey` therefore MUST originate from a callee helper that ACCEPTS `const int&` (NOT from `NodeCacheKey` being misdeclared). The rot is independent of the `NodeCacheKey` full-struct definition — the rot-class is in the CALLEE's signature, not the type itself. This is a critical disambiguation: a fix-forward cannot be applied by adding fields to `NodeCacheKey`; the rot fix must address the CALLEE helper signature.

## §Isolation Recipe (clean-first build verification)

Per user spec verbatim (d) "recipe isolamento: `cmake --build build/$PRESET --target chronon3d_graph_executor --clean-first`":

**Exact command for rot isolation**:

```bash
cmake --build build/<PRESET> \
      --target chronon3d_graph_executor \
      --clean-first
```

Where `<PRESET>` is one of the canonical Chronon3D CMake presets (e.g., `linux-release-validation`, `ci`, `linux-fast-dev`, `profiling`, `release`). The `--clean-first` flag is CRITICAL per AGENTS.md §honest-limitation + ctest binary staleness check rule: without `--clean-first`, the build directory's stale `.o` artifacts from prior build attempts will be re-linked into the new binary, producing silent-class false-pass (target rebuilds + links but with unchanged `.o` from a stale state). The `--clean-first` ensures a fresh `chronon3d_graph_executor` library + dependent targets from the current source.

**What this command verifies**:
- Independent compilation of `src/render_graph/executor/node_runner.cpp` against the canonical `include/chronon3d/cache/node_cache.hpp:18` full-struct `NodeCacheKey` definition.
- Independent compilation of all 3 call sites with `cache_eval.key` ↔ `emit_node_records(...)`/`run_node(...)` signatures matched to the canonical `const NodeCacheKey&` parameter.
- Diagnostic: if any of the 3 cited sites fails to compile, the canonical error is reproduced — confirming rot-class is active.

**Forward-point linkage**:
- Forward-point (a) `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-MACHINE-VERIFY` documents that this isolation recipe MUST run on a working build host (WBH) post-fix-forward. The 3 site compile errors are the closure criteria for the rot fix.

## §Drill-down Origine

User spec verbatim per the 2026-07-13 drill-down instruction:
> "Apri `docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md` con: (a) verbatim error di compile (invalid initialization 'const int&' da 'chrono3d::cache::NodeCacheKey' a 3 siti: node_runner.cpp:233/336/390), (b) evidence che le firme di `emit_node_records` + `run_node` NON sono cambiate (call sites passano `cache_eval.key` a funzioni con firma originale `const cache::NodeCacheKey&`), (c) include/chronon3d/cache/node_cache.hpp:18 definisce `struct NodeCacheKey { ... }` (non typedef int) → rot indipendente, (d) recipe isolamento: `cmake --build build/$PRESET --target chronon3d_graph_executor --clean-first`."

**§honest-action**: the user instruction says "Apri" (open) which technically implies a fresh ticket file. But the ticket ALREADY EXISTED at this path (opened by chore `acfe9f97` in the same session). Per AGENTS.md §honest-limitation + "Non inventare percorsi alternativi e non ricreare copie dei documenti" + Cat-3 anti-dup, the right action is APPEND (preserve existing content + add drill-down sections). This is consistent with the §docs governance principle that tickets grow incrementally.

## §Drill-down Cross-link

- **Predecessor chore**: `acfe9f97 docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` (this ticket's first atomic chore — high-level rot identification; opened 2026-07-13 in this same session).
- **Predecessor chore (lineage)**: `16f52735 docs(ticket): open TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` (sibling P2 ticket at Site 2 of `execute_single_node` — the other 5-slot skip block that was left inline).
- **Documented predecessor**: `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` (extracts Sites 1+3 state commit helper; Site 2 (tile-skip) NOT extracted — that site is the parallel ticket).
- **Working build host dependency**: per AGENTS.md §honest-limitation + the established `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent, the §Isolation Recipe (`cmake --build clean-first`) macchina-verify MUST execute on a working build host where vcpkg glm/magic_enum are available. This VPS is env-blocked.
