//! Build script for the hamlib NIF.
//!
//! Three jobs:
//!   1. Compile the C shim (`c_src/hamlib_shim.c`), which #includes `rig.h`.
//!   2. Generate Rust bindings from the shim header via bindgen (so the Rust
//!      side binds the shim's clean `hlx_*` signatures, never Hamlib's macros).
//!   3. Emit link directives for libhamlib.
//!
//! ## Finding Hamlib
//!
//! Host builds use `pkg-config hamlib` for include + lib paths (Homebrew on
//! macOS, distro `-dev` packages on Linux). For cross-compilation (Android
//! NDK), pkg-config usually can't see the target's Hamlib, so these env vars
//! override:
//!
//!   HAMLIB_INCLUDE_DIR  — dir containing `hamlib/rig.h`
//!   HAMLIB_LIB_DIR      — dir containing `libhamlib.so`
//!
//! When set, they take precedence over pkg-config.

use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=c_src/hamlib_shim.c");
    println!("cargo:rerun-if-changed=c_src/hamlib_shim.h");
    println!("cargo:rerun-if-env-changed=HAMLIB_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=HAMLIB_LIB_DIR");

    let (include_dirs, lib_dir) = resolve_hamlib();

    // 1. Compile the C shim.
    let mut build = cc::Build::new();
    build.file("c_src/hamlib_shim.c").include("c_src");
    for dir in &include_dirs {
        build.include(dir);
    }
    build.compile("hamlib_shim");

    // 3. Link directives. (Do this before bindgen so cargo records them even
    //    if bindgen is slow; order doesn't affect linking.)
    if let Some(dir) = &lib_dir {
        println!("cargo:rustc-link-search=native={}", dir);
    }
    println!("cargo:rustc-link-lib=dylib=hamlib");

    // 2. Generate bindings from the shim header.
    let mut bindings = bindgen::Builder::default()
        .header("c_src/hamlib_shim.h")
        // Only bind our flat surface — not anything Hamlib drags in.
        .allowlist_function("hlx_.*")
        .allowlist_type("hlx_.*")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()));

    for dir in &include_dirs {
        bindings = bindings.clang_arg(format!("-I{}", dir));
    }
    bindings = bindings.clang_arg("-Ic_src");

    let bindings = bindings.generate().expect("bindgen failed on hamlib_shim.h");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("failed to write bindings.rs");
}

/// Returns (include_dirs, lib_dir). Env vars win; else pkg-config; else a
/// best-effort default so the build at least attempts to link.
fn resolve_hamlib() -> (Vec<String>, Option<String>) {
    let env_inc = env::var("HAMLIB_INCLUDE_DIR").ok();
    let env_lib = env::var("HAMLIB_LIB_DIR").ok();

    if env_inc.is_some() || env_lib.is_some() {
        let includes = env_inc.map(|d| vec![d]).unwrap_or_default();
        return (includes, env_lib);
    }

    // Host path: ask pkg-config. We parse its output ourselves rather than use
    // the pkg-config crate so we can feed include dirs to both cc and bindgen.
    if let Ok(output) = std::process::Command::new("pkg-config")
        .args(["--cflags", "--libs", "hamlib"])
        .output()
    {
        if output.status.success() {
            let flags = String::from_utf8_lossy(&output.stdout);
            let mut includes = Vec::new();
            let mut lib_dir = None;
            for tok in flags.split_whitespace() {
                if let Some(p) = tok.strip_prefix("-I") {
                    includes.push(p.to_string());
                } else if let Some(p) = tok.strip_prefix("-L") {
                    lib_dir = Some(p.to_string());
                }
            }
            return (includes, lib_dir);
        }
    }

    (Vec::new(), None)
}
