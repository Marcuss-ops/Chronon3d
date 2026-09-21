# TICKET-TEXT-ROUTE-CONSOLIDATION — demolition debt dei percorsi testo

**Status:** ACTIVE
**Owner:** Text/Render maintainers
**Reason:** il testo animato conserva percorsi MTSDF, packed-compat, fused-batch e
walk CPU del selector mentre la semantica deve avere una sola authority.

## Demolition Debt

| Campo | Contratto |
|---|---|
| Owner | Text/Render maintainers; aggiornano questa scheda a ogni migrazione |
| Reason | compatibilità GPU incompleta e contratti animati non ancora coperti da MTSDF |
| Exit condition | zero hit sui corpus di route/selector e golden typewriter video verde su frame 0, metà reveal, completamento, completamento+1 e fine |
| Equivalence test/gate | `tools/verify_text_tile_gpu_canary.sh` esteso al golden typewriter; `selector_weight` unit contract; telemetry `text_run_zero_visible_frames == 0` |
| Removal scope | packed route e fused-batch CPU selector walk; packed route resta solo finché il corpus dimostra che MTSDF copre tutti i contratti animati |
| Status | ACTIVE |

Il packed route e il fused-batch CPU selector walk sono demolition debt, non
nuove authorities. La packed compat route è removibile quando MTSDF copre tutti
i contratti animati.
