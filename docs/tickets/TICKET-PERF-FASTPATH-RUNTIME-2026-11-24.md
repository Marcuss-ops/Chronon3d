# TICKET-PERF-FASTPATH-RUNTIME-2026-11-24 — benchmark reale del contract fast path e verifica dei lavori recenti

**Stato:** DONE (sessione 2026-11-24). Implementazione, benchmark runtime e verifica delle suite disponibili completati. Restano aperti i gate su job reale e i follow-up nei ticket collegati.
**Priorità:** P1
**Data:** 2026-11-24
**Owner:** performance / output contract
**Origine:** sessione di lavoro 2026-11-24
**Padre:** [TICKET-PERF-RECEIPT-CONTRACT-FASTPATH](TICKET-PERF-RECEIPT-CONTRACT-FASTPATH.md)
**Non sostituisce:** [TICKET-PERF-100X-VERIFIED-PLAN](TICKET-PERF-100X-VERIFIED-PLAN.md), [TICKET-PERF-ASSET-DECODE-HOTPATH](TICKET-PERF-ASSET-DECODE-HOTPATH.md), [TICKET-PERF-ENCODER-OPEN-REUSE](TICKET-PERF-ENCODER-OPEN-REUSE.md), [TICKET-PERF-PREPARE-RENDER-OVERLAP](TICKET-PERF-PREPARE-RENDER-OVERLAP.md). Questo ticket è il consuntivo della sessione; i gate di dettaglio restano nei ticket originali.

---

## 0. Regola di evidenza

| Tag | Significato |
|---|---|
| `[MISURA]` | Letto dall'output di un test/benchmark eseguito in questa sessione. |
| `[CODICE]` | Verificato nel checkout. |
| `[NOT RUN]` | Non eseguito in questa sessione. |

---

## 1. Cosa è stato fatto oggi

### 1.1 Contract fast path

- `OutputContractFacts` + overload `verify_output_contract(artifact, contract, facts)` in `include/chronon3d/media/video/output_contract.hpp` / `src/media/video/output_contract.cpp`.
- Precondizioni: `trusted_in_process`, `trailer_finalized`, `!hdr_mastering`, `expected_bytes == file_size`, un solo stream video completo, audio con durata quando richiesto.
- Fatti da `MuxSession::observed_media_facts()`, `NativeAvEncoder::output_media_facts()`, `EncoderCloseResult::output_media_facts`; wiring nell'exporter whole-timeline.
- `MuxSession` pubblica `r_frame_rate`/`avg_frame_rate` e traccia `video_end_pts_` prima della write; la durata è pubblicata prima del trailer.
- Qualsiasi fatto mancante/incoerente → `probe_media`; risultato con `contract_fast_path`, `contract_probe_fallback`, `probe_source`.

### 1.2 Benchmark runtime

- Aggiunto il test gated `verify_output_contract: runtime fast path benchmark` in `tests/video/test_output_contract.cpp`.
- Aggiunto `tools/bench_output_contract_fastpath.py`: 3 run, parsing della riga `BENCH`, mediana e risparmio per 100 item.
- Corretto il `MESSAGE` del benchmark: doctest stampava `std::fixed` come `{?}` e lo script non trovava la riga; rimossi i manipolatori, le 3 run sono parsate.

### 1.3 Verifica dei lavori recenti

- Aggiunto `tools/verify_recent_perf.py`: benchmark + suite media/CLI + topology/DoF + deterministic, con auto-discovery di `chronon3d_cli`.
- Costruiti `chronon3d_render_graph_tests` e `chronon3d_deterministic_tests` con `-j3` (75 step, `BUILD_EXIT=0`).
- Aggiornato il ticket padre con §6.6 (benchmark) e §6.7 (verifica).

### 1.4 Nessun commit

- 11 file modificati + 2 script nuovi; nessun commit/push. `git diff --check` e `py_compile` passano.

---

## 2. Risultati runtime

`[MISURA]` Artefatto: MP4 H.264 **1920×1080, 48 frame, 2.442.431 byte**, creato dal test. Ogni run: 2 warmup + 9 iterazioni; stesso artefatto, stesso contract, due fonti di fatti (probe libavformat vs muxer in-process).

