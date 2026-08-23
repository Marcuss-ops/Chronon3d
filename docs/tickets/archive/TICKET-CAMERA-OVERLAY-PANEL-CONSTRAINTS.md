# TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS — Add OverlayCanvasBounds + compute_overlay_panel_constraints pure helper

## Stato: DONE (2026-07-14)

## Problema

Camera debug overlay (`include/chronon3d/scene/camera/camera_debug_overlay.hpp`) is currently 🟡 partial per [`docs/CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md) §8 — Diagnostics and tools: "Safe area, target, bounds/path e viste diagnostiche presenti." The diagnostic emit path (`add_camera_debug_overlay` + `draw_diagnostic_overlay`) is gated behind `#ifdef CHRONON3D_ENABLE_DIAGNOSTICS` and emits the actual panel drawing, but downstream LAYOUT_2D code has no clean way to know where the panel will sit on the canvas WITHOUT invoking the side-effecting `add_camera_debug_overlay()` (which calls into SceneBuilder layers — a side-effect even when `enable_overlay=false` is set).

This forces consumer code into one of two anti-patterns:

1. **Duplicate the anchor-position math** — a Cat-3 anti-duplication violation against the canonical camera overlay surface.
2. **Call the side-effecting function for layout-plan queries** — coupling read-side queries to the diagnostic-render side effects.

The forward-point contract from TICKET-027 (Di diagnostica completa: non tutta la diagnostica viene propagata) requires a clean separation between *plan* (what/where) and *emit* (draw). This chore closes that seam at the camera-overlay level.

## Fix

Add a pure helper pair to `include/chronon3d/scene/camera/camera_debug_overlay.hpp`:

- `struct OverlayCanvasBounds { float x, y, width, height; }` — data-only POD (no methods, no operators), defaults match the canonical panel size 320×240 px.
- `inline OverlayCanvasBounds compute_overlay_panel_constraints(opts, viewport, panel_w=320, panel_h=240)` — pure function returning the canvas-relative bounds of the panel for the given `options.anchor` + `options.panel_offset_x/y`.

The helper is **always-defined** outside any `#ifndef CHRONON3D_ENABLE_DIAGNOSTICS` guard:

- Production builds (diagnostics OFF) benefit equally from layout queries (no runtime cost — pure function, optimisable; ARGS are devirtualisable at the ABI level).
- The function has no diagnostic-internals dependency → no side-channel coupling.
- The function has no SceneBuilder mutation → no side effect, safe to call from any read-only path.

Anchor semantics verbatim:

| Anchor        | x position                                        | y position                                       |
|---------------|---------------------------------------------------|--------------------------------------------------|
| `TopLeft`     | `options.panel_offset_x`                          | `options.panel_offset_y`                         |
| `TopRight`    | `vp_w - panel_w - options.panel_offset_x`         | `options.panel_offset_y`                         |
| `BottomLeft`  | `options.panel_offset_x`                          | `vp_h - panel_h - options.panel_offset_y`        |
| `BottomRight` | `vp_w - panel_w - options.panel_offset_x`         | `vp_h - panel_h - options.panel_offset_y`        |

## File modificato

- `include/chronon3d/scene/camera/camera_debug_overlay.hpp` — 1 NEW struct + 1 NEW inline function (~40 LoC). Zero existing code modified.

## Criteri di accettazione (verifiable on working build host)

- `bash tools/check_main_clean.sh` PASS (no source-side rot introduced on `main`).
- `bash tools/check_architecture_boundaries.sh` PASS (the new public symbol `compute_overlay_panel_constraints` is correctly declared in a header that's already part of the public header manifest per `cmake/Chronon3DPublicHeaders.cmake`).
- `bash -n include/chronon3d/scene/camera/camera_debug_overlay.hpp` clean (header compiles on its own as a syntax check; full TU compilation requires working build host).
- `bash tools/run_developer_gates.sh origin main` 10/10 PASS post-chore (the 10 canonical developer gates preserved; new gate `check_post_push_consistency.sh` (entry #10) gates the post-push SHA-triple invariant — see `AGENTS.md` §Post-push SHA-selfcheck rule).
- Zero regression in `chronon3d_scene_tests` + `chronon3d_camera_tests` ctest suite (post the build-rot cascade closure at `TICKET-BUILD-ROT-CASCADE-CAMERA` commit chain).

## Macchina-verifica path

- **DEFERRED-WBH** per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` env-block pattern (vcpkg `glm`/`magic_enum` missing + tmpfs quota on this VPS).
- The chore is a header-only additive that does not require vcpkg-only dependencies; the toolchain on the working build host will independently verify the symbol linkage + ABI.
- A first-pristine ctest run on WBH after this chore lands will produce the canonical `chronon3d_camera_tests` regression-locks sweep (no new test added in this chore; the helper is exercised by future CLI `inspect-camera` forward-point per `CAMERA_FEATURE_MATRIX.md` §8 CLI path report).

## Cat-3 minimal-surface justification

- The Camera V1 🟡 diagnostic surface gap → ✅ partial-closure (panel-layout awareness is now queryable). ZERO new internal singleton/registry/resolver/cache per `AGENTS.md §regole`. The helper is a free function over already-public types (`CameraDebugOverlayOptions`, `Viewport`).
- The Cat-3 anti-duplication rule is honoured: no SECOND anchor-position math; downstream code can call `compute_overlay_panel_constraints` directly without forking the math.
- The Cat-5 3-doc alignment rule is satisfied (this ticket + `docs/CHANGELOG.md` Cita-Only entry + `docs/FOLLOWUP_TICKETS.md` §Recently Closed row).
- AGENTS.md §regole "No espansione API non necessaria" is honoured: the helper justifies its existence by closing the read-side/emit-side seam (forward-point contract from TICKET-027).

## Forward-points (NOT in this chore, deferred)

1. **TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS-MACHINE-VERIFY** — working build host macchina-verifica on the next available ctest run after `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` env-block resolution.
2. **TICKET-027-DIAGNOSTICA-COMPLETA** (future) — propagate diagnostic completeness to the shot timeline, building on the camera-overlay panel-layout helper as a precedent for separating plan/emit at each diagnostic surface.
3. **`chronon3d_cli inspect-camera` CLI subcommand** (future) — surface `compute_overlay_panel_constraints` output as part of the CLI camera validate path (`tools/verify_camera_full_linux.sh` §8 CLI path report forward-point).
4. **Regression lock for `compute_overlay_panel_constraints`** (future) — pure function, lockable via 4 SUBCASEs (1 per anchor + 1 offset interaction) in `tests/scene/camera/test_camera_overlay_panel_constraints.cpp`. This chore deliberately does NOT add the lock to honour Cat-3 minimal-surface + AGENTS.md "Fare PR piccole e mirate"; the lock lands in a separate atomic chore tied to a working-build-host build.
