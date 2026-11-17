# Shadow map PCF evidence — rigenerata 2026-11-16

`report.json` è prodotto da:

```bash
CHRONON3D_SHADOW_MAP_REPORT=docs/evidence/ae-shadow-map-20261106/report.json \
  build/chronon/linux-video-release/tests/chronon3d_shadow_map_tests -tc='Vulkan*'
```

Lane: Vulkan, **NVIDIA RTX A4000** (16 GB, driver 595.99.02), ICD NVIDIA
(`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json`).

Esito: 51/51 assertion PASS. `status: PASS`, `edge_error_px: 0`,
`pcf_softened: true`, `bias_removed_acne: true`, `cpu_gpu_max_delta: 0`.

Limiti: fixture sintetica 32×32 con caster proiettato dal test. Non è l'audit
AE test 6 su scena di produzione, non certifica l'integrazione del pass nel
render graph e non sostituisce una CI same-SHA. Il vecchio `report.json` FAIL
era precedente al commit `cece645cf` che ha introdotto il pass.
