# 03_go

Minimal Go consumer. Binds the C ABI with `cgo` via the installed
`chronon3d.pc` (`#cgo pkg-config: chronon3d`), so it links only
`libchronon3d_c.so` — no third-party dev dependencies, no source tree.

```bash
export SDK_PREFIX=/opt/chronon    # install prefix
./run.sh                          # prints GO_CONSUMER_PASS 64x64
```
