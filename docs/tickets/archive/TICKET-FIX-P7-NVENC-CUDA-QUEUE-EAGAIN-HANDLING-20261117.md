# TICKET-FIX-P7-NVENC-CUDA-QUEUE-EAGAIN-HANDLING-20261117 — P7: Gestione corretta di `AVERROR(EAGAIN)` e rimozione `av_frame_clone` per-frame nella coda CUDA di `NativeAvEncoder`

- **Stato:** CLOSED (2026-11-17 — retry EAGAIN, `av_frame_ref` e pool `CUevent` verificati in `build/chronon/linux-video-test`)
- **Priorità:** P0/P1 (Bug di correttezza e perdita frame sotto backpressure NVENC nel percorso GPU)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `apps/chronon3d_cli/utils/video/native_av_encoder_cuda_queue.cpp` (righe 92–125)
  - `apps/chronon3d_cli/utils/video/native_av_encoder_write.cpp` (righe 310–350, riferimento CPU corretto)
- **Owner:** C3D Video Export / NVENC maintainer

---

## 1. Diagnosi Esatta del Problema

In `apps/chronon3d_cli/utils/video/native_av_encoder_cuda_queue.cpp:92-116`, la funzione `drain_ready_cuda_frames()` invia i frame GPU pronti a FFmpeg/NVENC:

```cpp
const auto send_t0 = Clock::now();
const int send_ret = avcodec_send_frame(codec_, pending.frame);
const double send_dur = elapsed_ms(send_t0);
if (send_ret == 0) session_restart_intra_pending_ = false;
native_send_frame_ms_ += send_dur;
encoder_nvenc_submit_ms_ += send_dur;
...
if (pending.frame) {
    duplicate_seed_source_.reset(av_frame_clone(pending.frame));
}
av_frame_unref(pending.frame);
reusable_cuda_frames_.push_back(pending.frame);
pending.frame = nullptr;
cuda::destroy(pending.ready);
pending_cuda_frames_.pop_front();
if (send_ret < 0) {
    char error[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(send_ret, error, sizeof(error));
    spdlog::error("[native_av] avcodec_send_frame failed for GPU frame {}: {}",
                  frames_written_, error);
    return false;
}
```

**Due difetti verificati**:
1. **Perdita del frame e abort su `AVERROR(EAGAIN)`**: quando il buffer di output di NVENC è pieno, `avcodec_send_frame` restituisce `AVERROR(EAGAIN)` e **non accetta il frame**, richiedendo al chiamante di svuotare i pacchetti con `drain_packets()` e **riprovare `avcodec_send_frame` con lo stesso `pending.frame`**. Qui invece `pending.frame` viene dereferenziato (`av_frame_unref`), rimesso in `reusable_cuda_frames_`, rimosso da `pending_cuda_frames_` e la funzione ritorna `false`, facendo fallire l'esportazione! (Nel percorso CPU `native_av_encoder_write.cpp:316-328`, il retry loop con `drain_packets()` è invece presente).
2. **Overhead per-frame di `av_frame_clone` e `cuda::destroy`**: ad ogni singolo frame inviato viene allocato e liberato un `AVFrame` tramite `av_frame_clone(pending.frame)` (riga 103) e viene distrutto il `CUevent` `pending.ready` (riga 108) invece di riciclarlo.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Implementare il retry loop su `AVERROR(EAGAIN)` prima di rilasciare `pending.frame`
In `apps/chronon3d_cli/utils/video/native_av_encoder_cuda_queue.cpp:92-116`:
```cpp
const auto send_t0 = Clock::now();
int send_ret = avcodec_send_frame(codec_, pending.frame);
double send_dur = elapsed_ms(send_t0);

int eagain_retries = 0;
double backpressure_ms = 0.0;
while (send_ret == AVERROR(EAGAIN) && eagain_retries < kMaxEagainRetries) {
    ++eagain_retries;
    const auto bp_t0 = Clock::now();
    if (!drain_packets()) {
        spdlog::error("[native_av] NVENC packet drain failed during EAGAIN on GPU frame {}",
                      frames_written_);
        return false;
    }
    backpressure_ms += elapsed_ms(bp_t0);
    const auto retry_t0 = Clock::now();
    send_ret = avcodec_send_frame(codec_, pending.frame);
    send_dur += elapsed_ms(retry_t0);
}
native_send_frame_ms_ += send_dur;
encoder_nvenc_submit_ms_ += send_dur;
encoder_backpressure_ms_ += backpressure_ms;

if (send_ret < 0) {
    char error[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(send_ret, error, sizeof(error));
    spdlog::error("[native_av] avcodec_send_frame failed for GPU frame {}: {}",
                  frames_written_, error);
    return false;
}
session_restart_intra_pending_ = false;
```
Solo **dopo** che `send_ret == 0` è confermato, aggiornare `duplicate_seed_source_`, fare `av_frame_unref(pending.frame)`, rimetterlo in `reusable_cuda_frames_` e fare `pending_cuda_frames_.pop_front()`.

### Passo 2 — Sostituire `av_frame_clone` con `av_frame_ref` su istanza pre-allocata e riciclare i `CUevent`

> **Stato 2026-11-17:** `av_frame_clone` è stato sostituito da `av_frame_ref` su
> `duplicate_seed_source_`. I `CUevent` sono ora riciclati tramite
> `reusable_cuda_events_`: i producer chiamano `acquire_recorded_cuda_event()`
> (riusa e ri-registra un evento del pool, altrimenti ne crea uno) e la coda
> rilascia `pending.ready` con `release_cuda_event()` dopo che il codec ha
> accettato il frame. `abort_open()` distrugge il pool residuo.
1. Inizializzare `duplicate_seed_source_` una sola volta con `av_frame_alloc()` e ad ogni frame eseguire:
   ```cpp
   if (!duplicate_seed_source_) duplicate_seed_source_.reset(av_frame_alloc());
   av_frame_unref(duplicate_seed_source_.get());
   (void)av_frame_ref(duplicate_seed_source_.get(), pending.frame);
   ```
2. Rimettere `pending.ready` in un vector `reusable_cuda_events_` invece di chiamare `cuda::destroy(pending.ready)` ad ogni frame (distruggendoli solo in `close()`).

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Unitario**:
   - `chronon3d_native_decoder_teardown_tests` verde; verificare che il retry `EAGAIN` sulla coda CUDA non perda mai il frame in testa a `pending_cuda_frames_`. — **fatto**
2. **Zero allocazioni `AVFrame`**:
   - `av_frame_ref` su `duplicate_seed_source_` — **fatto**.
3. **Zero allocazioni `CUevent` per frame in steady-state**:
   - **fatto**: pool `reusable_cuda_events_` in `NativeAvEncoder`, usato da
     entrambi i producer CUDA e dalla coda; il pool viene svuotato in
     `abort_open()`.
