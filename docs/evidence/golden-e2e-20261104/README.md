# Golden E2E Pareto — evidenza locale 2026-11-04

## Obiettivo

Rendere non regredibile il percorso che il piano Pareto considera critico:

1. testo editoriale con layout shaped (nessuna stima di larghezza);
2. compositing 2.5D con piani ordinati per profondità;
3. mesh inline, camera e profondità nello stesso render.

Il gate non sostituisce i test di contratto: li ancora a un output pixel
riproducibile e fallisce chiuso su qualunque differenza.

## Gate

Runner: `tests/e2e/golden/run_golden_e2e.py` (solo stdlib).
Fixture: `tests/e2e/golden/plans/*.plan.json`.
Manifest: `tests/e2e/golden/goldens.v1.json` (SHA-256 per frame).

Comando locale:

```bash
python3 tests/e2e/golden/run_golden_e2e.py \
  --cli build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
```

| Scena | Copertura | Frame | Canvas |
|---|---|---|---|
| `01_editorial_text` | testo shaped sopra un layer immagine | 12 | 480×270 |
| `02_packshot_2p5d` | due piani immagine a profondità diverse + caption | 12 | 480×270 |
| `03_mixed_mesh` | mesh inline, piano a profondità, caption | 12 | 480×270 |

## Risultato

- **3/3 scene, 36/36 frame**: tutti gli hash corrispondono al manifest.
- **Determinismo**: ogni scena è stata renderizzata due volte nella stessa
  esecuzione; le due sequenze di hash sono bit-identiche.
- **CTest**: `chronon3d_golden_e2e_tests` registrato come test #107,
  esito `Passed 9.52 sec`, label `e2e;golden`, timeout 900 s.
- **Fail-closed**: alterando l'hash del frame 0 della prima scena il runner
  esce con codice **1** e stampa la differenza; ripristinato il manifest,
  l'esecuzione torna **0**. Il test non può passare con un golden mancante o
  un numero di frame diverso.

Output CTest:

```text
1/1 Test #107: chronon3d_golden_e2e_tests .......   Passed    9.52 sec
100% tests passed, 0 tests failed out of 1
Label Time Summary:
e2e    = 9.52 sec*proc (1 test)
golden = 9.52 sec*proc (1 test)
```

## Ambiente

- Host Linux locale; build `linux-video-release`; backend `software`.
- CLI del checkout `d178f9c49` più il WIP locale sul comando render.
- Nessuna GPU, Vulkan o CI same-SHA è stata eseguita per questo gate.

## Perimetro e limiti

Copre: shaped text, piani immagine ordinati per profondità, mesh inline,
camera, raw sink RGBA8 e determinismo del renderer software.

Non copre: parità Vulkan, NVENC/MP4, CI same-SHA, review visiva umana. I gate
P0-02/P0-10/P0-13 che dipendono da runner, GPU o reviewer restano `BLOCKED` e
non vengono promossi da questa evidenza.

## Riproduzione e aggiornamento

```bash
# Verifica (fallisce su una singola differenza di pixel).
python3 tests/e2e/golden/run_golden_e2e.py --cli <cli>

# Rigenerazione solo dopo una modifica visiva intenzionale e review.
python3 tests/e2e/golden/run_golden_e2e.py --cli <cli> --update-goldens
```

Il job CI `golden-e2e` (Gate 8) esegue lo stesso test sulla lane
`linux-video-test` con Vulkan/CUDA disattivati, quindi su renderer software.
