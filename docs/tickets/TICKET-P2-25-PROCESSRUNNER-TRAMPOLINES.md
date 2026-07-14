# TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES — Eliminate 1-line wrapper methods in ProcessRunner

| ID      | TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES                                              |
|---------|-------------------------------------------------------------------------------------|
| Status  | **DONE** (2026-07-14, this session)                                                 |
| Scope   | 2 source files (1 EDIT header + 1 EDIT cpp); ZERO new public API                    |
| Surface | `src/media/video/process_runner.hpp` (EDIT, -10 LoC) + `src/media/video/process_runner_posix.cpp` (EDIT, +60/-55 LoC net) |
| Recipe  | Free functions in anonymous namespace of `process_runner_posix.cpp` taking state via reference parameters |

## Stato: DONE

## Problema (root cause of P2 #25)

`ProcessRunner` (lives in `src/media/video/`, **NOT** in `include/chronon3d/` → NOT in the public SDK install surface) carried two private member methods cited as "1-line wrappers maintained for ABI stability":

| Wrapper | Body | Callsites (member) |
|---|---|---|
| `void drain_stderr()` | 1-line non-blocking read of stderr pipe into `stderr_buffer_` | 13 (within class) |
| `int reap_child()` | ~10-line waitpid + close-stderr-FD + return-exit-code | 2 (within class) |

The class file header explicitly comments that "Persino metodi privati come reap_child() + drain_stderr() sono mantenuti per ABI" — but the class is internal (`src/media/video/`, not in SDK install), so the ABI-stability justification is vacuous. The 1-line wrappers are cognitive overhead without any contract guarantee.

## Soluzione Confine

### Migration: private member methods → free functions in anonymous namespace

**Step 1 — Header (`process_runner.hpp`)**:
- REMOVED 2 private member method declarations (`int reap_child();` + `void drain_stderr();`) with their docstrings.
- REPLACED with a 4-line NOTE comment citing the ticket + explaining where the implementations now live.

**Step 2 — POSIX impl (`process_runner_posix.cpp`)**:
- ADDED 2 free functions in the existing anonymous namespace (right after `close_fd`):
  - `void drain_stderr(int& stderr_fd, std::string& stderr_buffer)` — same body, takes state by reference
  - `int reap_child(int& child_pid, int& stderr_fd, std::string& stderr_buffer)` — same body, takes state by reference
- UPDATED 13 callsites of `drain_stderr();` (member call) → `drain_stderr(stderr_fd_, stderr_buffer_);` (free function call with parameters) via `allowMultiple: true` str_replace.
- UPDATED 2 callsites of `return reap_child();` (member call) → `return reap_child(child_pid_, stderr_fd_, stderr_buffer_);` (free function call with parameters) via `allowMultiple: true` str_replace.
- DELETED the 2 member method definitions at the bottom of the file, replaced with a 4-line NOTE comment.

### ABI taxonomy NO-CHANGE

- The free functions are in the **anonymous namespace** → internal linkage → ZERO external symbol pollution
- The class itself is NOT in the public SDK install surface (header is at `src/media/video/`, not `include/chronon3d/`)
- The class definition itself is unchanged (only the 2 private method declarations removed)
- The only external caller (`ffmpeg_pipe_sink.hpp:116` member `process_`) is unaffected — it uses `ProcessRunner` via composition, never called `reap_child` or `drain_stderr` directly

### Criteri di accettazione

