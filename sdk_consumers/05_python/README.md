# 05_python

Minimal Python consumer. Binds the C ABI with `ctypes` (standard library only,
no third-party packages) and loads `libchronon3d_c.so` from the installed
prefix.

```bash
export SDK_PREFIX=/opt/chronon    # install prefix
./run.sh                          # prints PYTHON_CONSUMER_PASS 64x64
```
