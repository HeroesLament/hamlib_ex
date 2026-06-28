# Android cross-compile + USB-serial bridge — design notes

Distilled from the JS8Call-improved Android port, which already solved both
problems. Reference artifacts are saved at the repo root (gitignored):

  - `.ref-android-ndk.yml`   — their NDK cross-compile CI workflow
  - `.ref-README.android`    — their Android build doc
  - `.ref-bridge.patch`      — `0001-android-usb-serial-bridge.patch`
  - `.ref-hamlib_jni.cpp`    — their JNI rig-control layer
  - `.ref-serial.c` / `.ref-iofunc.c` — stock Hamlib serial path (context)

## 1. Cross-compiling libhamlib for arm64 Android

Standard autotools build with the NDK LLVM toolchain. **API 28 minimum.**
`--without-libusb` is mandatory (no libusb cross build; USB rigs go through the
bridge below, not libusb).

```sh
export NDK=$HOME/Android/Sdk/ndk/28.0.12433566
export TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/<host>   # darwin-x86_64 on this Mac
export TARGET=aarch64-linux-android
export API=28
export AR=$TOOLCHAIN/bin/llvm-ar
export AS=$TOOLCHAIN/bin/llvm-as
export CC=$TOOLCHAIN/bin/${TARGET}${API}-clang
export CXX=$TOOLCHAIN/bin/${TARGET}${API}-clang++
export LD=$TOOLCHAIN/bin/ld
export RANLIB=$TOOLCHAIN/bin/llvm-ranlib
export STRIP=$TOOLCHAIN/bin/llvm-strip

./bootstrap                                  # git checkout only, not release tarball
./configure --host=$TARGET --without-libusb
make
make install DESTDIR=/tmp/hamlib-android-aarch64
```

Output (`libhamlib.so` + headers) under DESTDIR feeds straight into our crate's
`build.rs` via `HAMLIB_INCLUDE_DIR` / `HAMLIB_LIB_DIR`. The result needs
`libc++_shared.so` deployed alongside (or static linking).

## 2. The USB-serial bridge — how Hamlib reaches a USB rig with no /dev/tty

The interception is NOT in upstream Hamlib and NOT in iofunc.c's `port_open`
dispatch. It is a **patch applied to Hamlib at build time** (their patch lives
in the *app* repo, not the Hamlib fork tree). The patch:

### Magic pathname

The host passes `rig_pathname = "android-usb:<device_id>:<port_index>"` (or
`android-bt:<MAC>:<port>`). Everything keys off that prefix.

### Hook points (all in `src/serial.c`, `#ifdef __ANDROID__`)

At the top of each, `if (android_serial_is_path(rp->pathname)) return
android_serial_*(rp);`:

  - `serial_open`   -> `android_serial_open`
  - `serial_flush`  -> `android_serial_flush`
  - `ser_close`     -> `android_serial_close`
  - `ser_set_rts`   -> `android_serial_set_rts`   <- our PTT path
  - `ser_set_dtr`   -> `android_serial_set_dtr`

### The core trick (`android_serial_open`) — socketpair + pump threads

Hamlib is NOT handed a real serial fd. Instead:

  1. Parse `android-usb:<dev>:<port>`.
  2. Call JNI bridge `js8_android_usb_serial_open(dev, port, baud, data, stop, parity)`
     — Kotlin opens the USB device via the Android USB host API.
  3. `socketpair(AF_UNIX, SOCK_STREAM, 0, fds)`. Give Hamlib `port->fd = fds[0]`;
     keep `fds[1]` as the bridge peer. Stash a context in `port->handle`.
  4. Spawn two pump threads:
       - rx: `js8_android_usb_serial_read()` -> `write(fds[1])`   (CAT replies)
       - tx: `read(fds[1])` -> `js8_android_usb_serial_write()`   (CAT commands)

So all of Hamlib's existing `read()/write()/select()` serial code runs
unchanged against `port->fd`; the socketpair + threads bridge that fd to the
Android USB host API. RTS/DTR bypass the socketpair and call the bridge directly.

### The extern contract (host app must implement these)

```c
int js8_android_usb_serial_is_ready(void);
int js8_android_usb_serial_open(int device_id, int port_index,
                                int baud_rate, int data_bits,
                                int stop_bits, int parity);
int js8_android_usb_serial_read (unsigned char *buf, unsigned long len, int timeout_ms);
int js8_android_usb_serial_write(const unsigned char *buf, unsigned long len, int timeout_ms);
int js8_android_usb_serial_set_rts(int state);
int js8_android_usb_serial_set_dtr(int state);
int js8_android_usb_serial_flush(void);
int js8_android_usb_serial_close(void);
```

(Plus a parallel `js8_android_bt_serial_*` set for Bluetooth — we can drop BT.)

### Supporting patches (Bionic compatibility)

  - `misc.c`     — `parse_hoststr` rejects `android-usb:`/`android-bt:` so they
                   are treated as serial, not network.
  - `microham.c` — stub `glob()`/`globfree()` (not in Bionic).
  - `rig.c`      — drop `pthread_cancel` on Android (not in Bionic; join only).
  - `Makefile.am`— add `android_serial_bridge.c/.h` to `RIGSRC`.

## 3. How this maps onto hamlib_ex + the modem app

The `js8_android_usb_serial_*` externs are the seam. In our world the modem
app's Manager already owns the CP2102 USB serial session (open, read, write,
RTS keying) via the Android USB host API. Plan:

  - **Adopt the patch** (rename `js8_*` -> our symbols, drop BT) and apply it to
    the Hamlib we cross-compile.
  - **Implement the extern contract** against our existing CP2102/USB layer.
    Two homes:
      (a) a small C/JNI file in the mob app bundle that calls into Kotlin, or
      (b) route through the BEAM: externs call back into our NIF/Elixir, which
          talks to the Manager's CP2102 session.
    (a) is closer to JS8's proven design; (b) is more OTP-idiomatic but adds a
    NIF round-trip on the serial hot path. The socketpair pump-thread model
    tolerates either since it's already async.

  - **PTT note:** the bridge exposes `set_rts`, which is exactly our DigiRig PTT
    (RTS) path. Likely keep the Manager's direct RTS keying for the half-duplex
    T/R gate timing and use Hamlib only for CAT (freq/mode), to avoid two owners
    of RTS.

## Open decisions for next session

  1. Extern contract implementation: native/JNI (JS8-style) vs BEAM-callback.
     Affects whether hamlib_ex stays radio-agnostic or grows an Android bridge.
  2. Whether to vendor a trimmed patch (USB only, our symbol names) in this repo
     under `android/hamlib/patches/`, applied by the cross-compile script.
  3. RTS ownership: Hamlib `ser_set_rts` vs Manager direct keying (avoid double
     ownership during half-duplex T/R).
