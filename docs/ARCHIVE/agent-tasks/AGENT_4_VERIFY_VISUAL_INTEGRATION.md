# Agent 4 — Verifica visuale e integrazione

## Missione

Validare visually che Camera (Agent 1) + Testo (Agent 2) funzionino end-to-end sui 6 frame chiave canonici (0/30/70/110/145/179), senza modificare il codice core. Branch: `main` (lavoro diretto, no branch dedicato).

```bash
git fetch origin && git pull --ff-only origin main && git status -sb
```

## Perimetro consentito

- `tests/showcase/test_cinematic_camera_showcase.cpp` (NEW, 6 TEST_CASE A4.1..A4.6)
- `tests/showcase/CMakeLists.txt` (NEW, registro `chronon3d_cinematic_camera_showcase_tests`)
- `tests/CMakeLists.txt` (3 patch: CMAKE_CONFIGURE_DEPENDS + content-gated include + RENDER_TEST_DEPS)
- `tools/verify_cinematic_showcase.sh`, `tools/render_showcase_contact_sheet.sh` (NEW)
- `docs/agent-tasks/AGENT_4_VERIFY_VISUAL_INTEGRATION.md` (NEW, questo file)

Non toccare `include/**`, `src/**`, `content/**`.

## Problema da chiudere

I 6 keyframes sono verificati solo manualmente. Nessun test mirror cattura regression in `cinematic_text_camera` o TextAnimator. Gates A4.1..A4.6 (frame non-empty + camera motion + text motion per 5 presets + determinismo + contact sheet + perf) chiudono il gap.

## Definition of Done

- 5/6 gates OK attualmente osservati su `linux-ci` dopo build (`chronon3d_cinematic_camera_showcase_tests`, 438 MB binary, RC=0); A4.3 per-preset collision diagnostic → TICKET-051
- `output/showcase/contact_sheet.png` scritto, 5760×2160 (~5-15 MB compresso)
- Report finale ≤30 righe
- Zero `src/` / `content/` / `include/` touches (AGENTS.md §"Fare PR piccole e mirate")
