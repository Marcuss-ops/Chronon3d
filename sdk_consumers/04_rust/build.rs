// sdk_consumers/04_rust/build.rs
//
// Adds the installed package's lib dir to the linker search path.  Resolution
// order: pkg-config (chronon3d.pc), then CHRONON3D_PREFIX / SDK_PREFIX, then
// /usr/local.  The `-lchronon3d_c` flag itself comes from the `#[link]`
// attribute in src/main.rs.

use std::env;
use std::path::PathBuf;
use std::process::Command;

fn pkg_config_libdir() -> Option<PathBuf> {
    let output = Command::new("pkg-config")
        .args(["--variable=libdir", "chronon3d"])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let dir = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if dir.is_empty() {
        return None;
    }
    Some(PathBuf::from(dir))
}

fn main() {
    println!("cargo:rerun-if-env-changed=CHRONON3D_PREFIX");
    println!("cargo:rerun-if-env-changed=SDK_PREFIX");

    let libdir = pkg_config_libdir()
        .or_else(|| {
            env::var("CHRONON3D_PREFIX")
                .or_else(|_| env::var("SDK_PREFIX"))
                .ok()
                .map(|prefix| PathBuf::from(prefix).join("lib"))
        })
        .unwrap_or_else(|| PathBuf::from("/usr/local/lib"));

    println!("cargo:rustc-link-search=native={}", libdir.display());
}
