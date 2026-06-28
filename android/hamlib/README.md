# Android USB-serial bridge for Hamlib

This directory builds `libhamlib` for Android with a USB-serial bridge, so
`hamlib_ex` can drive a USB CAT rig (e.g. a DigiRig's CP2102) on a phone — where
there is no `/dev/tty` for USB serial.

See `../../docs/ANDROID_BRIDGE.md` for the full design rationale. Short version:
Hamlib is given a magic pathname `android-usb:<device_id>:<port_index>`; patched
hooks in `serial.c` divert open/flush/close/set_rts/set_dtr to the bridge, which
opens the USB device through host-supplied functions, creates a `socketpair`,
hands Hamlib one end as an ordinary fd, and runs two pump threads to shuttle
bytes to/from the Android USB host API. Hamlib's own read/write/select code runs
unchanged.

## Layout

```
android/hamlib/
  src/
    android_serial_bridge.h     bridge API + host extern contract
    android_serial_bridge.c     socketpair + pump-thread implementation
  patches/
    0001-android-usb-serial-bridge.patch   hooks into serial.c/misc.c/rig.c/Makefile.am
  gen_patch.py                  regenerates the patch against a pinned Hamlib tree
  build-hamlib-android.sh       NDK cross-compile driver
```

The bridge `.c`/`.h` are self-contained new files copied into Hamlib's `src/`
verbatim; the patch only carries the hook insertions into existing files. This
keeps the patch tiny (~140 lines) and robust against Hamlib line drift.

## Building

```sh
export ANDROID_NDK_HOME=~/Android/Sdk/ndk/28.0.12433566
./build-hamlib-android.sh aarch64        # or armv7a / x86_64
```

Then point the `hamlib_nif` crate at the result:

```sh
export HAMLIB_INCLUDE_DIR=$PWD/out/aarch64/usr/local/include
export HAMLIB_LIB_DIR=$PWD/out/aarch64/usr/local/lib
```

`build.rs` honors those two env vars ahead of pkg-config, so the NIF then
compiles its shim against the cross-built headers and links the cross-built lib.

## The host extern contract

The bridge calls eight functions that the **host application** must implement
against its Android USB layer (the Mob app already owns the CP2102 session).
They are declared `weak` in `android_serial_bridge.c`, so a build without them
still links — every call then degrades to "not ready" and rig-open fails
cleanly rather than failing to link.

```c
int hlx_android_usb_serial_is_ready(void);
int hlx_android_usb_serial_open(int device_id, int port_index,
                                int baud_rate, int data_bits,
                                int stop_bits, int parity);
int hlx_android_usb_serial_read (unsigned char *buf, unsigned long len, int timeout_ms);
int hlx_android_usb_serial_write(const unsigned char *buf, unsigned long len, int timeout_ms);
int hlx_android_usb_serial_set_rts(int state);   /* DigiRig PTT line */
int hlx_android_usb_serial_set_dtr(int state);
int hlx_android_usb_serial_flush(void);
int hlx_android_usb_serial_close(void);
```

Return conventions: `is_ready` → 1/0; `open`/`set_*`/`flush`/`close` → 0 on
success; `read` → bytes (>0), 0 on timeout, <0 on error; `write` → bytes (>0),
≤0 on error.

## Regenerating the patch

If retargeting a different Hamlib tag, re-anchor and regenerate:

```sh
git clone --depth 1 --branch <tag> https://github.com/Hamlib/Hamlib.git /tmp/hl
python3 gen_patch.py /tmp/hl
git -C /tmp/hl diff > patches/0001-android-usb-serial-bridge.patch
```

`gen_patch.py` asserts each anchor matches exactly once, so a drift in upstream
will fail loudly rather than silently mis-patch.

## Notes / open items

- **Static vs shared:** the build script defaults to `--disable-shared
  --enable-static` (static-link libhamlib into the NIF `.so`, simplest to
  deploy). `build.rs` currently emits `dylib=hamlib`; reconcile this when wiring
  the NIF's Android cross-compile (Mob's build path sets its own link flags).
- **libc++_shared.so:** Hamlib's Android binaries need it deployed alongside, or
  static linking of the C++ runtime — relevant only if the C++ binding is built
  (we pass `--without-cxx-binding`).
- **RTS ownership:** the bridge exposes `set_rts`, which is the DigiRig PTT line.
  The modem app's Manager already does direct RTS keying for half-duplex T/R
  timing — keep that authoritative and use Hamlib only for CAT (freq/mode) to
  avoid two owners of RTS.
