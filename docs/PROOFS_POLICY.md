# Proofs Policy

Questa regola decide cosa può entrare nei proofs del progetto.

## Regola base

Un proof si aggiunge solo se valida una feature che ha una base vera nel core del progetto.

Per "base vera" intendo:

- API pubblica presente in `include/chronon3d/...`
- implementazione reale presente in `src/...`
- comportamento verificabile senza creare una seconda logica parallela

Se una cosa non ha questa base, non deve diventare un proof.

## Cosa si può aggiungere

- Proof che testano una feature reale del core
- Proof di camera, layer, mask, text, image, depth, effects, graph, cache
- Proof piccoli e mirati, con scopo tecnico chiaro
- Proof che servono a prevenire regressioni

## Cosa non si deve aggiungere

- Demo estetiche senza legame con una feature core
- Scene che duplicano logica già presente altrove
- File che introducono una seconda base visiva solo per comodità
- Proof usati come contenitore di logica applicativa nuova
- Varianti "premium", "style", "theme", "visual preset" se non esiste una feature core da validare

## Regola pratica

Prima di aggiungere un proof, chiediti:

1. La feature esiste già nel core?
2. Il proof la sta solo verificando, non reimplementando?
3. Posso coprire la stessa cosa con un test più piccolo?

Se la risposta a una sola di queste domande è no, il proof non va aggiunto.

## Dove va la logica

- La logica vera va in `include/chronon3d/...` e `src/...`
- I proof devono solo consumare quella base
- `Operations/` resta wrapper leggero, non base di sviluppo

## Obiettivo

Meno proof, ma più utili.
Ogni proof deve servire a validare il core, non a creare un altro strato di progetto.
