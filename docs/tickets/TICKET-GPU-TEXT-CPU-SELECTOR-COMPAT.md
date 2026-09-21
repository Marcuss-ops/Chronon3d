# TICKET-GPU-TEXT-CPU-SELECTOR-COMPAT — CPU per-glyph selector compatibility path

> **Status: RESOLVED — 2026-09-21**

## Contesto

Questo ticket descriveva un ramo che, per le animazioni testo **non lowerable**
sulla GPU, percorreva **ogni glyph** del run su CPU ad ogni frame.

```cpp
if (prepared && prepared->reveal_selector &&
    !text_run::can_lower_gpu_text_animation(shape)) {
    for (std::size_t i = 0; i < shape.layout->placed.glyphs.size(); ++i) {
        ...
        evaluate_compiled_selector(*prepared->reveal_selector, ...);
    }
}
```

Il ramo è stato rimosso da `build_prepared_mtsdf_run()` e dal percorso MTSDF:
gli animator non lowerable vengono classificati prima e passano al percorso
per-glyph preparato, mentre il MTSDF resta riservato ai profili ammessi.

È esattamente il tipo di percorso che l'architettura prepared-GPU-text vuole
eliminare: l'end-state corretto è *"animazione GPU non supportata ⇒ FAIL o
fallback al momento della preparazione"*, mai un loop CPU per-glyph nel frame
loop.

## Modifiche implementate

- rimosso il walk CPU per-glyph dal fused runner;
- rimosso il walk duplicato dal MTSDF path;
- rimossi i contatori e il relativo snapshot telemetry;
- aggiunto il resolver strutturato `TextRouteDecision`, con route e reason
  osservabili;
- mantenuta l'equivalence coverage del selector compilato e del percorso GPU.

## Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | Render-graph executor maintainer |
| **Reason** | Il walk CPU duplicava la policy del selector e non apparteneva al frame executor. |
| **Exit condition** | Soddisfatta: nessun simbolo o counter del percorso nel codice; route unsupported classificata prima dell'esecuzione. |
| **Equivalence test/gate** | Test selector compilato, test GPU text e test fused verdi; gate architetturale verde. |
| **Removal scope** | Soddisfatto: fused/MTSDF walk e counter rimossi. |
| **Status** | `RESOLVED` — il 2026-09-21. |

## Nota di chiusura

La rimozione non aggiunge un fallback: il resolver route è fail-closed per gli
animator non supportati e il percorso per-glyph preparato conserva la loro
semantica prima del submit GPU.
