# 05_python

Minimal Python consumer. Binds the C ABI with `ctypes` (standard library only,
no third-party packages) and loads `libchronon3d_c.so` from the installed
prefix.

| File       | Role |
|------------|------|
| `probe.py` | Canonical Python ctypes surface of the C ABI. Subcommands: `abi` (version check), `compile` (valid plan), `schema` (invalid plan rejected), `consume` (full render flow). Driven directly by `tools/verify_sdk_product.sh` (checks 6, 7, 12). |
| `run.sh`   | Consumer entry point: resolves `SDK_PREFIX`/`CHRONON3D_PREFIX` (default `/usr/local`) and runs the `consume` flow. Prints `PYTHON_CONSUMER_PASS <w>x<h>` on success. |
| `plan.json`| Canonical `chronon.render-plan.v2` fixture (single `color` layer, no external assets). |

```bash
export SDK_PREFIX=/opt/chronon    # install prefix
./run.sh                          # prints PYTHON_CONSUMER_PASS 64x64
```
