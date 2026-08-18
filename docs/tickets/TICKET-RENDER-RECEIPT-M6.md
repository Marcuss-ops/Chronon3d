# TICKET-RENDER-RECEIPT-M6 — Canonical render receipt + release gate (M6)

## Stato: OPEN — receipt implementato (source level), release gate NOT RUN

Il receipt canonico (schema `chronon3d.render-receipt.v1`) è implementato e
promosso nel layer SDK. Il release gate M6 non è ancora stato eseguito
(bloccato dal broker terminal). Stato honest per area sotto.

## Problema

Il receipt nasceva nel layer CLI
(`apps/chronon3d_cli/commands/render/render_receipt.{hpp,cpp}`), con due
difetti strutturali:

1. dipendeva dal tipo CLI-only `PreparedRenderPlanContext` e dalla compile
   definition `CHRONON3D_CLI_PROJECT_VERSION`;
2. non era installabile come header pubblico SDK (il suo input referenziava i
   tipi del render-plan compiler, che sono intenzionalmente FUORI dalla
   superficie pubblica — vedi il commento in `src/CMakeLists.txt`).

Il design doc (M6) colloca il receipt in `include/chronon3d/verification/`
come modulo SDK di prima classe, con un output verifier + `copy_eligible`.

## Implementazione completata (source level)

- `include/chronon3d/verification/render_receipt.hpp` — header pubblico
  dependency-light: `chronon3d::verification::{MediaContract, ReceiptVerification,
  RenderReceiptInput, RenderReceipt}` + `build_render_receipt()` /
  `write_render_receipt()`.
- `src/verification/render_receipt.cpp` — output verifier completo
  (ffprobe/decode/frame_count/codec/pix_fmt/resolution/fps/audio) + SHA-256 +
  `copy_eligible`.
- `src/verification/CMakeLists.txt` — `chronon3d_verification` OBJECT lib
  (linka `nlohmann_json` + `chronon3d_assets`).
- Registrato in `cmake/Chronon3DRegistry.cmake` (OBJECT lib) +
  `cmake/Chronon3DPublicHeaders.cmake` (header API) + `src/CMakeLists.txt`
  (`add_subdirectory`).
- `include/chronon3d/assets/prepared_asset_manifest.hpp` +
  `src/assets/prepared_asset_manifest.cpp` — helper canonico
  `assets::sha256_file()` (riusa la primitiva SHA-256 esistente).
- `command_render_plan.cpp` mappa `PreparedRenderPlan` → `RenderReceiptInput`
  ed emette `<output>.receipt.json` dopo un render riuscito.

## Verifica

- Pre-move: `render --plan ... -o ...mp4` ha prodotto un receipt con
  `copy_eligible: true` e `ffprobe/decode/output_contract: pass` (verificato in
  una sessione precedente).
- Relocation nel layer SDK + check granulari `has_audio`/`-count_frames`: NOT
  build-verified (broker terminal giù dalla relocation).

## Release gate (M6 exit criterion) — NOT RUN

```text
doctor --deep          → READY
validate --plan        → VALID
inspect --json         → resolved plan corretto
render + receipt       → copy_eligible true
consumer C (pkg-config) + C++ (find_package) → pass
```

Bloccato dal broker terminal (`JSON Parse error: Unexpected EOF`).

## Decisioni di scope

- `RenderReceiptInput` trasporta stringhe/int (non `PreparedRenderPlan`) così
  l'header pubblico resta dependency-light e i tipi del render-plan compiler
  restano fuori dall'install set pubblico.
- Il receipt vive in `chronon3d::verification` (non `chronon3d::cli`).
- La CLI conserva solo un adapter di mapping sottile; nessun secondo motore di
  verifica.

## Forward-points

- Build-verify della relocation SDK + riesecuzione del release gate.
- Opzionale: unit test per `build_render_receipt` (copy_eligible true/false).
- Opzionale: consumer pkg-config persistente per CI (oggi il path pkg-config
  non è coperto da `tools/sdk/run_external_consumer.sh`, che usa il target CMake
  `Chronon3D::C`).
