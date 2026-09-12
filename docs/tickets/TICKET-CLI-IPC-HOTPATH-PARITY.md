# TICKET-CLI-IPC-HOTPATH-PARITY — the video-gated CLI/IPC parity gate is red on the hot path

## Contesto

Eseguendo per la prima volta l'intera suite video-gated
(`build/chronon/linux-video-test/tests/chronon3d_cli_tests`, 164 casi), un solo
caso fallisce:

```
tests/cli/test_render_job_planning.cpp:337: FATAL ERROR: CLI/IPC normalized fingerprints differ
  CLI: ... out_hot_path=require_direct_yuv ...
  IPC: ... out_hot_path=auto ...
TEST CASE: CLI and IPC boundary map identical semantics to one normalized job
```

Il sottocaso è `pure-video GPU job routes to DirectYUV`, compilato solo sotto
`#if defined(CHRONON3D_ENABLE_VIDEO)`:

```cpp
JobRequest cli = cli_request_for("vulkan", "nvenc", "native",
                                 "require_direct_yuv");   // ← output.gpu_hot_path_mode
```

**Non è una regressione del lavoro sul codec boundary**: il campo `out_hot_path`
del fingerprint è derivato esclusivamente da `request.output.gpu_hot_path_mode`
(authored dal transport) o dal resolver canonico, che il prototipo
`VideoExecutionPath` non tocca (`resolve_canonical_execution_parameters` non ha
nemmeno un hunk nel diff). Il sottocaso è stato introdotto il **2026-09-07**
(`942794a48`), tre giorni prima di questa sessione.

## Meccanismo

| Passo | Dove |
|---|---|
| Il test helper scrive l'hot path nell'**asse risolto** | `cli_request_for`: `request.output.gpu_hot_path_mode = hot_path` (il sottocaso passa `"require_direct_yuv"`) |
| Il payload IPC non lo porta | il payload ha solo `execution_requirements` + `output_spec` senza `gpu_hot_path_mode` ⇒ decodifica a stringa vuota |
| `normalize_render_job` fa back-fill **solo se vuoto** | `src/runtime/job/render_job_normalizer.cpp:122-124` |
| Il resolver canonico restituisce sempre `"auto"` | `resolve_canonical_execution_parameters`, `src/media/video/video_execution_resolver.cpp:435,444,455` |

Quindi: CLI ⇒ `require_direct_yuv` (authored, sopravvive), IPC ⇒ `auto`
(back-filled). Il fingerprint, che è la chiave di uguaglianza di questo gate,
diverge.

Il commento sopra `check_same_semantics` dichiara che l'uguaglianza è provata dal
fingerprint proprio perché `normalize_render_job` "back-fills the resolved
decision on both sides" — ma il back-fill è condizionale, quindi un hot path
authored **non** viene ricostruito dal lato IPC.

Nessun adattatore CLI di produzione scrive `output.gpu_hot_path_mode`:
l'unica altra occorrenza è il decoder IPC che la **legge** dal payload
(`apps/chronon3d_cli/daemon/daemon_service_ipc.cpp:107`). Il test helper
specifica quindi un asse risolto che il transport CLI reale non produce, e che
la normalizzazione non può ricostruire.

## Perché non si vedeva prima

Senza `CHRONON3D_ENABLE_VIDEO`, `normalize_render_job` prende il ramo `#else`
(`render_job_normalizer.cpp:98-106`) che scrive `gpu_hot_path_mode = "auto"` per
entrambi i transport ⇒ i due fingerprint coincidono e il caso passa. La suite
video-gated non era mai stata eseguita end-to-end: era stata solo verificata con
`-fsyntax-only`.

## Opzioni di fix (da decidere)

| # | Fix | Perché / rischio |
|---|---|---|
| A | **Lato test, fedele al wire**: il payload autora `"gpu_hot_path_mode":"require_direct_yuv"` dentro `output_spec`, come fa il sottocaso gemello che autora `require_gpu_native` | Minimo; entrambi i transport portano l'hot path richiesto. Non cambia la semantica di produzione |
| B | **Lato impl**: `normalize_render_job` assegna sempre `output.gpu_hot_path_mode` dalla risoluzione canonica | Rende il resolver l'autorità unica dell'asse risolto, ma *perde* la richiesta esplicita dell'utente (`--gpu-hot-path-mode require_*`), finché l'hot path richiesto non viene spostato in `requirements`/campo dedicato. Cambiamento più grande |

Raccomandazione: **A** come fix immediato di correttezza del test; **B** solo se
si decide che l'hot path è un asse *derivato* e non *richiesto* — decisione di
contratto, non di test.

## Accettazione

| Gate | Eval |
|---|---|
| Il caso non è più rosso in un build `CHRONON3D_ENABLE_VIDEO=ON` | `chronon3d_cli_tests --test-case="CLI and IPC boundary map identical semantics to one normalized job"` |
| Il gate resta verde nei build senza video | ramo `#else` invariato |

## Demolition Debt

Non applicabile: è un disallineamento di contratto test↔impl, non un percorso
duplicato da demolire.
