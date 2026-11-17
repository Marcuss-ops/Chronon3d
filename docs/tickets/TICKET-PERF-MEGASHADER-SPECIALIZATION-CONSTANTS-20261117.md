# TICKET-PERF-MEGASHADER-SPECIALIZATION-CONSTANTS-20261117 — Riduzione del Register Pressure (VGPR) e del branching nei megashader (`text_tile_raster.comp`, `warp_bulk.comp`, `color_matrix.comp`) tramite Specialization Constants

- **Stato:** CLOSED (2026-11-17)
- **Priorità:** P2 (Ottimizzazione occupancy SM e riduzione VGPR nei 3 compute shader più grandi del motore)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato: `text_tile_raster.comp` ha 1.262 righe, `color_matrix.comp` 566 righe, `warp_bulk.comp` 557 righe)**
- **File coinvolti:**
  - `src/backends/vulkan/shaders/text_tile_raster.comp`
  - `src/backends/vulkan/shaders/color_matrix.comp`
  - `src/backends/vulkan/shaders/warp_bulk.comp`
  - `src/backends/vulkan/vulkan_kernel_store.hpp`
- **Owner:** C3D Vulkan Shaders maintainer

---

## 1. Diagnosi Esatta del Problema

Nel backend Vulkan di Chronon3D, tre compute shader concentrano molteplici modalità ed effetti in un singolo entry point monolitico:
1. **`text_tile_raster.comp` (1.262 righe)**: include in un unico kernel il percorso fast/standard MTSDF, il supersampling HQ, vector stroke, multi-shadow ordinate, background box arrotondati, gradienti lineari, skew/animatori per-glifo e glow. Anche quando un layer di testo usa solo un riempimento piatto senza stroke, shadow o background, il compilatore SPIR-V→SASS deve allocare i registri temporanei (VGPR) per il caso peggiore dell'intero shader, limitando il numero di warp attivi per SM (occupancy).
2. **`color_matrix.comp` (566 righe)** e **`warp_bulk.comp` (557 righe)**: selezionano l'effetto da eseguire tramite un intero runtime nei push constant (`mode`), mantenendo nello stesso binario compilato decine di rami indipendenti.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Misurare VGPR e Occupancy con `VK_KHR_pipeline_executable_properties`
1. Prima di modificare gli shader, abilitare in uno script/test diagnostico di sviluppo l'estensione Vulkan `VK_KHR_pipeline_executable_properties` (`vkGetPipelineExecutableStatisticsKHR`) per stampare il numero esatto di registri (VGPR), `local memory spill bytes` e occupancy di tutti i 37 compute pipeline.

### Passo 2 — Introdurre Specialization Constants (`layout(constant_id = ...)`) in `text_tile_raster.comp`
1. Aggiungere costanti di specializzazione booleane in `text_tile_raster.comp`:
   ```glsl
   layout(constant_id = 0) const bool kSpecHasStrokeAndShadow = true;
   layout(constant_id = 1) const bool kSpecHasBackgroundAndGradient = true;
   ```
2. Creare in `VulkanKernelStore` (usando la stessa `VkPipelineCache` canonica!) 2 varianti della pipeline `text_tile_raster`:
   - **Fast Simple Variant (`kSpecHasStrokeAndShadow = false, kSpecHasBackgroundAndGradient = false`)**: il compilatore driver elimina con dead-code elimination oltre metà delle righe dello shader, dimezzando i VGPR per il caso più comune (sottotitoli/titoli semplici MTSDF);
   - **Full Feature Variant (`true, true`)**: usata quando `style_flags` dichiara stroke, shadow, background o gradienti.

### Passo 3 — Specializzazione per `mode` (o solo per i mode ad alto uso di registri) in `warp_bulk.comp` / `color_matrix.comp`
- Se l'ispezione del Passo 1 mostra register spilling (`spill > 0`) in `warp_bulk.comp` o `color_matrix.comp` a causa di un singolo `mode` complesso, promuovere `mode` (o il gruppo di mode) a `layout(constant_id = 0) const int kSpecMode = -1;`.

---

## 3. Demolition Debt

Nessuno: lo shader sorgente `.comp` rimane **uno solo** (nessuna duplicazione di file `.comp`!), e le varianti sono generate a creazione `VkPipeline` tramite `VkSpecializationInfo` dentro `VulkanKernelStore`.

---

## 4. Verifica e Criteri di Chiusura

1. **Tabella VGPR Prima/Dopo**:
   - Documentare il numero di registri per thread di `text_tile_raster` (variante Simple vs Full) tramite `VK_KHR_pipeline_executable_properties`.
2. **Parità Pixel**:
   - `tools/verify_text_tile_gpu_canary.sh` (154/154) e `chronon3d_effects_tests` (308/308) 100% PASS.
