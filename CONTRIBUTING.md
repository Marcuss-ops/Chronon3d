# Contributing to Chronon3d

Thanks for your interest! This is a code-first, headless, CPU-only motion graphics engine in C++20.

## Getting Started

### Prerequisites

```bash
# Install build tools
sudo apt-get install -y build-essential cmake ninja-build

# Install ccache for faster rebuilds (recommended — auto-bootstrapped by build-fast.sh)
sudo apt-get install -y ccache

# ffmpeg for video export
sudo apt-get install -y ffmpeg
```

### Build

1. Fork and clone the repo.
2. Set vcpkg root (the build script uses `$HOME/vcpkg` by default, or set your own):
   ```bash
   export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
   ```
   Or let `tools/chronon-linux.sh` handle it automatically.

3. **Daily workflow (sub‑30 s incremental)** — preferred for development:
   ```bash
   # First run: configures, bootstraps ccache (~/.ccache/ccache.conf), populates tmpfs build dir
   ./build-fast.sh

   # Subsequent runs: incremental rebuild lands ~13–17 s on warm ccache + tmpfs
   ./build-fast.sh
   ./build-fast.sh test 'ExtensionLoader*'
   ```
   See [`docs/FAST_BUILD.md`](docs/FAST_BUILD.md) for the full cheatsheet.

4. **Release build** — for publishing (no sloppiness, no tmpfs):
   ```bash
   cmake --preset linux-release
   cmake --build build/chronon/linux-release -j$(nproc)
   ctest --preset linux-test
   ```

## Development Workflow

1. **Branch from `main`** — use a descriptive branch name (e.g. `fix/path-registration`, `feat/disk-cache`)
2. **Keep commits focused** — one logical change per commit
3. **Write tests** — new features should include doctest cases; benchmarks go in `tests/bench/`
4. **Run tests before pushing** — `ctest --preset linux-test`
5. **Open a PR** — describe what changed and why

## Code Style

- C++20 with strict compiler flags. The codebase uses exceptions (`std::runtime_error`, `std::invalid_argument`, `std::out_of_range`) for error reporting in registries, asset loading, graph compilation, and other runtime validation paths.
- 4-space indentation, no tabs.
- Follow the naming conventions you see in the codebase:
  - `snake_case` for functions and variables
  - `PascalCase` for types and classes
  - `SCREAMING_SNAKE_CASE` for constants and macros
- Use `//` comments — no `/* */` except for long doc blocks.
- Minimize includes — prefer forward declarations where possible.
- Include-discipline for public-header refactors — when you change a public header (removing, renaming, or splitting a transitive include; or name-qualifying an unqualified symbol like `size_t`), update its known consumers in the same PR. Enumerate them with `git grep '<header_path>' -- include/ src/ tests/`. Do not ship a public-header refactor alone; the consumer set is otherwise uncompilable until a follow-up pass. See `docs/FOLLOWUP_TICKETS.md` → TICKET-005 Gap B for the prior incident (commit `856ff957`) that produced this rule.
- No raw `new`/`delete` — use the framebuffer pool or arena allocators.

## Project Structure

```
include/chronon3d/   — Public headers
src/                 — Implementation files (mirrors include/ layout)
apps/                — CLI tools
tests/               — Doctest-based tests
docs/                — Roadmap and architecture docs
content/             — Composition definitions
```

## Pull Request Checklist

- [ ] Compiles without warnings
- [ ] All existing tests pass
- [ ] New tests added for changed/added code
- [ ] CHANGELOG note added (if user-facing change)
- [ ] Last 10 commits have subjects ≤ 72 chars (enforced by `wrap_push.sh`)

## Commit Subject Policy

The pre-push CI gate [`tools/check_commit_subject_length.sh`](tools/check_commit_subject_length.sh)
enforces a strict **72-character maximum** on commit subjects.
The gate is *reachability-based*: it enumerates commits in
`git log origin/main..HEAD` (i.e., commits you are about to push),
excluding any commit already on `origin/main`. Pre-existing commits
on `origin/main` are implicitly exempt via ancestor reachability and
need not be retrofit with shorter subjects.

For the canonical policy decision, see
[`docs/adr/ADR-021-commit-subject-length-policy.md`](docs/adr/ADR-021-commit-subject-length-policy.md).
This section is the contributor-facing summary; ADR-021 is the source of truth.

### Rules

