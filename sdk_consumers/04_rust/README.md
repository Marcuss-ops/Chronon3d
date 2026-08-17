# 04_rust

Minimal Rust consumer. Binds the C ABI with hand-written FFI declarations (no
bindgen/libclang, no third-party crates) and links only `libchronon3d_c.so`;
`build.rs` resolves the installed lib dir via pkg-config or
`CHRONON3D_PREFIX`/`SDK_PREFIX`.

```bash
export SDK_PREFIX=/opt/chronon    # install prefix
./run.sh                          # prints RUST_CONSUMER_PASS 64x64
```
