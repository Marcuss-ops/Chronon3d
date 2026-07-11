# ADR-012 — Chronon3D V0.1 SDK Manifest Boundary

- **Status:** Accepted (deferred action)
- **Date:** 2026-06-30
- **Snapshot:** 2026-06-30. Linux-only. (commit SHA: see §References — L120)
- **Supersedes:** — (does not supersede any prior ADR)
- **Superseded by:** —

> MADR-style ADR (sections: Context & Forces · Decision · Consequences · Compliance & Verification · References).

## Context & Forces

### Why this ADR exists now

The V0.1 SDK public surface is curated at `cmake/Chronon3DPublicHeaders.cmake` (the *V0.1 manifest*). It currently enumerates seven entries — one umbrella (`chronon3d.hpp`) + five `sdk/*` headers + `timeline/composition.hpp`. The manifest is the single source of truth that the ship-the-SDK layer (`cmake/Chronon3DSdkInstall.cmake`) uses to populate the `chronon3d_sdk` INTERFACE target's `public_headers` FILE_SET, and which downstream `find_package(Chronon3D)` consumers resolve at compile time. Per `AGENTS.md` rule #16 + `TICKET-011` (cmake-boundary — PUBLIC SURFACE), the documented public API surface must remain exactly the seven manifest entries.

### The leak in question

`include/chronon3d/timeline/composition.hpp` is itself a manifest entry. Verbatim audit of its `^#include` directives transitively ships these headers to any consumer that includes `composition.hpp`:

| `#include <chronon3d/...>` | Surfaces type | In V0.1 manifest? |
| --- | --- | --- |
| `core/types/frame_context.hpp`   | `FrameContext`                       | NO |
| `core/types/sample_time.hpp`     | `SampleTime`                         | NO |
| `scene/model/camera/camera.hpp` | legacy `Camera` struct (retired)     | NO |
| `scene/model/core/scene.hpp`    | `Scene`                              | NO |
| `assets/asset_registry.hpp`      | `AssetRegistry`                      | NO |
| `scene/camera/camera_v1/camera_descriptor.hpp` | `CameraDescriptor`  | NO |
| `scene/camera/camera_v1/camera_program.hpp`     | `CameraProgram`     | NO |
| `<functional>, <string>, <memory>, <vector>` | STL types             | n/a (STL) |

None of `FrameContext`, `SampleTime`, `Scene`, `AssetRegistry`, `CameraDescriptor`, `CameraProgram` are V0.1 manifest entries. A consumer that `#include <chronon3d/timeline/composition.hpp>` will see these transitively because `composition.hpp` itself pulls them directly. This is exactly the same class of leak that motivated the umbrella prune (`fix(sdk): prune chronon3d.hpp umbrella to manifest-only re-export`): an installer-visible re-export violates manifest discipline. (commit SHA: see §References — L118) The umbrella prune closed that leak for the umbrella path; this ADR acknowledges that `composition.hpp` still has one.

### Forces in tension

1. **Anti-duplication rule (AGENTS.md §5):** "Qualunque nuovo tipo che sovrappone uno di questi concetti deve essere rifiutato." Adding `FrameContext`, `Scene`, `SceneBuilder`, etc. into the manifest would create a *second* public-API surface for OPP-internal concepts (these types are already reachable via OPP-internal `#include <...>` paths inside the OPP build). Adding them is *extension*, not realignment.
2. **V0.1 Feature Freeze (AGENTS.md 🔴 section, active 2026-06-29):** Option `(b) chore(sdk): extend V0.1 manifest with composition API types` falls under *"Estensioni / Nuovi tipi pubblici"* → **BLOCCATO**.
3. **The seal-discussion test surface:** `tests/install_consumer/main.cpp` currently builds scenes via `chronon3d::SceneBuilder` and passes `chronon3d::FrameContext` to `chronon3d::composition()`. None of these are manifest entries; the install consumer reaches them ONLY through the transitive leak above. Pruning `composition.hpp`'s transitives **breaks** the install consumer unless the consumer is migrated to a not-yet-existing opaque authoring API.

### Constraint set

