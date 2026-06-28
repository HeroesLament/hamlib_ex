/*
 * android_serial_bridge.h — Android USB-serial bridge hooks for Hamlib.
 *
 * Part of hamlib_ex. Lets Hamlib drive a USB CAT rig on Android, where there
 * is no /dev/tty for USB serial — the device is reached through the Android
 * USB host API instead.
 *
 * Mechanism (see android_serial_bridge.c): when a rig is opened with a
 * pathname of the form "android-usb:<device_id>:<port_index>", Hamlib's serial
 * layer (patched at the open/close/flush/set_rts/set_dtr entry points) diverts
 * here instead of calling POSIX open() on a tty. We open the USB device via the
 * host-supplied extern functions below, create a socketpair, hand Hamlib one
 * end as a normal fd, and run two pump threads that shuttle bytes between that
 * fd and the USB host API. All of Hamlib's existing read()/write()/select()
 * serial code then works unchanged.
 *
 * The host application (e.g. the Mob/Android app that already owns the CP2102
 * USB session) implements the hlx_android_usb_serial_* extern contract by
 * calling into its Kotlin USB host-API layer. hamlib_ex itself stays radio- and
 * platform-agnostic; this whole file compiles only under __ANDROID__.
 */

#ifndef HLX_ANDROID_SERIAL_BRIDGE_H
#define HLX_ANDROID_SERIAL_BRIDGE_H 1

#ifdef __ANDROID__

/*
 * hamlib_port_t and RIG_* / -RIG_E* live in rig.h. (Hamlib 4.6.x keeps the
 * port struct in include/hamlib/rig.h; there is no separate hamlib/port.h to
 * include here. The bridge .c is built inside the Hamlib source tree, so the
 * internal src/ headers it needs are already on the include path.)
 */
#include "hamlib/rig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Hamlib-facing API (called from the patched src/serial.c) ───────────── */

/* True if `pathname` is an android-usb: path this bridge should handle. */
int hlx_android_serial_is_path(const char *pathname);

/* Open the USB device named by port->pathname and wire it to port->fd. */
int hlx_android_serial_open(hamlib_port_t *port);

/* No-op setup hook (termios equivalent is handled at USB open time). */
int hlx_android_serial_setup(hamlib_port_t *port);

/* Tear down pump threads, socketpair, and the USB session. */
int hlx_android_serial_close(hamlib_port_t *port);

/* Drain buffered input. */
int hlx_android_serial_flush(hamlib_port_t *port);

/* RTS line control (DigiRig PTT path). state: 0=clear, 1=set. */
int hlx_android_serial_set_rts(hamlib_port_t *port, int state);

/* DTR line control. state: 0=clear, 1=set. */
int hlx_android_serial_set_dtr(hamlib_port_t *port, int state);

/* ── Host-facing extern contract (implemented by the Android app) ───────── */
/*
 * These are implemented OUTSIDE hamlib_ex by the host application, against its
 * Android USB host-API layer (the same one that owns the CP2102 session). They
 * are declared `weak` in the .c so a build that does not provide them still
 * links — every call then reports "not ready" and rig open fails cleanly rather
 * than failing to link.
 *
 * Return conventions:
 *   is_ready  -> 1 if a USB serial channel is available, else 0
 *   open      -> 0 on success, non-zero on failure
 *   read      -> bytes read (>0), 0 on timeout/no-data, <0 on error
 *   write     -> bytes written (>0), <=0 on error
 *   set_rts/set_dtr/flush/close -> 0 on success, non-zero on failure
 */
int hlx_android_usb_serial_is_ready(void);
int hlx_android_usb_serial_open(int device_id,
                                int port_index,
                                int baud_rate,
                                int data_bits,
                                int stop_bits,
                                int parity);
int hlx_android_usb_serial_read(unsigned char *buffer,
                                unsigned long length,
                                int timeout_ms);
int hlx_android_usb_serial_write(const unsigned char *buffer,
                                 unsigned long length,
                                 int timeout_ms);
int hlx_android_usb_serial_set_rts(int state);
int hlx_android_usb_serial_set_dtr(int state);
int hlx_android_usb_serial_flush(void);
int hlx_android_usb_serial_close(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANDROID__ */

#endif /* HLX_ANDROID_SERIAL_BRIDGE_H */
