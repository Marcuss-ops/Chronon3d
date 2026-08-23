# TICKET-P2-27-CAMERA-ORBIT-RIG-WRAPPER-VERIFICATION — Vacuous-truth audit chaser-chore

| ID      | TICKET-P2-27-CAMERA-ORBIT-RIG-WRAPPER-VERIFICATION                                                |
|---------|---------------------------------------------------------------------------------------------------|
| Status  | **DONE** (2026-07-14, this session) — vacuous-truth state confirmed                                 |
| Scope   | 1 NEW ticket-home + 2 EDIT canonical docs (FOLLOWUP_TICKETS §Recently Closed + CHANGELOG cite-only); ZERO source modifications |
| Surface | `docs/tickets/TICKET-P2-27-CAMERA-ORBIT-RIG-WRAPPER-VERIFICATION.md` (NEW) + `docs/FOLLOWUP_TICKETS.md` (EDIT-prepend §Recently Closed row) + `docs/CHANGELOG.md` (EDIT-prepend cite-only entry) |

## Stato: DONE — vacuous-truth

## Problema (root cause of P2 #27)

User-spec verbatim (P2 action plan item #27): *"Ora il wrapper fa soltanto: CameraDescriptor d = camera_descriptor_from(rig); aggiungendo un ID differente. Migrare i chiamanti alla funzione canonica e cancellare il wrapper."*

The expected rot-class: a 1-line wrapper `camera_descriptor_from_orbit_rig(CameraRig& rig)` that returns `camera_descriptor_from(rig)` with a different ID. The fix would be to inline the wrapper at call sites + delete the wrapper.

## §HONEST-discipline vacuous-truth finding (this session, 2026-07-14)

**The wrapper is already REMOVED at HEAD.** Empirical rg-probe (basher + code-searcher this session, 3 distinct search variants):

1. `rg -n 'camera_descriptor_from_orbit_rig' src/ include/ apps/ content/ examples/ tests/` → **0 matches**
2. `rg -n 'orbit_rig|OrbitRig' . --type-add 'code:*.{cpp,hpp,h,c,cc,hh,py,cmake,md}' -t code -g '!build/' -g '!vcpkg-clone/'` → **0 matches** (broader sweep including all file types)
3. `rg -n 'camera_descriptor_from_orbit_rig' .` (entire repo, all file types) → **0 matches**

The wrapper was eliminated by the **prior `TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL` chore** (CLOSED 2026-07-14), which deleted the entire `legacy_camera_adapters.{hpp,cpp}` file pair (155 hpp + 125 cpp = 284 LoC atomic). That chore's §Forward-points explicitly noted: *"TICKET-CAMERA-ORBIT-RIG-FILE-PAIR-FULL-REMOVAL de-scoped this chore (the file pair is already gone; Adapter 1 had zero consumers through this orphan-bridge header)"* — confirming the orbit-rig wrapper was already removed as a side effect of the larger legacy-camera-adapter-pair deletion.

## Canonical state verified

The canonical function `camera_descriptor_from(rig)` is alive and well at:
- `include/chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp` (overloads for `CameraMotionParams`, `CameraRig`, `Camera`)
- The user-spec "migrate callers to canonical function" pre-condition is vacuously satisfied: zero consumers of the wrapper → zero migrations needed → canonical function is the only entry point.

## Criteri di accettazione

| # | Criterion | Status |
|---|-----------|--------|
| 1 | rg-probe confirms zero residual `camera_descriptor_from_orbit_rig` matches | PASS (0 matches across entire codebase) |
| 2 | rg-probe confirms zero residual `orbit_rig\|OrbitRig` patterns | PASS (0 matches across entire codebase) |
| 3 | Canonical `camera_descriptor_from` is the only entry point for orbit-rig adapter | PASS (per TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL forward-point note) |
| 4 | NO source modifications performed (vacuous-truth state) | PASS (chore is pure Cat-5 3-doc) |
| 5 | NO new public SDK API | PASS (vacuous-truth preserves existing surface) |
| 6 | NO new singleton/registry/resolver/cache | PASS (per AGENTS.md §regole deny-everywhere preserved) |
| 7 | NO `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS (Gate 5 Check 11 deny-everywhere preserved) |

## macchina-verifica (this session, 2026-07-14, VPS-only)

- `rg -n 'camera_descriptor_from_orbit_rig' src/ include/ apps/ content/ examples/ tests/` → **0 matches** (vacuous-truth confirmed)
- `rg -n 'orbit_rig|OrbitRig' . --type-add 'code:*.{cpp,hpp,h,c,cc,hh,py,cmake,md}' -t code -g '!build/' -g '!vcpkg-clone/'` → **0 matches** (broader sweep confirms no orphan)
- `ls include/chronon3d/scene/camera/camera_v1/legacy_camera_adapters.hpp` → **No such file** (deleted by prior chore)
- `ls src/scene/camera/camera_v1/legacy_camera_adapters.cpp` → **No such file** (deleted by prior chore)
- `rg -n 'camera_descriptor_from\b' include/chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp` → multiple matches (canonical function alive)
- CMake build + ctest-run macchina-verifica DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vcpkg glm/magic_enum env-block on this VPS)

## Cross-link (this session)

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §HONEST-discipline (vacuous-truth audit documented in §HONEST 1-liner + §Stato)
- AGENTS.md §Cat-3 minimal-surface (ticket-only chaser, zero source touched per audit)
- `docs/tickets/TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL.md` (the prior chore that eliminated the wrapper as a side effect of deleting the legacy_camera_adapters file pair; §Forward-points explicitly notes the orbit-rig wrapper was de-scoped AT-THIS-CHORE since zero consumers)
- `docs/FOLLOWUP_TICKETS.md` §Recently Closed `TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL` row (the parent closure reference)
- Vacuous-satisfaction pattern precedent siblings this session (9 prior): `TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL` + `TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL` + `TICKET-RENDERGRAPH-CACHE-PERSISTENT-DEAD-FLAG-SCOPE-REALIGNMENT` + `TICKET-TEXT-HELPERS-TYPEWRITER-P2-22-VERIFICATION` + `TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX` + `TICKET-CONTENT-TEXT-CAMERA-V1-ROT-PHASE-2-AUDIT` + `TICKET-PIPE-EXPORT-SESSION-P2-24-VERIFICATION` + `TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION` + `TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES` (the prior chore from previous turn, real work not vacuous)
- Subject envelope 47 chars OK ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| (none — vacuous-truth state) | DONE | The P2-#27 user-spec is vacuously satisfied because the wrapper was eliminated by the prior TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL chore. No new forward-point tickets are required. |
