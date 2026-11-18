# TICKET-PERF-P6-NVENC-CROSS-ITEM-CODEC-SESSION-REUSE-20261117 — P6: Certificazione e abilitazione del riuso cross-item della sessione encoder NVENC (~259 ms risparmiati per video breve)

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Alto impatto sul throughput di batch di video brevi / overlay)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `apps/chronon3d_cli/utils/video/native_av_encoder_internal.hpp` (righe 27–65)
  - `apps/chronon3d_cli/utils/video/native_av_encoder_open.cpp`
  - `apps/chronon3d_cli/utils/video/native_av_encoder_lifecycle.cpp`
- **Owner:** C3D Video Export / Daemon maintainer

---

## 1. Diagnosi Esatta del Problema

In `apps/chronon3d_cli/utils/video/native_av_encoder_internal.hpp:27-65`, è già implementata l'infrastruttura `CodecSession` per parcheggiare e riutilizzare un `AVCodecContext*` tra job consecutivi che condividono lo stesso `BitstreamTargetContract`, evitando il costo fisso di creazione e distruzione della sessione hardware NVENC (`encoder_open_nvenc` + `encode_close` $\approx 259\text{ ms}$ per video).
Attualmente il meccanismo è **opt-in** tramite `CHRONON_ENCODER_SESSION_REUSE=1` perché richiede la certificazione formale di isolamento tra i bitstream di item consecutivi.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Scrivere la Suite di Certificazione Multi-Item (`tests/video/`)
Aggiungere in `tests/video/test_native_av_encoder_teardown_stress.cpp` un test deterministico che esegue 5 job consecutivi con `CHRONON_ENCODER_SESSION_REUSE=1` (sia su `h264_nvenc` CQP sia su `libx264` CQP):
1. **Isolamento IDR e GOP**: verificare che il pacchetto 0 di ciascuno dei 5 file MP4 prodotti abbia `AV_PKT_FLAG_KEY` attivo (`session_restart_intra_pending_ == true` al primo frame del nuovo item) e che ogni MP4 sia decodificabile indipendentemente da FFmpeg con 0 warning.
2. **Isolamento PTS/DTS**: verificare che i timestamp nel container MP4 di ciascun item partano da 0 e siano strettamente monotoni, senza contaminazione dai PTS dell'item precedente.
3. **Invalidazione su cambio contratto**: verificare che cambiando risoluzione, frame rate, pixel format o color space al 3° item, la sessione parcheggiata venga distrutta e ricreata pulitamente.
4. **Parità Qualitativa**: verificare che i frame decodificati dagli item 2..5 con session reuse abbiano parità (bit-exact su x264 CQP o entro la tolleranza NVENC CQP) rispetto agli stessi item codificati con sessione fresca.

### Passo 2 — Abilitare di Default per i soli Contratti CQP Certificati nel Daemon
Una volta verde il test di certificazione:
- Attivare di default il riuso della sessione nel daemon per i soli contratti già validati come stateless (`is_session_reuse_supported_contract`: CQP senza lookahead/B-adapt cross-boundary), lasciando `CHRONON_ENCODER_SESSION_REUSE=0` come kill-switch diagnostico.

---

## 3. Demolition Debt

Nessuno: `CodecSession` è l'unica authority di caching della sessione codec in `native_av_encoder`.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Automatico**:
   - `chronon3d_native_decoder_teardown_tests` 100% PASS con il nuovo caso multi-item session reuse.
2. **Benchmark Multi-Item**:
   - Misurare su 10 clip consecutive da 48 frame il tempo di open/close dell'encoder nel sidecar (`encoder_open_ms` / `encoder_close_ms`), verificando il crollo da ~259 ms/item a < 5 ms/item sugli item 2..10.