- **Limit**: Every commit subject must be ≤ 72 characters. UTF-8
  multi-byte characters (em-dash, accented letters) count once each
  via `awk length()`, so visual length matches the measurement.
  The per-character limit is overridable for tooling via the env var
  `SUBJECT_LENGTH_LIMIT`.
- **Scope (reachability-based)**: The gate enumerates
  `git log origin/main..HEAD` — commits about to be pushed. Commits
  already on `origin/main` are exempt by virtue of being ancestors of
  the upstream HEAD; they are grandfathered implicitly. When
  `origin/main` is unreachable (e.g., fresh clone with no upstream,
  or `git remote remove origin`), the gate falls back to the last 10
  local commits and emits a deprecation notice to stderr.
- **Hard block**: The exit-on-violation gate is hard. There is no
  `--skip-gates` escape hatch. Violating subjects block the push.
- **Scope is invariant per ADR-021**: Reachability via `origin/main`
  is the project's canonical enforcement boundary. Changes to the
  enforcement semantics require an ADR (see ADR-021 §Decision).

### Pre-existing history (grandfathered)

`origin/main`'s full history contains **1,698 commits** with subjects
longer than 72 chars (audited 2026-07-12 against `origin/main` HEAD
using `git log --format='%h %s' origin/main | awk 'length > 72' | wc -l`).
These are an intentional pre-policy-era condition. The gate is
necessarily reachability-based on `origin/main` precisely so it does
NOT re-fail on them on every push. Wholesale retroactive rewrites
are explicitly out of scope per AGENTS.md "no cosmetic amend churn
unless enforceable in CI" — enforcing the gate retroactively across
full history would require rewriting ~50% of `origin/main`'s commits
and breaking every external clone. Treat these commits as read-only
historical artifacts. The full tally and rationale live in
[`docs/tickets/TICKET-124-commit-subject-historical-ledger.md`](docs/tickets/TICKET-124-commit-subject-historical-ledger.md).

### Practical guidance

- Keep subject lines scannable: aim for ~50 characters and never more
  than 72. Use the body for detail.
- Use the same `type(scope): summary` convention you see elsewhere in
  the log (e.g. `feat(text): …`, `fix(render_graph): …`, `docs(adr): …`).
- A commit message body is preserved and not length-checked; only the
  first line (subject) is in scope.

## Architecture

Before making changes to core engine files, read:

- [`docs/ARCHITECTURE_EVOLUTION_PLAN.md`](docs/ARCHITECTURE_EVOLUTION_PLAN.md) — the roadmap for modularizing the engine
- [`docs/CORE_OWNERSHIP.md`](docs/CORE_OWNERSHIP.md) — zone definitions, protected contracts, growth rules, V3 migration rules, agent rules, and PR checklist
- [`docs/V3_BLUEPRINT.md`](docs/V3_BLUEPRINT.md) — V3 tile-first architecture and deletion map
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — active roadmap and Lean Architecture Gates

### Zone System

The engine is divided into five zones (see `CORE_OWNERSHIP.md` for details):

| Zone | Purpose | Default for |
|---|---|---|
| **Core Zone** | Fundamental contracts and invariants | Graph executor, scene model, framebuffer, cache policy |
| **Feature Zone** | Effects, render nodes, presets, exporters, content | Most new work |
| **Integration Zone** | Extension points, registries, resolvers, samplers | Wiring between modules |
| **Diagnostics Zone** | Telemetry, profiling, benchmarks, debug overlay | Observability (feature-flagged) |
| **Experimental Zone** | V3 tile-first prototypes | V3 development (isolated) |

Key rule: **work in Feature Zone by default** (`content/`, `effects/`, `nodes/`, `assets/`, `video/`, `CLI/`). Touch core contracts only with explicit justification.

### Growth Constraints

Every new module must satisfy all items in the **growth budget** checklist (see `CORE_OWNERSHIP.md` §2.5):
- One responsibility, one CMake target, one existing registry/extension point
- Targeted tests, no heavy dependency without feature flag, no new global singleton
- No new cache if the common `LruCache` primitive can be used

### V3 Migration

Every new V3 component must declare its V2 replacement, temporary adapter, removal criterion, equivalence test, and removal deadline. No permanent V2/V3 duplication. See `CORE_OWNERSHIP.md` §2.2 and `V3_BLUEPRINT.md`.

## Questions?

Open an issue or start a discussion. We're friendly.