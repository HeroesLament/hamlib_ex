//! Rustler NIF wrapping the Hamlib C API via the `hlx_*` C shim.
//!
//! The BEAM owns the rig handle through a `ResourceArc<RigHandle>`; when the
//! Elixir term is garbage-collected, `Drop` calls `hlx_cleanup`, so a rig is
//! always freed even if the owning process crashes without closing it.
//!
//! All Hamlib operations that touch the radio (`open`, `set_freq`, `set_mode`,
//! `set_ptt`, …) can block on serial I/O, so their NIFs run on a **dirty IO**
//! scheduler — never stall a normal BEAM scheduler waiting on a UART.
//!
//! Errors come back as `{:error, {code, "reason"}}` where `code` is the Hamlib
//! integer error and `"reason"` is `hlx_strerror`'s text.
//!
//! ## Rustler 0.37 conventions (mirrors phy_modem / milwave)
//!
//! - `#[rustler::nif]` auto-registers each NIF via the `inventory` crate.
//! - `#[rustler::resource_impl]` on `impl Resource for RigHandle {}`
//!   auto-registers the resource type at load — no resource list.
//! - `rustler::init!("Elixir.Hamlib.Nif", load = load)` — the only reason we
//!   keep a `load` callback is to silence Hamlib's stdout debug firehose;
//!   resource registration does not need it.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

mod ffi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

use std::ffi::{CStr, CString};
use std::sync::Mutex;

use rustler::{Atom, Encoder, Env, Resource, ResourceArc, Term};

mod atoms {
    rustler::atoms! {
        ok,
        error,
        nil,
        invalid_handle,
        bad_string,
    }
}

/// BEAM-owned rig handle. The inner `hlx_handle` is an opaque pointer-sized
/// integer from the shim. A `Mutex` serializes access: Hamlib's per-rig state
/// is not safe to drive from two schedulers at once, and a single rig is a
/// single serial line anyway. (The mutex also satisfies `ResourceArc`'s
/// `Sync` requirement.)
struct RigHandle {
    inner: Mutex<ffi::hlx_handle>,
}

#[rustler::resource_impl]
impl Resource for RigHandle {}

unsafe impl Send for RigHandle {}
unsafe impl Sync for RigHandle {}

impl Drop for RigHandle {
    fn drop(&mut self) {
        if let Ok(h) = self.inner.lock() {
            if *h != 0 {
                unsafe {
                    // Best-effort: close then cleanup. cleanup frees the RIG*.
                    ffi::hlx_close(*h);
                    ffi::hlx_cleanup(*h);
                }
            }
        }
    }
}

/// Translate a Hamlib return code into either `:ok` (or another ok term) or
/// `{:error, {code, msg}}`.
fn check<'a>(env: Env<'a>, code: i32, ok_term: Term<'a>) -> Term<'a> {
    if code == 0 {
        ok_term
    } else {
        let msg = unsafe {
            let p = ffi::hlx_strerror(code);
            if p.is_null() {
                "unknown".to_string()
            } else {
                CStr::from_ptr(p).to_string_lossy().into_owned()
            }
        };
        (atoms::error(), (code, msg)).encode(env)
    }
}

fn cstring(s: &str) -> Result<CString, Atom> {
    CString::new(s).map_err(|_| atoms::bad_string())
}

/// Initialize a rig for the given Hamlib model number. Returns
/// `{:ok, handle}` or `{:error, :invalid_handle}`. Does not open the port.
#[rustler::nif]
fn init(env: Env, model: i32) -> Term {
    let h = unsafe { ffi::hlx_init(model) };
    if h == 0 {
        (atoms::error(), atoms::invalid_handle()).encode(env)
    } else {
        let res = ResourceArc::new(RigHandle {
            inner: Mutex::new(h),
        });
        (atoms::ok(), res).encode(env)
    }
}

/// Set a Hamlib config token (e.g. "rig_pathname", "serial_speed", "ptt_type").
#[rustler::nif(schedule = "DirtyIo")]
fn set_conf(env: Env, handle: ResourceArc<RigHandle>, token: String, value: String) -> Term {
    let tok = match cstring(&token) {
        Ok(c) => c,
        Err(a) => return (atoms::error(), a).encode(env),
    };
    let val = match cstring(&value) {
        Ok(c) => c,
        Err(a) => return (atoms::error(), a).encode(env),
    };

    let h = *handle.inner.lock().unwrap();
    let rc = unsafe { ffi::hlx_set_conf(h, tok.as_ptr(), val.as_ptr()) };
    check(env, rc, atoms::ok().encode(env))
}

/// Open the configured rig port.
#[rustler::nif(schedule = "DirtyIo")]
fn open(env: Env, handle: ResourceArc<RigHandle>) -> Term {
    let h = *handle.inner.lock().unwrap();
    let rc = unsafe { ffi::hlx_open(h) };
    check(env, rc, atoms::ok().encode(env))
}

