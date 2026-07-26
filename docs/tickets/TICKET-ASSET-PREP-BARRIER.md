# TICKET-ASSET-PREP-BARRIER — Explicit asset-preparation barrier

## Stato: CLOSED — 2026-07-26

Chronon3D ora ha un solo barrier sincrono prima del rendering e
dell'encoding:

```text
Composition → AssetManifest → AssetPreflightResolver
            → ResourcePreparation → runtime cache population
            → optional warmup → render/encoder
```

## Implementazione completata

- `runtime::prepare_render()` è il solo orchestratore usato dai percorsi
  still/job/video/chunked/pipe/benchmark/dry-run.
- `ResourcePreparation::prepare()` esegue in ordine le cinque fasi
  dichiarative: font, immagini, metadata video, indice audio e layout.
- La modalità predefinita è fail-loud con `PreparationError` strutturato;
  `WarnAndSkip` è esplicita e produce diagnostica.
- I font vengono verificati tramite il preflight del renderer e le immagini
  vengono decodificate nella `ImageCache` del `RenderRuntime` prima del primo
  frame.
- Video/audio usano il probing nativo quando FFmpeg è abilitato; senza
  backend nativo un asset media richiesto fallisce esplicitamente, senza
  metadata inventati.
- `PreparedAssets` è un value-type di readiness, non una seconda cache o un
  contenitore di handle opachi. L'ownership dei servizi concreti resta nel
  `RenderRuntime`.
- Il renderer nullo viene rifiutato prima dell'esecuzione e la preparazione è
  idempotente rispetto a manifest, resolver e opzioni.
- Non esiste alcun `delayRender()`/busy-wait nel frame loop.

## Verifica

`tests/runtime/test_resource_preparation.cpp` copre manifest vuoto, asset
mancanti, path vuoti, modalità fail-loud/WarnAndSkip, opt-out per fase,
loader per fase, deduplicazione per owner, idempotenza e renderer nullo.

Il gate `check_doc_sync.sh` e il gate architetturale restano obbligatori dopo
ogni modifica a questa pipeline.

## Decisioni di scope

Non vengono aggiunte nuove API `RenderEngine::prepare()` o un overload
`execute(plan, prepared)`: introdurrebbero una seconda superficie pubblica
senza un consumer runtime che la richieda. La barriera esistente è già
integrata nei percorsi produttivi e chiude il requisito P2 senza duplicare
resolver, cache o servizi.
