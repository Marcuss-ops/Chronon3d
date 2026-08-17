# 02_c_minimal

Minimal C ABI consumer. Links only `libchronon3d_c.so` (via pkg-config, with a
plain prefix fallback) and includes the single public C header
`<chronon3d/c_api/chronon3d.h>`.

```bash
export SDK_PREFIX=/opt/chronon    # install prefix
make -C .                         # build c_minimal
make run                          # run (prints C_ABI_CONSUMER_PASS 64x64)
```

Or directly, without Make:

```bash
cc -O2 -Wall main.c $(pkg-config --cflags --libs chronon3d) -o c_minimal
```
