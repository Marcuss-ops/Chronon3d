# Follow-up Tickets — Open Blockers Index

> Snapshot: `main@f69045a6a` — 2026-06-29. Linux-only.
> File canonico: solo blocker aperti. La storia completa (49+ ticket chiusi) è migrata in
> `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`.
>
> Schema pregresso (`🔵 Planned` / `🟡 Partial` / `🟢 Done`) rimane canonico. La label
> `🔵 OPEN`introdotta per TICKET-051 è un sinonimo di `🔵 Planned` introdotto per aderenza letterale
> allo spec Agent 3 / Step 4.

## Open blockers

| ID | Area | Stato | File principali | Gate |
|---|---|---|---|---|
| TICKET-005 | post-cascade cleanup | 🟡 Partial | `—` | arch-completeness |
| TICKET-EXP2-G3 | ExpressiveV2 | 🔵 Planned | `—` | arch-boundary |
| TICKET-009 | experimental subtree | 🟡 In | `—` | — |
| TICKET-009 | experimental subtree | 🟡 Partial | `—` | arch-boundary |
| TICKET-010 | experimental subtree | 🔵 Planned | `—` | arch-boundary |
| TICKET-011 | Pre-existing mainline build rot | 🔵 Planned | `—` | arch-boundary |
| TICKET-012 | Arch violation: forbidden include in `include/chro | UNKNOWN | `—` | arch-boundary |
| TICKET-013 | Arch violation: sanction bypass in `render_session | UNKNOWN | `—` | arch-boundary |
| TICKET-014 | Test fail: `test_render_session_reset_and_isolatio | UNKNOWN | `—` | — |
| TICKET-015 | Test fail | UNKNOWN | `—` | — |
| TICKET-016 | Test fail | UNKNOWN | `—` | arch-boundary |
| TICKET-017 | Re-enable `TICKET-007.c` HierarchyResolver cycle d | 🔵 Planned | `—` | arch-boundary |
| TICKET-018 | Re-enable `TICKET-007.i` temporal frame-keyed jitt | 🔵 Planned | `—` | arch-boundary |
| TICKET-019 | Re-enable `TICKET-007.j/.k/.l` motion-blur torture | 🔵 Planned | `—` | arch-boundary |
| TICKET-021 | `PoseTracksSource` forza `Zoom` sopra FOV/Physical | 🔵 Planned | `—` | arch-boundary |
| TICKET-022 | Double look-at nel compiled path | 🟡 Code-fix | `—` | arch-boundary |
| TICKET-023 | `OrientAlongPath` stub | 🔵 Planned | `—` | arch-boundary |
| TICKET-024 | `OrbitMotion` usa world-Z invece del basis camera- | 🟡 Code-fix | `—` | — |
| TICKET-025 | `Trajectory` hardcoded `zoom=1000`/`fov=50` con pe | 🔵 Planned | `—` | arch-boundary |
| TICKET-026 | `MotionBlurSettings` conserva sia `MotionBlurMode` | 🟡 Code-fix | `—` | arch-boundary |
| TICKET-027 | `ShotTimeline` non propaga completamente diagnosti | 🔵 Planned | `—` | arch-boundary |
| TICKET-028 | Constraint stateful senza `CameraStateCheckpoint`/ | 🔵 Planned | `—` | arch-boundary |
| TICKET-033 | CameraDescriptor payload on `LayerKind::Camera` | M ERGED | `—` | — |
| TICKET-034 | `CameraDescriptor` as canonical default in composi | M ERGED-ON-BRANCH | `—` | arch-completeness |
| TICKET-035 | `Framebuffer` lacks `bytes | UNKNOWN | `—` | — |
| TICKET-036 | `chronon3d_camera_architecture_gate` P0 violation: | UNKNOWN | `—` | arch-boundary |
| TICKET-041 | Sync cmake/Chronon3DRegistry.cmake with src/ add_l | 🔵 Planned | `—` | — |
| TICKET-042 | Sync vcpkg.json deps with root CMakeLists.txt find | 🔵 Planned | `—` | — |
| TICKET-043 | Audit apps/* includes for internal chronon3d_sdk_i | 🔵 Planned | `—` | — |
| TICKET-044 | `arch_boundaries_selftest` hard-coded against pre- | 🔵 Planned | `—` | arch-boundary |
| TICKET-045 | `tools/check_gitignored_dirs.sh` + `tools/audit_so | 🔵 Planned | `—` | — |
| TICKET-046 | `tools/check_filename_drift.sh` reports 236 stale  | 🔵 Planned | `—` | arch-boundary |
| TICKET-047 | `tools/test_architectural.sh` Section X | 🔵 Planned | `—` | arch-completeness |
| TICKET-048 | `tools/install_consumer_test.sh` consumer can't se | 🔵 Planned | `—` | — |
| TICKET-051 | A4.3 per-preset diagnostic: surface the cinematic  | 🔵 OPEN | `—` | — |
| TICKET-054 | `build-fast.sh` target forwarding | 🔵 Planned | `—` | — |
| TICKET-055 | Per-composition keyframe PNG forensic glyph | 🔵 Planned | `—` | — |
| TICKET-056 | A4.4 + A4.5 memory envelope tuning | 🔵 Planned | `—` | — |
| TICKET-057 | Drop redundant `tests/helpers/render_fixtures.hpp` | 🔵 Planned | `—` | — |

## Cross-link history

Per la cronologia completa (root cause, resolution, cross-reference), vedi:

- `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md` — storico verbatim dei ticket 🟢 Done / 🟢 Resolved.
- `docs/CHANGELOG.md` — eventi sul main.
