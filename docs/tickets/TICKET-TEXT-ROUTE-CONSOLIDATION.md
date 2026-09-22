# TICKET-TEXT-ROUTE-CONSOLIDATION — demolition debt dei percorsi testo

**Status:** CLOSED — 2026-09-22
**Owner:** Text/Render maintainers
**Reason:** il testo animato conserva percorsi MTSDF, packed-compat, fused-batch e
walk CPU del selector mentre la semantica deve avere una sola authority.

## Demolition Debt

| Campo | Contratto |
|---|---|
| Owner | Text/Render maintainers; aggiornano questa scheda a ogni migrazione |
| Reason | compatibilità GPU incompleta e contratti animati non ancora coperti da MTSDF |
| Exit condition | corpus MTSDF/canary hardware verde, zero packed route production e golden/canary invariants verdi |
| Equivalence test/gate | `tools/verify_text_tile_gpu_canary.sh` esteso al golden typewriter; `selector_weight` unit contract; telemetry `text_run_zero_visible_frames == 0` |
| Removal scope | packed route, packed atlas builder/cache e fused-batch CPU selector walk |
| Status | CLOSED |

La packed route è stata rimossa definitivamente. Il resolver espone soltanto
MTSDF, software esplicito e reject fail-closed; non esiste più una seconda
native text authority né un fallback CPU-rasterizzato che carica un atlas
packed.

Evidence di chiusura:

- `chronon3d_mtsdf_generator_tests`: 7 test / 178 assertion PASS.
- `verify_text_tile_gpu_canary.sh`: 104/104 assertion PASS su RTX A4000/Vulkan.
- packed builder, `GpuTextAtlasCache`, contatori di route e test dedicati rimossi.
- `text_execution_route_name()` non produce più `native_packed`.
