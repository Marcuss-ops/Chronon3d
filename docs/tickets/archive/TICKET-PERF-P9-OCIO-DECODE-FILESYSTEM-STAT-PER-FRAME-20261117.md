# TICKET-PERF-P9-OCIO-DECODE-FILESYSTEM-STAT-PER-FRAME-20261117 — P9: Eliminazione delle syscall `std::filesystem::last_write_time` e `file_size` per-frame in `NativeVideoFrameDecoder::decode_frame_at_impl`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Eliminazione syscall filesystem e allocazioni stringa ad ogni frame video decodificato con OCIO)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/media/video/native_video_frame_decoder_decode_at.cpp` (righe 46–83)
  - `include/chronon3d/media/video/native_video_frame_decoder.hpp`
- **Owner:** C3D Media / Color maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/media/video/native_video_frame_decoder_decode_at.cpp:50-68`:

```cpp
#ifdef CHRONON3D_ENABLE_OCIO
    std::shared_ptr<color::OcioColorTransform> ocio_transform;
    if (transform_options != nullptr) {
        std::error_code metadata_error;
        const auto modified = std::filesystem::last_write_time(
            transform_options->config_path, metadata_error);
        const auto size = metadata_error ? 0 : std::filesystem::file_size(
            transform_options->config_path, metadata_error);
        if (metadata_error)
            return make_decode_failure(...);
        const auto key = transform_options->config_path + "\n" +
            transform_options->input_space + "\n" + transform_options->working_space + "\n" +
            std::to_string(modified.time_since_epoch().count()) + ":" + std::to_string(size);
        {
            std::lock_guard lock(m_mutex);
            const auto it = m_ocio_transforms.find(key);
            if (it != m_ocio_transforms.end()) ocio_transform = it->second;
        }
```

**Difetto verificato**:
Ogni volta che il decoder decodifica un singolo frame (`decode_frame_at_impl`) con una trasformazione colore OCIO attiva:
1. Invoca **due syscall al kernel Linux** (`statx` / `stat` tramite `std::filesystem::last_write_time` e `std::filesystem::file_size`) sul file `.ocio` prima ancora di guardare nella cache `m_ocio_transforms`!
2. Esegue 2 conversioni `std::to_string` e 6 concatenazioni per allocare sull'heap la `std::string key`.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Cachare i metadati del file `.ocio` per l'istanza di `NativeVideoFrameDecoder` (o usare lookup a 2 livelli senza syscall sul fast-path)
In `src/media/video/native_video_frame_decoder_decode_at.cpp`:
1. La chiave logica della trasformazione dentro una sessione di decode è la tripla `(config_path, input_space, working_space)`.
2. Sotto `m_mutex`, cercare prima nella mappa `m_ocio_transforms` usando una chiave `(config_path, input_space, working_space)` (senza fare alcuna syscall `stat` sul filesystem!).
3. **Solo in caso di cache miss** (cioè la **prima volta** che quel `config_path` viene visto dal decoder):
   - Verificare l'esistenza/accessibilità del file con `std::filesystem::last_write_time` / `file_size` per produrre lo stesso identico messaggio diagnostico `DecodeFailureReason::UnsupportedFormatChange` se il file non esiste;
   - Costruire `color::OcioColorTransform::from_config(...)` e inserirlo in `m_ocio_transforms`.
4. Se un test specifico modifica un file `.ocio` allo stesso identico path *durante* la vita della stessa istanza `NativeVideoFrameDecoder`, verificare con `ctest -R ocio`: se esiste tale test, cachare l'ultimo `(config_path, mtime, size)` o invalidare su `clear()`, altrimenti il lookup in-memory elimina il 100% delle syscall dai frame $1 \dots N$.

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Unitari OCIO e Video Decode**:
   - `ctest -R "ocio|video_decode|native_decoder"` 100% PASS.
2. **Verifica Syscall**:
   - Zero chiamate `last_write_time` / `file_size` nei frame successivi al frame 0 della stessa clip.