/// Close the rig port (rig remains initialized; can be reopened).
#[rustler::nif(schedule = "DirtyIo")]
fn close(env: Env, handle: ResourceArc<RigHandle>) -> Term {
    let h = *handle.inner.lock().unwrap();
    let rc = unsafe { ffi::hlx_close(h) };
    check(env, rc, atoms::ok().encode(env))
}

/// Set frequency in Hz on the current VFO.
#[rustler::nif(schedule = "DirtyIo")]
fn set_freq(env: Env, handle: ResourceArc<RigHandle>, freq_hz: f64) -> Term {
    let h = *handle.inner.lock().unwrap();
    let rc = unsafe { ffi::hlx_set_freq(h, freq_hz) };
    check(env, rc, atoms::ok().encode(env))
}

/// Get frequency in Hz from the current VFO. `{:ok, hz}` on success.
#[rustler::nif(schedule = "DirtyIo")]
fn get_freq(env: Env, handle: ResourceArc<RigHandle>) -> Term {
    let h = *handle.inner.lock().unwrap();
    let mut hz: f64 = 0.0;
    let rc = unsafe { ffi::hlx_get_freq(h, &mut hz as *mut f64) };
    check(env, rc, (atoms::ok(), hz).encode(env))
}

/// Set mode by string ("USB","PKTUSB",…) + passband Hz (0 = rig normal width).
#[rustler::nif(schedule = "DirtyIo")]
fn set_mode(env: Env, handle: ResourceArc<RigHandle>, mode: String, passband_hz: i64) -> Term {
    let m = match cstring(&mode) {
        Ok(c) => c,
        Err(a) => return (atoms::error(), a).encode(env),
    };
    let h = *handle.inner.lock().unwrap();
    let rc = unsafe { ffi::hlx_set_mode(h, m.as_ptr(), passband_hz as std::os::raw::c_long) };
    check(env, rc, atoms::ok().encode(env))
}

/// Get current mode + passband. `{:ok, {mode_string, passband_hz}}`.
#[rustler::nif(schedule = "DirtyIo")]
fn get_mode(env: Env, handle: ResourceArc<RigHandle>) -> Term {
    let h = *handle.inner.lock().unwrap();
    let mut buf = [0u8; 32];
    let mut pb: std::os::raw::c_long = 0;
    let rc = unsafe {
        ffi::hlx_get_mode(
            h,
            buf.as_mut_ptr() as *mut std::os::raw::c_char,
            buf.len() as i32,
            &mut pb as *mut std::os::raw::c_long,
        )
    };
    if rc != 0 {
        return check(env, rc, atoms::ok().encode(env));
    }
    let mode = unsafe { CStr::from_ptr(buf.as_ptr() as *const std::os::raw::c_char) }
        .to_string_lossy()
        .into_owned();
    (atoms::ok(), (mode, pb as i64)).encode(env)
}

/// Set PTT: `true` = transmit, `false` = receive.
#[rustler::nif(schedule = "DirtyIo")]
fn set_ptt(env: Env, handle: ResourceArc<RigHandle>, on: bool) -> Term {
    let h = *handle.inner.lock().unwrap();
    let rc = unsafe { ffi::hlx_set_ptt(h, if on { 1 } else { 0 }) };
    check(env, rc, atoms::ok().encode(env))
}

/// Get PTT state. `{:ok, true|false}`.
#[rustler::nif(schedule = "DirtyIo")]
fn get_ptt(env: Env, handle: ResourceArc<RigHandle>) -> Term {
    let h = *handle.inner.lock().unwrap();
    let mut on: i32 = 0;
    let rc = unsafe { ffi::hlx_get_ptt(h, &mut on as *mut i32) };
    check(env, rc, (atoms::ok(), on != 0).encode(env))
}

/// Set Hamlib's global debug level (0 = silent … 5 = trace).
#[rustler::nif]
fn set_debug(level: i32) -> Atom {
    unsafe { ffi::hlx_set_debug(level) };
    atoms::ok()
}

/// Hamlib library version string.
#[rustler::nif]
fn version() -> String {
    unsafe {
        let p = ffi::hlx_version();
        if p.is_null() {
            String::new()
        } else {
            CStr::from_ptr(p).to_string_lossy().into_owned()
        }
    }
}

fn load(_env: Env, _info: Term) -> bool {
    // Silence Hamlib's stdout debug firehose by default; callers can raise it
    // via Hamlib.Nif.set_debug/1. Resource registration is handled at
    // compile time by #[rustler::resource_impl], so it needs nothing here.
    unsafe { ffi::hlx_set_debug(0) };
    true
}

rustler::init!("Elixir.Hamlib.Nif", load = load);
