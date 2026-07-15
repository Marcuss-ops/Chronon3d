# TICKET-TEXT-SPEC-FORWARDER-REMOVAL

## Stato

FORWARD-POINT REGISTER — P2 Text

## Problema

Rimuovere l'overload `LayerBuilder::text(name, TextSpec)` forwarder e tracciare la migrazione dei caller al canale canonico `TextDefinition`.

## Criteri di accettazione

- L'overload forwarder è rimosso dal codebase.
- Tutti i caller migrati a `TextDefinition`.
- Nessuna regressione nei test testo.