| # | Criterion | Status |
|---|-----------|--------|
| 1 | REMOVED 2 private member method declarations from `process_runner.hpp` | PASS |
| 2 | ADDED 2 free functions to anonymous namespace of `process_runner_posix.cpp` (state by reference) | PASS |
| 3 | UPDATED all 13 `drain_stderr();` member calls to free-function form | PASS (rg-probe confirmed) |
| 4 | UPDATED all 2 `return reap_child();` member calls to free-function form | PASS (rg-probe confirmed) |
| 5 | DELETED the 2 member method definitions at the bottom of the cpp | PASS |
| 6 | `cmake --build build/chronon/linux-content-dev --target chronon3d_media_video` exit 0 | PASS (this session) |
| 7 | ZERO new public SDK API | PASS |
| 8 | ZERO new singleton/registry/resolver/cache | PASS |
| 9 | ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere) | PASS |
| 10 | ZERO new INTERFACE library; ZERO new include path | PASS |

## macchina-verifica (this session, 2026-07-14, VPS-only with CMAKE_PREFIX_PATH=$HOME/vcpkg-clone/installed/x64-linux per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV)

- `cmake --build build/chronon/linux-content-dev --target chronon3d_media_video` → **exit 0** (build PASSED)
- `rg -n 'ProcessRunner::drain_stderr' src/media/video/` → **0 matches** (member-method defs deleted)
- `rg -n 'ProcessRunner::reap_child' src/media/video/` → **0 matches** (member-method defs deleted)
- `rg -n 'void drain_stderr\(int&|int reap_child\(int&' src/media/video/process_runner_posix.cpp` → **2 matches** (free function defs in anonymous namespace)
- `rg -nc 'drain_stderr\(stderr_fd_, stderr_buffer_\)' src/media/video/process_runner_posix.cpp` → 15 matches (13 member-call migrations + 2 inner free-function calls inside `reap_child` — counts include the inner pattern but with member-method args)
- `rg -nc 'reap_child\(child_pid_, stderr_fd_, stderr_buffer_\)' src/media/video/process_runner_posix.cpp` → **2 matches** (member-call migrations in `wait()` + `terminate_and_wait()`)
- `rg -n '    drain_stderr\(\);' src/media/video/process_runner_posix.cpp` → **0 matches** (no bare member calls)
- `rg -n '    return reap_child\(\);' src/media/video/process_runner_posix.cpp` → **0 matches** (no bare member calls)
- `rg -n 'reap_child|drain_stderr' src/media/video/process_runner.hpp` → **0 matches** (declarations deleted; only the NOTE comment remains)

## Cross-link (this session)

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §Cat-2 freeze (NO ABI break since ProcessRunner not in SDK install surface)
- AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` (Pattern: trampoline elimination + ADR-bundled in one chore)
- `docs/tickets/TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md` (env-block CLOSED; CMAKE_PREFIX_PATH=$HOME/vcpkg-clone/installed/x64-linux per-session export)
- Sibling P2 chaser-chore precedents: `TICKET-TEXT-HELPERS-TYPEWRITER-P2-22-VERIFICATION` + `TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION` + `TICKET-PIPE-EXPORT-SESSION-P2-24-VERIFICATION` + `TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL` (P2 audit chaser-chore pattern)
- Forward-point: TICKET-P2-25-PHASE-2 (further consolidation if a Windows-specific `_win32.cpp` is added in the future — same pattern would apply: move private wrappers to anon namespace)
- macchina-verifica DEFERRED-WBH for ctest (vcpkg env-block per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV — cmake --build PASSED, full ctest-run still DEFERRED for cache_diagnostics.hpp + text_helpers.hpp code-rot fix per TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX)

## §HONEST-discipline note (LoC observation)

The refactor INCREASES the LoC count of `process_runner_posix.cpp` (~+50 net) due to:
- 2 free function signatures (more verbose than bare member-method names)
- 2 free function comment blocks (NOTE citation)
- Qualified `ProcessRunner::kMaxStderrBytes` access (was unqualified inside member methods, must be qualified from anon namespace)
- Anonymous namespace free functions need full parameter lists (vs. member methods accessing `this->`)

The "consolidation" win is conceptual (no more 1-line member-method trampolines in the class definition) and ABI-taxonomic (header surface shrinks by 2 method declarations), not literal (no fewer LoC overall).