| Sessione | probe p50 | fast p50 | risparmio | speedup |
|---|---:|---:|---:|---:|
| 1 | 28,100 ms | 10,551 ms | 17,549 ms | 2,66x |
| 2 | 31,257 ms | 11,162 ms | 20,095 ms | 2,80x |
| 3 | 27,493 ms | 10,505 ms | 16,987 ms | 2,62x |

**Mediana: 2,66x, 17,5 ms per artefatto, circa 1,8 s ogni 100 item.**

- Il fast path calcola ancora lo SHA-256 (8–15 ms); il risparmio è la riapertura libavformat, non il digest.
- Il test `verify_output_contract: real MuxSession facts take the fast path` conferma che i fatti arrivano dal muxer reale.
- **Scope:** è il tempo della verifica del contract, non del render. Sui job del padre `validation_ms` è 46–88 ms, `ffprobe_ms` 33–75 ms, `sha256_ms` 8–15 ms; il risparmio atteso è **30–70 ms/item**, ma non è ancora misurato su un job exporter reale.
- Sul wall caption da 4,7–8,2 s vale ~0,2–0,4%; su un item corto da 300–500 ms vale il 10–25%. È una tassa fissa che sparisce, non una leva di throughput.

---

## 3. Verifica delle suite

`[MISURA]` `python3 tools/verify_recent_perf.py`:

| Controllo | Esito |
|---|---|
| benchmark fast path | PASS, 3 run |
| media/video | **160/160 test, 22.314 asserzioni** |
| CLI contract | **11/11 test, 92 asserzioni** |
| topology animated paths | **1/1 test, 42 asserzioni** |
| DoF graph | **16/16 test, 981 asserzioni** |
| deterministic | **94/94 test, 11.797 asserzioni** |

- Build `chronon3d_render_graph_tests` + `chronon3d_deterministic_tests`: 75 step, `BUILD_EXIT=0`.
- La suite deterministica inizialmente dava 3 FAIL perché non trovava `chronon3d_cli`; con `CHRONON3D_CLI_BINARY=build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli` passa 94/94. Lo script lo imposta da solo.
- Nessun artefatto di test nel repo; `git status` mostra solo le modifiche intenzionali e i due script.

---

## 4. Lavori recenti: stato consolidato

| Area | Evidenza | Stato |
|---|---|---|
| Receipt/contract | 2,62–2,80x; 17–20 ms/artefatto | DONE A/B; gate job reale aperto |
| Asset decode | p50 −69,0% r21 (2.549,861 → 790,687 ms); −78,4/−88,0% caption; wall r21 −37,1% | PARTIAL; decode al bound e replay worker aperti |
| Encoder reuse | +20,3% item/min su 100 job; +14,9% sul mix; 99/100 adozioni | PARTIAL; gate qualità p1/P4 e writer P4 aperti |
| Prepare/render overlap | +6,6% batch 10; +3,9% batch 20; −1,1% batch 100; mappe negative | PARTIAL; prefetch opt-in/spento |
| Topology path animati | 30 frame → 1 materializzazione + 30 path update; 1/1, 42 asserzioni | CPU DONE; baseline SHA job aperta |
| DoF | 16/16, 981 asserzioni; fallback 0; SHA warm == cold | DONE CPU; lane GPU strict non coperta |
| LUT steady state | zero create/upload al secondo frame; 16/16 parità; max err 5,96e-08 | DONE measured |
| Light-wrap | download → blur CPU → upload per frame | OPEN; nessuna misura |
| Timeline eval | 42,6 ms/frame su 46,2 nel batch r49; 0,26–0,74 ms nel benchmark | Da profilare; divario aperto |
| Worker P5 | 196,9 item/min mediani; +19,9%; hash identici | System-level; non è una modifica di codice |

---

## 5. Dove risparmiare: priorità