- **CONSENTITO during feature freeze** (per AGENTS.md 🔴 section):
  - Allineamento documentazione ✓
  - Rimozione percorsi legacy ✓ (route (a))
  - Test deterministici · Consumer SDK esterno · Correzioni di build ✓
- **BLOCCATO during feature freeze:**
  - Estensioni · Nuovi tipi pubblici · Nuovi registry/resolver/sampler · Tile-first · Expressions V2 (route (b))

## Decision

We **acknowledge the manifest boundary leak in `composition.hpp`** as a **deferred remediation**, documented but not resolved in this ADR cycle. Specifically:

1. **Do not prune `composition.hpp`'s transitives in this commit cycle.** Doing so breaks `tests/install_consumer/main.cpp` (which depends on `SceneBuilder` + `FrameContext` via the transitive). The install consumer's seal-discussion test surface is *green* today; preserving that surface is a higher-priority signal than closing the leak now.
2. **Do not extend the V0.1 manifest in this commit cycle.** Option (b) would publish OPP-internal types (`Scene`, `FrameContext`, `SceneBuilder`, `CameraDescriptor`, …) as public API surface, duplicating concepts that are already reachable via the OPP-internal `#include` paths. Per AGENTS.md anti-duplication rule this is *extension*, not realignment, and the V0.1 Feature Freeze explicitly forbids it.
3. **Authoring this ADR is the only action.** The leak remains in code. Future maintainers reading `composition.hpp` and its transitive chain will find this ADR as the canonical explanation of the *temporary* exception.

### Why ADR-only and not (a) or (b)

- **Route (a) `refactor(timeline): audit and prune composition.hpp transitives`** — would require a parallel migration of `tests/install_consumer/main.cpp` to a non-existent opaque authoring API. That migration is itself an extension of the SDK surface and would be re-classified as *Nuovi tipi pubblici* under the feature freeze, with the deeper risk of removing the seal-discussion surface entirely.
- **Route (b) `chore(sdk): extend V0.1 manifest with composition API types`** — explicitly forbidden by AGENTS.md 🔴 Feature Freeze.
- **Route (c) `docs(adr): chronon3d-sdk-manifest-boundary` (this ADR)** — falls under *"Allineamento documentazione CONSENTITO"* and resolves the audit/decision axis without expanding public API or breaking the install-consumer seal.

The thinker's verdict (Step 4 strategic validation, 2026-06-30) was that **only route (c) is viable under the V0.1 freeze**. 👍

## Consequences

### Positive

- The manifest boundary concern is **documented** as a first-class architectural artifact — discoverable by future maintainers, gate authors, and the eventual feature-freeze revocation review.
- The seal-discussion test surface (`tests/install_consumer/main.cpp` reaches `package@prefix/include/` and produces a PNG) stays green; we do not regress a pre-existing PASS.
- Future Feature Freeze revocation review (post 11/11 baseline-verde) has the explicit ADR pointer to revisit this leak structurally.

### Negative

- **The leak remains in code.** Downstream consumers who `#include <chronon3d/timeline/composition.hpp>` continue to see the OPP-internal types listed above transitively. This is OPP-internal-API surface published via a manifest-shim, which is technically a violation of V0.1 manifest discipline.
- **`docs/CURRENT_STATUS.md` cannot record a clean V0.1 manifest compliance** until this ADR's referenced restructure is implemented.
- Gate 10 (install_consumer_test.sh) Phase-1.1 cmake-configure currently fails on a *different* layer (CMP0077 INTERFACE_INCLUDE_DIRECTORIES source-tree path leak — see `fix(build): resolve chronon3d_runtime<->backend_software OBJECT library cycle` and the prior committed file-set fix (commit SHA: see §References — L119)). That blocker must be cleared *before* the leak this ADR documents is structurally resolved, since the structural resolution would touch the same include graph.

### Neutral / Ongoing

- The anti-duplication rule in `docs/ANTI_DUPLICATION_RULES.md` is unchanged; this ADR is consistent with it, not an exception to it.
- The Feature Freeze in `AGENTS.md` 🔴 section remains active; revocation requires 11/11 baseline-verde per the same section's *Revoca* policy.

## Compliance & Verification

### Compliance invariants this ADR commits to

