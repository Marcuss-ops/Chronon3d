# TICKET-077 — software_renderer.hpp LOC overflow (233 > 200)

## Stato
OPEN

## Priorità
P0

## Problema
L'header `include/chronon3d/backends/software/software_renderer.hpp` supera il limite di 200 linee imposto dal gate-3 soft-boundary R4. A `a8842d20` l'header emette `[FAIL] I2: header LOC=233 > 200`.

## Evidenza
`bash tools/check_software_renderer_boundary.sh` → I2 FAIL. Il file non è stato modificato nei 9 commit tra `a8842d20..HEAD` (solo `software_renderer.cpp` ha avuto un touch in `e19125dd`).

## Impatto
Blocca la certificazione baseline verde (gate-3 I2). Feature freeze attivo finché non risolto.

## Confine
Solo `software_renderer.hpp`. Non toccare `software_renderer.cpp` a meno che non serva per spostare helper privati.

## Soluzione accettabile
Splittare in sub-header logici (es. `software_renderer_types.hpp` per tipi POD, `software_renderer_config.hpp` per struct di configurazione runtime) OPPURE spostare helper private-only in `src/backends/software/software_renderer.cpp`. Non introdurre nuove classi pubbliche in `include/chronon3d/` (feature freeze).

## Criteri di accettazione
- Header LOC ≤ 200
- gate-3 I2 PASS
- Nessuna nuova classe pubblica in `include/chronon3d/`
- Non regressione di I3 (TICKET-078)

## Collegamenti
- Gate: check_software_renderer_boundary I2
- Ticket correlati: TICKET-078, TICKET-079
- Milestone: M0
