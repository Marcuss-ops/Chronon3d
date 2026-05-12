# Guida alla Build e Sviluppo di Chronon3d

Chronon3d supporta attualmente due flussi di lavoro per la compilazione e lo sviluppo. Questa guida spiega come configurare e utilizzare entrambi.

## 🛠️ Prerequisiti (Windows)

- **C++20 Compiler**: Visual Studio 2022 o superiore (con il workload "Sviluppo di applicazioni desktop con C++").
- **PowerShell**: Per l'esecuzione degli script di automazione.
- **Git**: Per il controllo del codice sorgente.

## ⚡ Metodo 1: Xmake (Percorso consigliato per lo sviluppo rapido)

Xmake è il percorso consigliato per lo sviluppo rapido e l'iterazione veloce. Gestisce le dipendenze automaticamente e offre tempi di compilazione incrementale estremamente ridotti. CMake rimane il sistema standard e supportato per le build di produzione.

### Comandi Rapidi
Usa lo script di sviluppo per compilare ed eseguire un render di prova:
```powershell
.\tools\dev.ps1
```

### Comandi Manuali
1. **Configurazione**:
   ```powershell
   xmake f -m debug          # Modalità Debug (default)
   xmake f -m release        # Modalità Release
   xmake f --profiling=true  # Abilita Tracy Profiling
   ```
2. **Compilazione**:
   ```powershell
   xmake -y
   ```
3. **Esecuzione**:
   ```powershell
   xmake run chronon3d_cli list
   ```

---

## 🏛️ Metodo 2: CMake + vcpkg (Sistema Standard)

CMake viene mantenuto per garantire la compatibilità con gli standard di settore e per le build di produzione.

### Script di Automazione
Lo script `tools/chronon-win.ps1` gestisce l'installazione delle dipendenze tramite vcpkg e la configurazione di CMake:
```powershell
# Build Release (default)
.\tools\chronon-win.ps1

# Build Debug
.\tools\chronon-win.ps1 -Configuration Debug
```

Su Linux, usa `tools/chronon-linux.sh`:
```bash
bash tools/chronon-linux.sh
```

---

## 🚀 Utilizzo della CLI (chronon3d_cli)

Una volta compilato, puoi usare la CLI per interagire con il motore.

### 1. Elencare le Composizioni
Mostra tutti i video/scene registrati nel codice:
```powershell
xmake run -w . chronon3d_cli list
```

### 2. Rendering di un Frame
Esegue il render di un singolo frame di una composizione specifica:
```powershell
xmake run -w . chronon3d_cli render RoundedRectProof --frame 0 -o output/render.png
```

### 3. Rendering di un'Animazione
```powershell
xmake run -w . chronon3d_cli render AnimatedVisualQualityProof --frame 0 -o output/seq_0000.png
xmake run -w . chronon3d_cli render AnimatedVisualQualityProof --frame 30 -o output/seq_0030.png
```

---

## 🧪 Testing e Validazione

Per verificare che tutto funzioni correttamente dopo una modifica:
```powershell
.\tools\test-xmake.ps1
```
Questo script eseguirà una suite completa di test di rendering e verificherà l'integrità dei file prodotti.

## 📁 Struttura Directory

| Percorso | Contenuto |
|---|---|
| `apps/chronon3d_cli/` | Entry point CLI |
| `examples/` | Compositions (codice creativo) |
| `include/chronon3d/` | Header pubblici del motore |
| `src/` | Implementazioni (renderer, scene, io) |
| `tests/` | Test automatici (doctest) |
| `tools/` | Script di build, render e watch |
| `assets/` | Font, immagini di test |
| `docs/` | Documentazione interna |
| `output/` | Destinazione rendering (gitignored) |
