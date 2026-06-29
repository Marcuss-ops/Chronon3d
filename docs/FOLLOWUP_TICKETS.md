# Follow-up Tickets — Open Blockers Index

> Snapshot: `main@0ac86d9f8` — 2026-06-29. Linux-only.
>
> File canonico per gli aperti. Massimo ~10 righe di tabella, ordinate per blocco della
> certificazione `Baseline verde: CERTIFICATA` (vedi `AGENTS.md` regola di lavoro #4). Tutti gli
> altri ticket aperti non-baseline-critical + i 7 ticket con stato derivazionale `UNKNOWN` (data-quality
> carry-forward di Step 6, riqualificazione deferrata a Step 8) vivono in
> `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md` e in git history pre-Step-6.
>
> Schema pregresso (`🔵 Planned` / `🟡 Partial` / `🟢 Done`) rimane canonico. Label `🔵 OPEN`
> introdotta per TICKET-051 è sinonimo di `🔵 Planned` (Step 4 spec Agent 3).

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

## Cross-link history

Per la cronologia completa (49+ ticket chiusi + 28 deferred a follow-up + 7 ticket `UNKNOWN` da
riqualificare in Step 8), vedi:

- `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md` — verbatim cronologia ticket chiusi + deferred.
- `git show main~N:docs/FOLLOWUP_TICKETS.md` (commit pre-Step-6) per la versione 3834-line originale.
- `docs/CHANGELOG.md` — eventi sul main.
