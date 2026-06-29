# Follow-up Tickets — Open Blockers Index

> Snapshot: `main@111d9930` — 2026-06-29. Linux-only.
>
> File canonico per gli aperti. Massimo ~10 righe di tabella, ordinate per blocco della
> certificazione `Baseline verde: CERTIFICATA` (vedi `AGENTS.md` regola di lavoro #4). Tutti gli
> altri ticket aperti non-baseline-critical vivono in `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`
> (verbatim cronologia) e in git history pre-Step-6 (commit `6f3309e6`).
>
> Schema pregresso (`🔵 Planned` / `🟡 Partial` / `🟢 Done`) rimane canonico. Estensioni:
> `🟡 Code-fix` introdotto da Step 6 redux è sinonimo di `🟡 Partial` (tickets con codice già
> scritto ma non ancora macchina-verificato). `🔵 OPEN` introdotta per TICKET-051 è sinonimo
> di `🔵 Planned` (Step 4 spec Agent 3).

## Open blockers (top 10 — prioritari per `Baseline verde`)

| ID | Area | Stato | File principali | Gate |
|---|---|---|---|---|
| TICKET-051 | A4.3 per-preset diagnostic | 🔵 OPEN | `tests/text/test_text_preset_visual.cpp` | A4.3 visual |
| TICKET-048 | SDK install consumer rot | 🔵 Planned | `tools/install_consumer_test.sh` | install consumer (gate 9) |
| TICKET-036 | chronon3d_camera_architecture_gate P0 | 🔵 Planned | `tools/check_camera_architecture.sh` | arch-boundary (gate 5/6) |
| TICKET-044 | arch_boundaries_selftest hardcoded | 🔵 Planned | `tools/check_architecture_boundaries_selftest.sh` | arch-boundary (gate 5) |
| TICKET-046 | filename drift stale refs | 🔵 Planned | `tools/check_filename_drift.sh` | arch-boundary (gate 5) |
| TICKET-011 | pre-existing mainline build rot | 🔵 Planned | `src/` | arch-boundary (gate 1–8) |
| TICKET-005 | post-cascade cleanup | 🟡 Partial | `src/` + `include/` | arch-completeness (gate 5) |
| TICKET-022 | camera double look-at compiled path | 🟡 Code-fix | `include/camera/` | arch-boundary (gate 5/6) |
| TICKET-024 | camera OrbitMotion world-Z vs camera basis | 🟡 Code-fix | `include/camera/` | camera path |
| TICKET-026 | camera MotionBlurSettings Mode duality | 🟡 Code-fix | `include/camera/` | arch-boundary (gate 5) |

> **Ordinamento tabella top-10**: priorita `gate-impact desc` (regole AGENTS.md #4).
> Priority order = blocco diretto dei gate `Baseline verde: CERTIFICATA` (gate 1-9 RELEASE_GATE).
> Per espandere la copertura o riordinare, applicare stessa euristica sui ticket deferred listati sotto.

## Other open (deferred — see archive)

29 ticket aperti non-baseline-critical. Per `**Status**` reale + root cause + risoluzione,
vedi [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md)
o `git show 6f3309e6:docs/FOLLOWUP_TICKETS.md` (pre-Step-6 source-of-truth, 3834-line).

| ID | Area |
|---|---|
| TICKET-EXP2-G3 | ExpressiveV2 |
| TICKET-009 | experimental subtree |
| TICKET-010 | experimental subtree |
| TICKET-012 | Arch violation: forbidden include |
| TICKET-013 | Arch violation: sanction bypass |
| TICKET-014 | Test fail: render_session_reset |
| TICKET-015 | Test fail |
| TICKET-016 | Test fail |
| TICKET-017 | Re-enable TICKET-007.c cycle detection |
| TICKET-018 | Re-enable temporal frame-keyed jitter |
| TICKET-019 | Re-enable motion-blur torture |
| TICKET-021 | PoseTracksSource Zoom over FOV/Physical |
| TICKET-023 | OrientAlongPath stub |
| TICKET-025 | Trajectory hardcoded zoom/fov |
| TICKET-027 | ShotTimeline diagnostics |
| TICKET-028 | Constraint stateful without Checkpoint |
| TICKET-033 | CameraDescriptor payload on LayerKind::Camera |
| TICKET-034 | CameraDescriptor as canonical default |
| TICKET-035 | Framebuffer lacks bytes |
| TICKET-041 | Sync cmake/Chronon3DRegistry |
| TICKET-042 | Sync vcpkg.json deps with CMakeLists.txt |
| TICKET-043 | Audit apps/* includes |
| TICKET-045 | tools/check_gitignored_dirs + audit_software |
| TICKET-047 | tools/test_architectural.sh Section X |
| TICKET-054 | build-fast.sh target forwarding |
| TICKET-055 | Per-composition keyframe PNG forensic glyph |
| TICKET-056 | A4.4 + A4.5 memory envelope tuning |
| TICKET-057 | Drop tests/helpers/render_fixtures.hpp |

## Cross-link history

Per la cronologia completa (49+ ticket chiusi + 28 deferred a follow-up, tutti con `**Status**` reale
preservato verbatim dal pre-Step-6), vedi:

- `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md` — verbatim cronologia ticket chiusi + deferred.
- `git show 6f3309e6:docs/FOLLOWUP_TICKETS.md` (commit pre-Step-6 pinned) per la versione 3834-line originale.
- `docs/CHANGELOG.md` — eventi sul main.
