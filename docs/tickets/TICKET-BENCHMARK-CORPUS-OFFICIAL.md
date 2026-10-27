# TICKET-BENCHMARK-CORPUS-OFFICIAL

## Stato

MANIFEST-LOCKED / HOST-CERTIFICATION-PENDING (2026-09-11)

### Verifica locale manifest (2026-10-26)

- Il JSON contiene 17 casi con ID e scene tutti univoci e non vuoti.
- `python3 -m pytest -q tests/test_gpu_corpus_manifest.py`: PASS (1 test).
- La certificazione runtime resta aperta: questo checkout non contiene gli
  scenari B00–B11/GPU_TEXT del content pack, e la CLI locale non li registra.

## Problema

Rendere ufficiale il corpus GPU B00-B11 + GPU_TEXT_01–05 come riferimento
stabile per regressioni prestazionali. Le composizioni appartengono al content
pack attivo e non vengono duplicate nel core repository.

## Criteri di accettazione

- `docs/benchmarks/gpu-corpus-v1.json` è versionato con 17 casi univoci.
- `tools/benchmark_gpu_baseline.sh --corpus ...` espande il manifest usando un
  unico set di parametri e produce un report per caso.
- `tests/test_gpu_corpus_manifest.py` valida il contratto del manifest.
- La certificazione runtime completa e il test di sanity senza errori sono
  eseguiti sul working host che fornisce content pack e NVENC.
