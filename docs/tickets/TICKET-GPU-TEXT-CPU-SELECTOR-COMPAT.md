# TICKET-GPU-TEXT-CPU-SELECTOR-COMPAT — CPU per-glyph selector compatibility path

## Contesto

`src/render_graph/executor/node_runner_fused_batch.cpp::build_prepared_mtsdf_run()`
contiene un ramo che, per le animazioni testo **non lowerable** sulla GPU,
percorre **ogni glyph** del run su CPU ad ogni frame:

```cpp
if (prepared && prepared->reveal_selector &&
    !text_run::can_lower_gpu_text_animation(shape)) {
    for (std::size_t i = 0; i < shape.layout->placed.glyphs.size(); ++i) {
        ...
        evaluate_compiled_selector(*prepared->reveal_selector, ...);
    }
}
```

Il commento immediatamente sopra dichiara *"Do not walk every glyph here: that
both defeats the hot path..."* — ed è corretto: il percorso GPU usa
`visible_glyph_count` compatto sul record dinamico. Il ramo è quindi un
**compatibility path** per gli shapes che il lowering GPU non sa esprimere,
ed è O(glyph) per frame.

È esattamente il tipo di percorso che l'architettura prepared-GPU-text vuole
eliminare: l'end-state corretto è *"animazione GPU non supportata ⇒ FAIL o
fallback al momento della preparazione"*, mai un loop CPU per-glyph nel frame
loop.

## Modifiche implementate

**File**: `src/render_graph/executor/node_runner_fused_batch.cpp`,
`include/chronon3d/core/profiling/render_counter_macros.hpp`

Il ramo ora emette due contatori (dominio `CHRONON_COUNTERS_TEXT`), così la
sua esistenza è **misurabile** invece che invisibile:

| Contatore | Significato |
|---|---|
| `gpu_text_cpu_selector_compat_frames` | frame che hanno attraversato il ramo compat |
| `gpu_text_cpu_selector_compat_glyph_evals` | valutazioni di selector su CPU (una per glyph per frame) |

Nessun cambio di comportamento: il ramo funziona come prima. La modifica è la
premessa osservabile della demolizione.

## Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | Render-graph executor maintainer |
| **Reason** | Il lowering GPU non copre ancora tutti i tipi di animazione (notoriamente la variante Tracking non lowerable). Finché esiste uno shape non lowerable in produzione, il ramo resta necessario per non rompere il rendering. |
| **Exit condition** | Corpus ufficiale (scene testo animate: Smooth, Tracking, Blur, preset motion) ⇒ `gpu_text_cpu_selector_compat_frames == 0` **e** `gpu_text_cpu_selector_compat_glyph_evals == 0` su un export video completo. Se qualche preset colpisce il ramo: identificare il lowering mancante, implementarlo, e solo allora rimuovere il ramo. |
| **Equivalence test/gate** | Byte-parity dei frame resi (`tools/verify_z3_equivalence.sh --compare` o hashing pixel) fra il ramo compat e il lowering GPU sostitutivo, sullo stesso corpus; `gpu_text_cpu_selector_compat_*` = 0 sul candidate. |
| **Removal scope** | Il blocco `if (prepared && prepared->reveal_selector && !can_lower_gpu_text_animation(shape))` in `build_prepared_mtsdf_run`; il campo `visible_glyph_count` se nessun altro consumatore lo usa; i due contatori `gpu_text_cpu_selector_compat_*`. |
| **Status** | `ACTIVE` — censimento da eseguire sul corpus ufficiale. |

## Cosa NON è stato fatto (e perché)

- **Nessuna rimozione del ramo**: manca l'evidenza di corpus (0 hit). Rimuoverlo
  senza censimento rischierebbe di rompere animazioni testo in produzione.
- **Nessun nuovo lowering GPU**: fuori scope per questo intervento; l'exit
  condition indica la strada (lowering + equivalence test) se il corpus mostra
  hit.
