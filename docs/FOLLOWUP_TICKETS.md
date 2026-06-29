# Follow-up Tickets — Open Blockers Index

> Snapshot: `main@88d2deec` — 2026-06-29. Linux-only.
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
| TICKET-036 | chronon3d_camera_architecture_gate P0 | 🔵 Planned | `tools/check_camera_architecture.sh` | arch-boundary (gate 5/6) |
| TICKET-044 | arch_boundaries_selftest hardcoded | 🔵 Planned | `tools/check_architecture_boundaries_selftest.sh` | arch-boundary (gate 5) |
| TICKET-046 | filename drift stale refs | 🔵 Planned | `tools/check_filename_drift.sh` | arch-boundary (gate 5) |
| TICKET-011 | pre-existing mainline build rot | 🔵 Planned | `src/` | arch-boundary (gate 1–8) |
| TICKET-005 | post-cascade cleanup | 🟡 Partial | `src/` + `include/` | arch-completeness (gate 5) |
| TICKET-022 | camera double look-at compiled path | 🟡 Code-fix | `include/camera/` | arch-boundary (gate 5/6) |
| TICKET-024 | camera OrbitMotion world-Z vs camera basis | 🟡 Code-fix | `include/camera/` | camera path |
| TICKET-026 | camera MotionBlurSettings Mode duality | 🟡 Code-fix | `include/camera/` | arch-boundary (gate 5) |
| TICKET-064 | §9 ExecutionScope — ScopeError/ScopeErrorCode structured error model (PR 6.8 prep) | 🟡 Code-fix | `include/chronon3d/core/execution/scope_error.hpp` | arch-boundary (gate 5) |

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

### Recently closed (atomic `main` commits, 2026-06-29)

| ID | Area | Status | Atomic commit on `main` |
|---|---|---|---|
| TICKET-052 | TEXT-UNM-01 — real TextUnitMap 8-level identity ladder | 🟢 Done (code on main) | `feat(text): TEXT-UNM-01 — real TextUnitMap with separated identity levels` (env-vars Agent3; **§12 closure doc lost during partner rebase**, see followup SECT-12-RESTORE; **code surface intact on main HEAD**, verify via `git grep kInvariant include/chronon3d/text/glyph_selector.hpp`) |
| TICKET-053 | TEXT-SEL-01 — Range / Wiggly / Expression canonical selectors | 🟢 Done | `feat(text): TEXT-SEL-01 — Range/Wiggly/Expression selectors as canonical types` (env-vars Agent3, atomic commit on `main`) |
| TICKET-058 | Step 9 ghost_sweep ROT-2 — `RenderRuntime::executor` chain on software_renderer consumers | 🟢 Done (atomic on `main`) | `e853e728 fix(backends/software): include render_runtime.hpp for software_renderer consumers (Step 9 ghost_sweep ROT-2)` — Fix-5 sibling rot-2 mirror del Fix-2 in `81cdc738`. Audit-comment block TICKET-038/TXT-00 cita rot-1 precedent. Forbidden-list: solo `include/chronon3d/backends/software/software_renderer.hpp`. |

See:
- `docs/MIGRATION_TEXT_SPEC.md` §13 for full closure documentation (3 canonical selector types stored under `std::variant<RangeSelector, WigglySelector, ExpressionSelector>`, `std::visit` dispatch via `evaluate_selector_v2` / `evaluate_selectors_v2`, `SafeAccessMap` with `textIndex` / `textTotal` / `frame` auto-binding, 13 new TEST_CASEs at `tests/text/test_glyph_selector_spec.cpp`).
- `docs/MIGRATION_TEXT_SPEC.md` §12 (closure for TEXT-UNM-01) is **missing on disk** — followup SECT-12-RESTORE in atomi backlog to restore closure doc after partner rebase truncated it.

| TICKET-059 | TEXT-PLY-01 — 14-feature ParagraphStyle extension + wiring | 🟢 Done | `feat(text): TEXT-PLY-01 — 14-feature ParagraphStyle extension + wiring` (env-vars Agent3, atomic on `main`; §14 closure doc in `docs/MIGRATION_TEXT_SPEC.md` §14; 14 TEST_CASEs in `tests/text/test_paragraph_layout_extras.cpp`) |

| TICKET-048 | GATE-MNT-01 — wire main-sync gate as pre-push wrapper | 🟢 Done | `ops(gates): GATE-MNT-01 — wire main-sync gate as pre-push wrapper` (env-vars Agent3, atomic on `main`; 4 tracked files: `tools/check_main_clean.sh` + `tools/wrap_push.sh` + `docs/FOLLOWUP_TICKETS.md` TICKET-048 row + `AGENTS.md` § GATE-MNT-01; local-only `.git/hooks/pre-push` installed per-repo for defence in depth) |

| TICKET-060 | SECT-12-RESTORE — MIGRATION_TEXT_SPEC §12 stub (chronology drift closure) | 🟢 Done | `docs: SECT-12-RESTORE — restore §12 stub pointing to §15` (env-vars Agent3, atomic on \`main\`; §12 stub inserted in \`docs/MIGRATION_TEXT_SPEC.md\` immediately before §13 marker; 2-file docs-only diff, no partner-owned \`src/registry/text/\` files staged) |

| TICKET-061 | TEXT-UNM-01 S15 deferred -- span_index_by_name resolves SemanticSpanRef.name | done | feat(text): close TEXT-UNM-01 S15 deferred -- span_index_by_name now resolves SemanticSpanRef.name (env-vars Agent3, atomic on main; include/chronon3d/text/text_unit_map.hpp +1 member; src/text/text_unit_map.cpp ctor init + impl replacement; tests/text/test_text_unit_map_8level.cpp +1 TEST_CASE (i); docs/MIGRATION_TEXT_SPEC.md S15.X; docs/FOLLOWUP_TICKETS.md TICKET-061 row) |

| TICKET-062 | PARTNER-OVERLAY-CLEANUP — gitignore src/registry/text/ (Refactor 1B) | 🟢 Done | `chore(gitignore): PARTNER-OVERLAY-CLEANUP — exclude Refactor 1B overlay (canonical registry is cmake/...)` (env-vars Agent3, atomic on `main`; 2-file diff [`.gitignore` + `docs/FOLLOWUP_TICKETS.md`]; gate-fix-only, NOT a gate-skip; partner commit `8d5ed596` never carried the 8 stale files; partner-overlay now invisible to `git status` → GATE-MNT-01 passes cleanly for all pushes; if partner rescinds, `git add -f src/registry/text/` recovers them).

| TICKET-063 | SCRATCH-HELPERS-CLEANUP — gitignore tools/_*.py (Agent3 scratch tooling) | 🟢 Done | `chore(gitignore): SCRATCH-HELPERS-CLEANUP — exclude tools/_*.py (Agent3 idempotency helpers)` (env-vars Agent3, atomic on \`main\`; SAME atomic-commit as TICKET-062 — amends commit 8bbd376b; 2-file config+docs diff [\`.gitignore\` extension + \`docs/FOLLOWUP_TICKETS.md\` TICKET-063]; gate-fix-only, NOT a gate-skip; underscore prefix signals \`ephemeral local helper, NOT durable project tooling\` consistent with existing .gitignore convention \`/src/registry/text/\` + \`.worktrees/\` + \`.build-tmp/\`).