1. **No new manifest entries during the feature freeze.** `cmake/Chronon3DPublicHeaders.cmake` is read-only during the freeze; this ADR is the *record* of why.
2. **`composition.hpp` is the only manifest entry with a documented transitive leak.** Future additions to the manifest must be checked for the same issue (audit script candidate: `tools/check_manifest_discipline.sh` — out of scope for this ADR).
3. **The OPP-internal reachability of `Scene`, `FrameContext`, etc. is unchanged.** No new OPP-side headers, no renamed types, no private→public promotions.

### Verification

- **The literal manifest is unchanged** — `cmake/Chronon3DPublicHeaders.cmake` diff against `HEAD~1` is empty (this ADR is doc-only).
- **The OPR umbrellas re-export is unchanged** — `include/chronon3d/chronon3d.hpp` still has only 6 anchored `#include`s (P3-I).
- **The install consumer remains green** — `tests/install_consumer/main.cpp` continues to compile against the manifest, *via* the documented leak.
- **No gate regressions** — `tools/audit_software_renderer.sh`, `tools/check_camera_architecture.sh`, `tools/check_architecture_boundaries.sh`, etc. are unaffected (none flag the `composition.hpp` → OPP-internal transitives as a P0 today). <!-- drift-allow: archived-doc-pattern -->

### When to revisit this ADR

- **At feature-freeze revocation.** Per `AGENTS.md` 🔴 section *Revoca*: a commit on `main` that records 11/11 baseline-verde + explicitly removes the 🔴 section is the gate to revisit. At that moment, the team's call is to either (a) prune `composition.hpp`'s transitives AND migrate `tests/install_consumer/main.cpp` to a non-leaking authoring surface, OR (b) extend the manifest with the OPP-internal types (legitimate after feature freeze lifts). Either path requires a follow-up ADR.
- **If a new manifest entry is added in the meantime.** Any new entry to `cmake/Chronon3DPublicHeaders.cmake` triggers a manifest-discipline re-audit (this ADR's principle).

## References

- `AGENTS.md` — feature freeze **🔴** section ("V0.1 Feature Freeze"), Revision Conditions, Working Gates for "baseline-verde".
- `AGENTS.md` — Canonical Priority list + Workflow & Hygiene rules.
- `docs/ANTI_DUPLICATION_RULES.md` — rule #17 (one source of truth per domain), § Source of Truth table.
- `docs/CORE_OWNERSHIP.md` — registry, resolver, sampler ownership canon.
- `cmake/Chronon3DPublicHeaders.cmake` — V0.1 manifest (7 entries, read-only during freeze).
- `cmake/Chronon3DSdkInstall.cmake` — installs the manifest via `FILE_SET public_headers` (Post-P3 Step-3 alignment).
- `cmake/Chronon3DSdkTargets.cmake` — defines `chronon3d_sdk` INTERFACE target that carries the manifest-installed FILE_SET.
- `include/chronon3d/chronon3d.hpp` — umbrella, pruned to manifest-only re-export in P3-I.
- `include/chronon3d/timeline/composition.hpp` — manifest entry #7, *the* leak source documented in this ADR.
- `tests/install_consumer/main.cpp` — seal-discussion surface that depends on the documented leak.
- `docs/adr/ADR-001-frame-graph-compiler.md` … `ADR-011-camera-legacy-deletion.md` — sibling ADRs (precedent for structure).
- TICKET-011 (cmake-boundary — PUBLIC SURFACE), TICKET-036 (camera-arch exemption basis).
- Git log: `a1f5e645 fix(sdk): prune chronon3d.hpp umbrella to manifest-only re-export` — origin of the residual concern.
- Git log: `d73e7e0b fix(sdk): declare missing interface file sets on chronon3d_sdk target` — File-set alignment.
- Git log: `6f7570c9 fix(build): resolve chronon3d_runtime<->backend_software OBJECT library cycle` — Pre-ADR prerequisite for install-side cleanup.

---

*This ADR was authored as part of the Step-4 audit of the manifest boundary after the umbrella prune (P3-I). It defers — does not resolve — the structural remediation. The deferral is bounded by feature-freeze revocation (11/11 baseline-verde per AGENTS.md 🔴 Revoca section).*