| Priorità | Leva | Evidenza / potenziale | Prossimo gate |
|---|---|---|---|
| P0 | `timeline_eval` | 42,6 ms/frame su 46,2 nel batch r49; benchmark 0,26–0,74 ms | Profilare il divario, poi cache/incrementalizzazione |
| P0 | Decode al bound display | Restano 0,8–1,6 s/piano; ipotesi 1,5–3 s/item | Decoder con scaling o backend; output identico |
| P1 | Topology a livello job | 1 materializzazione/30 frame; se ~214 ms, ~6 s/30 frame (estrapolazione) | Baseline SHA + wall su job |
| P1 | Light-wrap GPU | download + 2 box blur CPU + upload per frame | Shader + cache blurred; `download_surface=0` |
| P1 | DoF wall | Rebuild evitato; fallback 0; SHA warm == cold | Misura wall su job DoF-heavy |
| P2 | Encoder open/reuse | 39–99 ms/item; +20,3% item/min; SSIM 0,9943, file +15,8–16,3% | Decisione qualità/bitrate p1 vs P4; writer P4 |
| P2 | Vulkan fallback/getenv | 74 `std::getenv` nel path caldo | Lettura una volta, fail-closed |
| P3 | Prepare overlap | +6,6/+3,9/−1,1%; mappe negative | P5 pool/host; prefetch resta off |
| P3 | Map plate/GeoJSON/popen | ChrononPlugin, fuori dal core | Ticket separato |

**Da non fare:** riabilitare il prefetch (A/B negativo) o cambiare preset/bitstream senza sign-off qualità.

---

## 6. Limiti

- Benchmark su un solo artefatto video-only, senza audio, segmenti P4 o HDR.
- Fast path: `codec_config_sha256` vuoto (avcC finale del container diverso dall'extradata); P4/HDR/audio senza durata ricadono sulla probe; fallback sicuro.
- Topology/DoF: test CPU/SHA verdi, ma nessuna misura wall sul job.
- Encoder: gate qualità p1 vs P4 non approvato; 115/2.400 frame mappa diversi nell'A/B; replay worker mappa con parità fallita.
- Deterministic: richiede `CHRONON3D_CLI_BINARY` in questa build dir; lo script lo gestisce.
- Nessun commit/push.

---

## 7. Comandi riproducibili

```bash
# Build e benchmark
ninja -C build/chronon/linux-video-test -j3 chronon3d_media_video_tests
python3 tools/bench_output_contract_fastpath.py --runs 3

# Suite recenti (topology/DoF/deterministic incluse)
ninja -C build/chronon/linux-video-test -j3 \
  chronon3d_render_graph_tests chronon3d_deterministic_tests
python3 tools/verify_recent_perf.py

# Deterministic isolata, se serve
CHRONON3D_CLI_BINARY=build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli \
  ./build/chronon/linux-video-test/tests/chronon3d_deterministic_tests
```

---

## 8. File toccati

- Fast path: `include/chronon3d/media/video/output_contract.hpp`, `src/media/video/output_contract.cpp`, `include/chronon3d/media/video/packet_assembler.hpp`, `src/media/video/mux/packet_assembler_session.cpp`.
- Encoder/exporter: `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp`, `native_av_encoder.hpp`, `native_av_encoder_lifecycle.cpp`, `commands/video/common/pipe_export_types.hpp`, `commands/video/exporters/pipe_export_finalize_encoder.cpp`.
- Test: `tests/video/test_output_contract.cpp`.
- Script: `tools/bench_output_contract_fastpath.py`, `tools/verify_recent_perf.py`.
- Ticket padre: `docs/tickets/TICKET-PERF-RECEIPT-CONTRACT-FASTPATH.md` (§6.6/§6.7).

---

## 9. Definition of Done

Questo ticket di sessione è chiuso quando benchmark e verifiche sono registrati e i limiti sono espliciti. I follow-up restano aperti nei ticket collegati:

- [ ] Job exporter reale: `validation_ms` p50 ≤20 ms su 3 run.
- [ ] 20 artefatti golden field-identici; `codec_config_sha256` catturato o escluso.
- [ ] Decode immagini al bound di display con output identico.
- [ ] Profilo `timeline_eval` sul batch r49 e spiegazione del divario col benchmark.
- [ ] Light-wrap GPU con `download_surface=0` per frame.
- [ ] Baseline SHA/wall per topology e DoF.
- [ ] Decisione qualità/bitrate encoder p1 vs P4.

---

## 10. Prossimi passi

1. Misurare il fast path su un job exporter reale (`validation_ms` p50).
2. Implementare il decode al bound e misurarlo sul corpus r2/r3.
3. Profilare `timeline_eval` e decidere cache/incrementalizzazione.
4. Portare il light-wrap su GPU.
5. Misurare topology/DoF sul job, non solo sui test.
6. Chiudere la decisione encoder con soglie qualità/bitrate approvate.
