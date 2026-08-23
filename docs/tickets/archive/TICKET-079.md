# TICKET-079 — RuntimeAdapter::attach_software_backend prende SoftwareRenderer& in process surface

## Stato
OPEN

## Priorità
P0

## Problema
Il runtime-adapter process surface espone `void attach_software_backend(chronon3d::SoftwareRenderer& renderer);` in `runtime_adapter.hpp:57`, che è un lvalue reference che pinna `SoftwareRenderer` (il gate-3 R2 lo vieta; solo `SoftwareRenderer&&` rvalue-ref è permesso per move legittimi).

## Evidenza
A `a8842d20` + `main@eecfda9c` questa è l'unica violazione I5 che blocca gate-3. `grep -rn 'SoftwareRenderer&' src/render_graph/ src/runtime/ include/chronon3d/runtime/ apps/` mostra la reference.

## Impatto
Blocca gate-3 I5 → blocca baseline verde.

## Confine
Solo la firma di `attach_software_backend`. Non toccare il comportamento runtime.

## Soluzione accettabile
Opzioni (in ordine): (1) prendere per `chronon3d::SoftwareBackend&` (il facade pipeline di livello inferiore); (2) prendere per `chronon3d::runtime::RenderRuntime&`; (3) ristrutturare split in `attach_software_backend(unique_ptr<SoftwareBackend>)` + `attach_renderer_driver(...)`.

## Criteri di accettazione
- 0 riferimenti `SoftwareRenderer&` non qualificati in `src/render_graph/`, `src/runtime/`, `include/chronon3d/runtime/`, `apps/`
- gate-3 I5 PASS
- Nessuna regressione I2 (TICKET-077), I3 (TICKET-078)

## Collegamenti
- Gate: check_software_renderer_boundary I5
- Ticket correlati: TICKET-077, TICKET-078
- Milestone: M0
