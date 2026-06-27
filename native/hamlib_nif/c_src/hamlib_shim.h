/*
 * hamlib_shim.h — flat C surface over the Hamlib C API for FFI binding.
 *
 * Hamlib's public headers wrap several calls in __builtin_FUNCTION() macros,
 * so binding rig.h directly (e.g. via bindgen) loses the macro and mismatches
 * the ABI. This shim #includes rig.h in its .c so the macros apply correctly,
 * owns the RIG* handle lifetime, configures the port via the version-stable
 * rig_set_conf() token interface, and converts modes to/from strings. It
 * exposes plain signatures with no Hamlib types in them, so the Rust side
 * binds clean functions and never sees freq_t/rmode_t/RIG.
 *
 * Handle model: hlx_init() returns an opaque handle (uintptr_t) that the
 * caller threads back into every other call. 0 means "init failed".
 *
 * Error model: every operation returns an int Hamlib error code; 0 == RIG_OK,
 * negative == Hamlib error (see hlx_strerror()). Out-params are written only
 * on success.
 */
#ifndef HAMLIB_SHIM_H
#define HAMLIB_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque RIG* handle as an integer the caller threads through every call. */
typedef uintptr_t hlx_handle;

/*
 * Allocate and initialize a rig for the given Hamlib model number.
 * Returns an opaque handle, or 0 on failure. Does NOT open the port —
 * configure with hlx_set_conf(), then hlx_open().
 */
/*
 * Set Hamlib's global debug verbosity. 0 = none (silence the stdout firehose),
 * 1 = bug, 2 = err, 3 = warn, 4 = verbose, 5 = trace. The NIF sets this to 0
 * at load; exposed for debugging. Affects the whole library, not one rig.
 */
void hlx_set_debug(int level);

hlx_handle hlx_init(int model);

/*
 * Set a Hamlib config token by name on an initialized (not-yet-open) rig.
 * Common tokens: "rig_pathname" (serial device or "host:port" for NET rigctl),
 * "serial_speed", "ptt_type" ("RTS"/"DTR"/"CAT"/"NONE"), "data_bits", etc.
 * Returns a Hamlib error code (0 == ok).
 */
int hlx_set_conf(hlx_handle h, const char *token, const char *value);

/* Open the rig's port (must be configured first). Hamlib error code. */
int hlx_open(hlx_handle h);

/* Close the rig's port. Hamlib error code. */
int hlx_close(hlx_handle h);

/*
 * Free the rig and its handle. After this the handle is invalid. Always safe
 * to call (no-op on a 0 handle). Returns a Hamlib error code from cleanup.
 */
int hlx_cleanup(hlx_handle h);

/* Set frequency in Hz on the current VFO. Hamlib error code. */
int hlx_set_freq(hlx_handle h, double freq_hz);

/* Get frequency in Hz from the current VFO. Writes *out_hz on success. */
int hlx_get_freq(hlx_handle h, double *out_hz);

/*
 * Set mode by string ("USB","LSB","CW","FM","AM","PKTUSB","PKTLSB","RTTY",…)
 * and passband width in Hz (pass 0 for the rig's normal width for that mode).
 * Returns a Hamlib error code.
 */
int hlx_set_mode(hlx_handle h, const char *mode_str, long passband_hz);

/*
 * Get current mode + passband. Writes the mode string into out_mode (up to
 * out_mode_len bytes, NUL-terminated) and the passband Hz into *out_pb.
 */
int hlx_get_mode(hlx_handle h, char *out_mode, int out_mode_len, long *out_pb);

/* Set PTT: 0 = off (RX), 1 = on (TX). Hamlib error code. */
int hlx_set_ptt(hlx_handle h, int on);

/* Get PTT state. Writes *out_on (0/1) on success. Hamlib error code. */
int hlx_get_ptt(hlx_handle h, int *out_on);

/*
 * Human-readable string for a Hamlib error code. Returns a pointer to a
 * static string (do not free). Valid for any int returned by the calls above.
 */
const char *hlx_strerror(int errcode);

/* Hamlib library version string (e.g. "Hamlib 4.6.5"). Static; do not free. */
const char *hlx_version(void);

#ifdef __cplusplus
}
#endif

#endif /* HAMLIB_SHIM_H */
