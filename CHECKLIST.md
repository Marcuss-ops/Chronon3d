# Checklist — Validazione Finale

> **core only builds, adapters only call, data only flows in.**

Protocollo da eseguire prima di ogni merge o rilascio per verificare che la repo
sia coerente, compilabile e priva di duplicazioni concettuali.

---

## 1. Build completa

```powershell
cmake --build build\chronon\win-release --target chronon3d_cli chronon3d_tests -j 4
```

**Esito atteso:**
- nessun errore di compilazione
- nessun warning nuovo bloccante
- binari generati correttamente
- nessun link error

---

## 2. Test automatici

```powershell
ctest --test-dir build\chronon\win-release -C Release --output-on-failure
```

**Esito atteso:**
- `chronon3d_tests` passa tutto
- nessun test fallito, nessun crash

---

## 3. Render singolo frame (specscene)

```powershell
build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe render output\background_preview.specscene -o output\background_preview.png --frames 0
```

**Esito atteso:**
- PNG creato in `output/background_preview.png`
- sfondo scuro presente
- griglia visibile
- nessun testo, nessun badge, nessun elemento extra non richiesto

---

## 4. Render full HD (1920×1080)

```powershell
build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe render output\background_preview_1920x1080.specscene -o output\background_preview_1920x1080.png --frames 0
```

**Esito atteso:**
- PNG 1920×1080 creato
- griglia regolare
- composizione coerente con il file specscene

---

## 5. Git status pulito

```powershell
git status --short
```

**Esito atteso:**
- nessun file non voluto
- solo le modifiche che si intendono tenere
- nessun artefatto di build o generato accidentalmente

---

## 6. Controllo duplicazioni concettuali

```powershell
rg -n "templates|visual_presets|rendergraph|examples/proofs" include src tests docs Operations
```

**Esito atteso:**
- niente layer vecchi riemersi
- niente logica duplicata sotto nomi nuovi
- niente `templates` / `presets` / `examples` che diventano una seconda logica parallela

---

## Riepilogo: "ok finale"

| # | Controllo | Esito |
|---|---|---|
| 1 | Build passa | ✅ / ❌ |
| 2 | Test passano | ✅ / ❌ |
| 3 | Render specscene PNG corretto | ✅ / ❌ |
| 4 | Render full HD coerente | ✅ / ❌ |
| 5 | `git status` pulito | ✅ / ❌ |
| 6 | Nessun duplicato (`rg`) | ✅ / ❌ |

Tutti ✅ → rilascio pronto.
