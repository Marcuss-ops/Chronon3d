# TICKET-C3D-VULKAN-FALLBACK-FAILCLOSED-2026-11-22 — eliminare i fallback GPU↔CPU silenziosi

**Stato:** `OPEN — registry/compositor verdi; manca la certificazione counters su job strict`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** Vulkan backend
**Origine:** C3D-AUDIT-06 (parte residua; la cache dei `getenv` è già atterrata)
**Padre:** [TICKET-CHRONON-AUDIT-2026-11-22](TICKET-CHRONON-AUDIT-2026-11-22.md)
**Correlati:** [TICKET-CLI-STRICT-GPU-NATIVE-MAPPING](TICKET-CLI-STRICT-GPU-NATIVE-MAPPING.md)

## Problema

Il backend Vulkan ha ancora un `m_fallback_backend` che, per diverse operazioni,
esegue `sync_gpu_to_cpu` → operazione CPU → `sync_cpu_to_gpu`. Nei job
certificati questo produce readback e lavoro CPU nascosto: il job “GPU native”
può avere successo senza essere realmente GPU-native.

La cache dei flag `std::getenv` è già stata fatta in questa wave; resta la
parte funzionale del gate C3D-AUDIT-06.

## Evidenza

- `[CODICE]` `src/backends/vulkan/vulkan_native_shape_dispatch.cpp:152-166`:
  `draw_node` usa `m_fallback_backend` e `sync_gpu_to_cpu`/`sync_cpu_to_gpu`.
- `[CODICE]` `:171-180`: `apply_effect_stack` fa lo stesso.
- `[CODICE]` `:183-210`: `composite_layer` fa fallback su dimensioni/mode
  non nativi.
- `[CODICE]` `:217-288`: `draw_text_run` fa readback CPU, upload e, se
  `CHRONON3D_TEXT_DEBUG`, un secondo download diagnostico.
- `[CODICE]` `:293-299`: `apply_blur` fa fallback CPU.
- `[GATE PADRE]` “zero `std::getenv` nei loop per-frame/per-nodo; nessun
  fallback silenzioso nei job certificati”.

## Piano di chiusura

1. **Policy esplicita.** Aggiungere al backend una policy derivata da
   `RenderPolicy`/`require_native_gpu`: `allow_cpu_fallback`.
   - `false` nei job certificati: nessun `sync_gpu_to_cpu` e nessun
     `m_fallback_backend`.
   - `true` solo per compatibilità dichiarata e contata.
2. **Fail-closed.** Quando un'operazione non ha un kernel nativo, restituire
   `UnsupportedCapability` con nome nodo/operazione e capacità mancante. Non
   restituire successo dopo un fallback silenzioso.
3. **Kernel nativi mancanti.** Coprire in ordine:
   - `draw_node` per le shape non ancora native;
   - `apply_effect_stack`;
   - `composite_layer` per mode/operator non Normal/SourceOver;
   - `draw_text_run`;
   - `apply_blur`.
4. **Telemetria.** Aggiungere/estendere i counters:
   `silent_cpu_fallback_count`, `cpu_pixel_readback_bytes`,
   `vulkan_fallback_operation`. Devono essere 0 nei job strict.
5. **Test.**
   - `chronon3d_backend_registry_tests` strict.
   - `chronon3d_compositor_tests` per il path CPU.
   - Un caso CLI strict che su host senza feed nativo deve fallire con
     `required_gpu_capability_unavailable` prima del setup.
6. **Log.** Il log `[vulkan-text-fallback]` non deve comparire in un job
   certificato; se compare, il test deve fallire.

## Gate di accettazione

- [ ] Job strict: `cpu_pixel_readback_bytes == 0`.
- [ ] Job strict: `silent_cpu_fallback_count == 0`.
- [ ] Nessun `[vulkan-text-fallback]` nei log certificati.
- [ ] Operazione non supportata → errore tipizzato, nessun output artifact.
- [ ] Backend registry e compositor verdi sul commit.
- [ ] La lane CPU continua a funzionare con fallback esplicito.

## Comandi di verifica

```bash
rg -n "m_fallback_backend|sync_gpu_to_cpu|sync_cpu_to_gpu|vulkan-text-fallback" \
  src/backends/vulkan

ninja -C build/chronon/linux-video-test -j2 \
  chronon3d_backend_registry_tests chronon3d_compositor_tests
./build/chronon/linux-video-test/tests/chronon3d_compositor_tests

# Con ICD Vulkan sano (llvmpipe in CI headless):
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
  ./build/chronon/linux-video-test/tests/chronon3d_backend_registry_tests
```

## Rischi e rollback

- Il fail-closed può trasformare un job oggi “verde ma lento” in errore. È
  intenzionale: il job deve dichiarare la capability o usare il path CPU
  esplicito.
- Rollback: mantenere `allow_cpu_fallback=true` come default legacy, ma
  attivare `false` nella lane strict e nei test di certificazione.

## Definition of Done

Il ticket si chiude quando un job strict non esegue readback/fallback e le
operazioni non supportate falliscono in modo tipizzato, con registry e
compositor verdi. La semplice cache dei `getenv` non chiude questo ticket.

## Esito verifica — 2026-11-22

- Policy esplicita `m_cpu_fallback_allowed`: la lane strict la disattiva e non
  allega il backend CPU di fallback; `silent_cpu_fallback_count` è disponibile.
- `chronon3d_compositor_tests`: 71/71, 651082 assertions, verde sul codice
  corrente.
- Vulkan headless con ICD llvmpipe: “persistent headless device” 28/28 verde;
  il driver software è quindi utilizzabile per i test mirati.
- `chronon3d_backend_registry_tests` completo con ICD llvmpipe: 161/161 test,
  1.999.396 assertions, 0 failed; disk bokeh Vulkan/CPU con delta massimo
  1.19e-7 (soglia 1e-3).
- Senza ICD selezionato il registry fallisce con `vkCreateInstance VkResult=-9`
  (107/161 test); non è una regressione del codice.
- **Non misurati:** `cpu_pixel_readback_bytes == 0`,
  `silent_cpu_fallback_count == 0` e assenza di `[vulkan-text-fallback]` su un
  job strict reale. Il codice e il registry sono verdi, ma la DoD richiede
  questa misura: il ticket resta aperto.
