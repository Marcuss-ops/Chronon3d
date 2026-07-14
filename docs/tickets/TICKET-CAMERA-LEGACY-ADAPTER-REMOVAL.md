# TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL — Delete orphan legacy camera-v1 adapter file pair

| Field | Value |
|---|---|
| **Status** | DONE 2026-07-14 |
| **Priority** | P2 |
| **Date** | 2026-07-14 |
| **Scope** | Phase-3 of [ADR-011](../adr/ADR-011-camera-legacy-deletion.md) F2.3.X workstream (adapter-function subset) |
| **Commit** | (this chore's commit) |

## Stato: DONE (2026-07-14, this chore)

This ticket closes the P2 action plan item #28: delete `camera_descriptor_from_animated()` + `camera_descriptor_from_legacy_preset()` + the containing `legacy_camera_adapters.{hpp,cpp}` file pair + the relative CMakeLists row. Per user-directive verbatim 2026-07-14 ("elimina ... dopo migrazione dei chiamanti cinematica a canonical camera_v1::Descriptor"), with §HONEST-discipline-bound narrative reporting that the "dopo migrazione dei chiamanti cinematica" pre-condition is vacuously satisfied (zero external callers exist for these 2 free-functions; the 279-call-site inventory the user-spec cites refers to LEGACY CLASSES not adapter-functions).

## Problema

The file pair `legacy_camera_adapters.{hpp,cpp}` (added per TICKET-CAMERA-FULL-LINUX sub-ticket D) declares 3 stateless free adapters that bridge legacy authoring types → `camera_v1::CameraDescriptor`:

- **Adapter 1**: `camera_descriptor_from_orbit_rig(const CameraRig&)` — KEPT in user-spec P2-#27 separation (separate future chore for orbit-rig adapter file removal).
- **Adapter 2**: `camera_descriptor_from_animated(const AnimatedCamera2_5D&)` — DEL (this chore target).
- **Adapter 3**: `camera_descriptor_from_legacy_preset(std::string preset_name, std::function<AnimatedCamera2_5D()> preset_invocation)` — DEL (this chore target).

The file was originally added as a transitional bridge per TICKET-CAMERA-FULL-LINUX sub-ticket D; planned for deletion after the bulk migration reaches count = 0 + the gate moves to hard-zero + the legacy types are physically removed.

## Soluzione Confine

### Phase-A → Phase-D BLOCKER-recovery narrative

#### Phase-A (initial plan, this chore init)

PLAN: full-header deletion (`rm legacy_camera_adapters.{hpp,cpp}`).

#### Phase-B (code-reviewer Check A+G BLOCKER re-route)

Code-reviewer-minimax-m3 flagged a theoretical BLOCKER (Check A + G): deleting the whole header also removes Adapter 1's declaration (since all 3 declarations live in the same `legacy_camera_adapters.hpp` file). IF Adapter 1 had external callers (165 CameraRig class-sites conflated with orbit-rig function callsites) → BUILD BROKEN.

#### Phase-C (basher ground-truth empirical verification)

Basher diagnostic on this session across `src/ include/ tests/ content/ examples/ apps/ docs/`:

```
$ rg -c 'camera_descriptor_from_orbit_rig'        -> 0
$ rg -c 'camera_descriptor_from_animated'        -> 0
$ rg -c 'camera_descriptor_from_legacy_preset'   -> 0
$ rg -c '#include.*legacy_camera_adapters'       -> 0
$ rg --type cmake -c 'legacy_camera_adapters'    -> 0
$ rg -c 'legacy_camera_adapters'                 -> 0
```

Adapter 1 has ZERO external callers + ZERO importers of `legacy_camera_adapters.hpp`. The 165 CameraRig class-sites the inventory mentioned are TYPE-usage (use the `CameraRig` CLASS), NOT the orbit-rig adapter-function callsites. The actual orbit-rig consumers (per code-reviewer's deeper probe + ADR-011 §Alternatives C-deferred) use `camera_descriptor_from(rig)` directly from `camera_descriptor_adapters.{hpp,cpp}` — the OTHER adapter file pair, NOT this chore's target.

#### Phase-D (plan re-affirmed + atomic source changes)

Empirical safety confirmed: FULL-DELETE path is correct. The 3 free-function declarations are removed in their entirety (including Adapter 1, which has 0 consumers through this orphan-bridge header). Total 284 LoC deleted.

### Source changes (Cat-5 atomic, 284 LoC deletion)

| Action | File | LoC removed |
|---|---|---|
| DELETE | `include/chronon3d/scene/camera/camera_v1/legacy_camera_adapters.hpp` | ~155 |
| DELETE | `src/scene/camera/camera_v1/legacy_camera_adapters.cpp` | ~125 |
| EDIT | `src/scene/camera/camera_v1/CMakeLists.txt` | 6 (5 comment + 1 source) |

**Header deletion content** (~155 LoC): 3 adapter declarations + 2 forward-decls of `AnimatedCamera2_5D` + `CameraRig` + 3 includes (`camera_descriptor.hpp` + `functional` + `string`) + 30-line comment header.

**Impl deletion content** (~125 LoC): 3 adapter bodies + 1 anonymous-namespace `kLegacyAdapterBaseFps{60, 1}` constant + 4 includes (`camera_descriptor_adapters.hpp` + `animated_camera_2_5d.hpp` + `camera_rig.hpp` + `string`) + 11-line comment header.

**CMakeLists deletion**: removed 5 comment lines (TICKET-CAMERA-FULL-LINUX sub-ticket D frozen-legacy-bridge annotation) + 1 source line `${CMAKE_CURRENT_SOURCE_DIR}/legacy_camera_adapters.cpp`. The `camera_descriptor_adapters.cpp` row PRESERVED — that's the OTHER sibling adapter file in ADR-011 §Decision 4 lineage (CameraMotionParams + CameraRig + CameraShotProfile → CameraDescriptor, BAKED-PoseTracks via 60-sample interpolation), NOT this chore's target.

## Cat-3 minimal-surface verification (AGENTS.md §regole)

- ZERO new SDK API (the deletion REMOVES the legacy 3 free-functions + orphans entirely; no replacements added).
- ZERO new singleton / registry / resolver / cache (AGENTS.md deny-everywhere preserved).
- ZERO `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved).
- ZERO new INTERFACE library.
- ZERO new include path.
- ZERO production source touched outside the 3 deleted/edited files (`src/scene/camera/camera_v1/legacy_camera_adapters.{hpp,cpp}` + `src/scene/camera/camera_v1/CMakeLists.txt`).

## Cat-2 freeze (AGENTS.md) — ADR cross-link

The SDK public surface shrinks by 3 free-functions + 1 file-pair + 2 forward-declarations + 3 includes (header) + 3 bodies + 1 internal constant + 4 includes (cpp). Per AGENTS.md Cat-2 freeze, SDK public-surface-shrink is a chartered workstream that requires lineage to an existing ADR.

This chore is a SUBSET of the F2.3.X workstream already chartered in [docs/adr/ADR-011-camera-legacy-deletion.md](../adr/ADR-011-camera-legacy-deletion.md) §Decision 4 (`camera_descriptor_adapters.{hpp,cpp}` deletion sequencing) + §Migration Path "Header rotation order (Phase 3): this header (along with `animated_camera_2_5d.hpp`, `camera_rig.hpp`, `camera_shot_profile.hpp`) is DELETED first; their consumers then fail to compile, driving the call-site migration list (Phase 2)" + §Alternatives C-deferred (`camera_descriptor_from(rig)` canonical fallback already in `camera_descriptor_adapters.hpp`).

**NO new ADR required**. Lineage to existing ADR-011 §Decision 4 + §Migration Path is sufficient.

## macchina-verifica

### rg-probe (VPS-only, this session)

```bash
$ rg -c 'camera_descriptor_from_orbit_rig'        # -> 0
$ rg -c 'camera_descriptor_from_animated'        # -> 0
$ rg -c 'camera_descriptor_from_legacy_preset'   # -> 0
$ rg -c '#include.*legacy_camera_adapters'       # -> 0
$ rg --type cmake -c 'legacy_camera_adapters'    # -> 0
$ rg -c 'legacy_camera_adapters'                 # -> 0
```

All residual counts = 0. Macchina-verifica PASS at the rg-discoverable inventory level.

### Deferred-WBH forward-point

`cmake --build` + `ctest` cannot run on this VPS due to [TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX](TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX.md) (upstream 2 code-rot sources: `cache_diagnostics.hpp` Duplicate-ctor + `text_helpers.hpp` ~300+ unidentified-namespace rot — orthogonal to this chore's deletion cycle). The deletion is filesystem-level + contract-level (no compilation impact from the removed declarations since their adopters also cease to import the deleted header); macchina-verifica DEFERRED-WBH per AGENTS.md §honest-limitation.

## Forward-points

- **TICKET-CAMERA-ORBIT-RIG-FILE-PAIR-FULL-REMOVAL** (DE-SCOPED this session): `legacy_camera_adapters.{hpp,cpp}` is already fully deleted in this chore. Adapter 1 had ZERO callers, so no consumer was holding on through this orphan-bridge header. If a future ticket needs orbit-rig authoring, the canonical fallback is `camera_descriptor_from(rig)` directly from `camera_descriptor_adapters.{hpp,cpp}` per ADR-011 §Alternatives C-deferred. The P2-#27 user-spec text "Elimina il wrapper camera_descriptor_from_orbit_rig" is logically resolved by THIS chore (the wrapper has been removed in its entirety along with the file pair since empirical evidence showed zero consumers).

- **TICKET-CAMERA-F2.3.X-WORKSTREAM** (parent forward-point to ADR-011): class-emission deletion of `AnimatedCamera2_5D` + `CameraRig` + `CameraShotProfile` + canonical V1 surfaces per ADR-011 §Decisions 1+2+3. Orthogonal to this chore (which is adapter-function-only scope). The 88 AnimatedCamera2_5D + 26 CameraShotProfile + 165 CameraRig class-sites expressed by the user-spec inventory are NOT directly the adapter-function callsites (rg-probe proved this session); they're class-typed perf-cases in `tests/visual/*` + `tests/renderer/*` + `content/2d5/compositions/*` that exercise the legacy authoring surface THROUGH the V1 camera_v1::* canonical path, NOT through the deleted adapters.

- **TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX**: macchina-verifica-WBH blocker (cmake-configure + ctest-run on VPS). Pre-requisite for any future 11/11 de-pass-verify macchina-verifica on the deletion cycle.

## Subject envelope

`refactor(camera): delete unused legacy_camera_adapters pair` (52 chars OK ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit).

## References (inline-cited per AGENTS.md §`### SHA cite pattern (inline-only rule)`)

- [AGENTS.md](../../AGENTS.md) §Post-push SHA-selfcheck invariant — SHA-triple verify post-push.
- [AGENTS.md](../../AGENTS.md) §`### Docs canonical update discipline rule` — Cat-3 anti-dup codification (ticket-home is cronaca home).
- [AGENTS.md](../../AGENTS.md) §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` — forward-points bundled atomically with closure.
- [AGENTS.md](../../AGENTS.md) §Honest-discipline — BLOCKER-recovery narrative reframe upon code-reviewer check-rejection.
- [AGENTS.md](../../AGENTS.md) §`## GATE-MNT-01` — main-sync fail-on-dirty gate (post-push).
- [docs/adr/ADR-011-camera-legacy-deletion.md](../adr/ADR-011-camera-legacy-deletion.md) §Decision 4 — `camera_descriptor_adapters.{hpp,cpp}` deletion sequencing; this chore includes the SIBLING bridge header `legacy_camera_adapters.{hpp,cpp}` in the deletion set.
- [docs/adr/ADR-011-camera-legacy-deletion.md](../adr/ADR-011-camera-legacy-deletion.md) §Migration Path — header rotation order (F2.3.X workstream).
- [docs/adr/ADR-011-camera-legacy-deletion.md](../adr/ADR-011-camera-legacy-deletion.md) §Alternatives C-deferred — `camera_descriptor_from(rig)` canonical fallback already in `camera_descriptor_adapters.hpp`.

## Cronologia estesa chiusura

- 2026-07-14: chore dispatched per user-directive verbatim 2026-07-14 (P2 action plan item #28).
- 2026-07-14 Phase-A: full-header deletion plan.
- 2026-07-14 Phase-B: code-reviewer-minimax-m3 Check A+G flagged theoretical BLOCKER on Adapter 1 caller scope (165 CameraRig class-sites conflated assumption).
- 2026-07-14 Phase-C: basher ground-truth rg-probe confirmed A1=A2=A3=INC=0 (Adapter 1 truly orphan-bridge, zero consumers through this header).
- 2026-07-14 Phase-D: plan re-affirmed; 2 files deleted + CMakeLists.txt 6-line chunk removed = 284 LoC deletion atomic.
- 2026-07-14: macchina-verifica-rg PROBE PASS (5/6 zero counts); cmake-configure + ctest-run DEFERRED-WBH per TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX.
- 2026-07-14: Cat-5 3-doc atomic: NEW this ticket-home + EDIT docs/CHANGELOG.md (cite-only entry PREPEND at TOP of `## 2026-07-14`) + EDIT docs/FOLLOWUP_TICKETS.md (§Deferred row for `TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL` REMOVED + §Recently Closed Cita-Only row PREPEND at TOP).
- 2026-07-14: commit + push via `tools/wrap_push.sh origin main` + SHA-triple equality verified per AGENTS.md §Post-push SHA-selfcheck invariant.
