# hamlib_ex

Elixir/BEAM bindings for [Hamlib](https://hamlib.github.io/) — the Ham Radio
Control Library — via a Rustler NIF.

This wraps the Hamlib **C API** (the same surface the JS8Call Android port
drives: `rig_init` → `rig_open` → `rig_set_freq`/`rig_get_freq` /
`rig_set_mode` / `rig_set_ptt` → `rig_close` → `rig_cleanup`) and exposes it
to the BEAM. The point is *not* to reimplement CAT dialects in Elixir — Hamlib
already encodes ~250 rigs' command sets, tested and field-proven — but to make
that library callable from an OTP application.

## Architecture

```
  Elixir  (Hamlib, Hamlib.Rig)                 lib/
     │  Rustler NIF
  Rust    (hamlib_nif crate)                   native/hamlib_nif/src/lib.rs
     │  bindgen over a thin C shim
  C shim  (hlx_* flat functions)               native/hamlib_nif/c_src/hamlib_shim.{c,h}
     │  links -lhamlib
  libhamlib  (the actual Hamlib C library)
```

### Why a C shim instead of bindgen-on-`rig.h`?

Hamlib's public headers wrap the hot calls in `__builtin_FUNCTION()` macros,
e.g. `#define rig_set_freq(r,v,f) rig_set_freq(r,v,f,__builtin_FUNCTION())` —
so the real ABI symbol takes an extra trailing `const char *` the macro injects
for logging. bindgen run directly against `rig.h` binds the underlying symbol
and loses the macro, which is a footgun. The shim (`hamlib_shim.c`) `#include`s
`rig.h` so the macros apply correctly, owns the `RIG *` handle lifetime, and
exposes flat `hlx_*` functions with plain signatures and translated error
codes. bindgen binds the shim, not Hamlib's macro-mangled surface.

## Serial I/O on Android (the open question)

On desktop, Hamlib opens a serial device path (`/dev/ttyUSB0`) and owns the fd.
On Android there is no `/dev/tty*` access for USB serial — the CP2102 is reached
through the Android USB host API (the same `UsbDeviceConnection` the modem app
already owns). Bridging Hamlib's serial layer to that fd is the load-bearing
problem for the on-device target and is tracked separately; host development and
testing here use a real serial path or `rigctld`/dummy rig so the BEAM↔Hamlib
seam can be built and validated before the Android serial bridge lands.

## Status

Scaffolding. Host build against Homebrew Hamlib 4.6.5 first; Android NDK
cross-compile and the USB-serial bridge follow.
