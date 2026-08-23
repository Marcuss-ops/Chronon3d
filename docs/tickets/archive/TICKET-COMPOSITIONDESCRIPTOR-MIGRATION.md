# TICKET-COMPOSITIONDESCRIPTOR-MIGRATION — Canonical registration API

## Stato: DONE / ARCHIVED (2026-08-01)

La migrazione è completa sul `main` corrente. `CompositionRegistry` espone
soltanto `add(CompositionDescriptor)`: l'overload
`add(std::string, Factory)` e la mappa duplicata `factories_` non sono più
presenti. La soppressione globale dei warning di deprecazione è anch'essa
assente.

Non restano call site da migrare per questo ticket. Le deprecazioni pubbliche
ancora esistenti sono indipendenti e tracciate in
`TICKET-DEPRECATED-API-REMOVAL`.
