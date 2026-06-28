#!/usr/bin/env python3
"""
Generate android-usb-bridge.patch against a pristine Hamlib 4.6.5 tree.

Applies our serial.c hook insertions + Bionic-compat fixes (misc.c,
microham.c, rig.c) + Makefile.am RIGSRC line, then leaves the tree dirty so
`git diff` can capture a clean unified diff. The android_serial_bridge.{c,h}
files themselves are NOT part of the diff — they are copied in verbatim by the
build script (self-contained new files), so the patch only carries the hook
insertions into existing files.

Run from anywhere; pass the Hamlib source tree as argv[1] (must be a clean
git checkout of tag 4.6.5).
"""
import sys
import pathlib

if len(sys.argv) != 2:
    print("usage: gen_patch.py <hamlib-src-tree>", file=sys.stderr)
    sys.exit(2)

root = pathlib.Path(sys.argv[1]).resolve()


def edit(relpath, replacements):
    """Apply (old, new) replacements to a file, asserting each matches once."""
    p = root / relpath
    s = p.read_text()
    for old, new in replacements:
        n = s.count(old)
        assert n == 1, f"{relpath}: expected 1 match for anchor, got {n}:\n{old[:80]!r}"
        s = s.replace(old, new)
    p.write_text(s)
    print(f"patched {relpath}")


# ── src/serial.c — include + 5 hook insertions ──────────────────────────────

SERIAL_INCLUDE_OLD = '''#include "serial.h"
#include "misc.h"'''
SERIAL_INCLUDE_NEW = '''#include "serial.h"

#ifdef __ANDROID__
#include "android_serial_bridge.h"
#endif
#include "misc.h"'''

SERIAL_OPEN_OLD = '''int HAMLIB_API serial_open(hamlib_port_t *rp)
{

    int fd;               /* File descriptor for the port */
    int err;

    if (!rp)
    {
        return (-RIG_EINVAL);
    }
'''
SERIAL_OPEN_NEW = '''int HAMLIB_API serial_open(hamlib_port_t *rp)
{

    int fd;               /* File descriptor for the port */
    int err;

    if (!rp)
    {
        return (-RIG_EINVAL);
    }

#ifdef __ANDROID__
    if (hlx_android_serial_is_path(rp->pathname))
    {
        return hlx_android_serial_open(rp);
    }
#endif
'''

# serial_flush: insert right after the #endif that closes the __WIN32__ block,
# before the microHam fd check.
SERIAL_FLUSH_OLD = '''    PurgeComm(index->hComm, PURGE_RXCLEAR);
    return RIG_OK;

#endif

    if (p->fd == uh_ptt_fd || p->fd == uh_radio_fd || p->flushx)'''
SERIAL_FLUSH_NEW = '''    PurgeComm(index->hComm, PURGE_RXCLEAR);
    return RIG_OK;

#endif

#ifdef __ANDROID__
    if (hlx_android_serial_is_path(p->pathname))
    {
        return hlx_android_serial_flush(p);
    }
#endif

    if (p->fd == uh_ptt_fd || p->fd == uh_radio_fd || p->flushx)'''

SERIAL_CLOSE_OLD = '''int ser_close(hamlib_port_t *p)
{
    int rc;
    term_options_backup_t *term_backup, *term_backup_prev;

    //rig_debug(RIG_DEBUG_VERBOSE, "%s called\\n", __func__);
'''
SERIAL_CLOSE_NEW = '''int ser_close(hamlib_port_t *p)
{
    int rc;
    term_options_backup_t *term_backup, *term_backup_prev;

    //rig_debug(RIG_DEBUG_VERBOSE, "%s called\\n", __func__);

#ifdef __ANDROID__
    if (hlx_android_serial_is_path(p->pathname))
    {
        return hlx_android_serial_close(p);
    }
#endif
'''

SERIAL_RTS_OLD = '''int HAMLIB_API ser_set_rts(hamlib_port_t *p, int state)
{
    unsigned int y = TIOCM_RTS;
    int rc;

    rig_debug(RIG_DEBUG_VERBOSE, "%s: RTS=%d\\n", __func__, state);

    // ignore this for microHam ports
    if (p->fd == uh_ptt_fd || p->fd == uh_radio_fd)
    {
        return (RIG_OK);
    }
'''
SERIAL_RTS_NEW = '''int HAMLIB_API ser_set_rts(hamlib_port_t *p, int state)
{
    unsigned int y = TIOCM_RTS;
    int rc;

    rig_debug(RIG_DEBUG_VERBOSE, "%s: RTS=%d\\n", __func__, state);

    // ignore this for microHam ports
    if (p->fd == uh_ptt_fd || p->fd == uh_radio_fd)
    {
        return (RIG_OK);
    }

#ifdef __ANDROID__
    if (hlx_android_serial_is_path(p->pathname))
    {
        return hlx_android_serial_set_rts(p, state);
    }
#endif
'''

SERIAL_DTR_OLD = '''int HAMLIB_API ser_set_dtr(hamlib_port_t *p, int state)
{'''
# We need the dtr body to find its microHam guard. Anchor on the function's
# microHam check which is identical in shape to RTS; disambiguate by requiring
# the DTR debug string immediately above.
SERIAL_DTR_GUARD_OLD = '''    rig_debug(RIG_DEBUG_VERBOSE, "%s: DTR=%d\\n", __func__, state);

    // silently ignore on microHam RADIO channel,
    // but (un)set ptt on microHam PTT channel.
    if (p->fd == uh_radio_fd)
    {
        return (RIG_OK);
    }
'''
SERIAL_DTR_GUARD_NEW = '''    rig_debug(RIG_DEBUG_VERBOSE, "%s: DTR=%d\\n", __func__, state);

#ifdef __ANDROID__
    if (hlx_android_serial_is_path(p->pathname))
    {
        return hlx_android_serial_set_dtr(p, state);
    }
#endif

    // silently ignore on microHam RADIO channel,
    // but (un)set ptt on microHam PTT channel.
    if (p->fd == uh_radio_fd)
    {
        return (RIG_OK);
    }
'''

edit("src/serial.c", [
    (SERIAL_INCLUDE_OLD, SERIAL_INCLUDE_NEW),
    (SERIAL_OPEN_OLD, SERIAL_OPEN_NEW),
    (SERIAL_FLUSH_OLD, SERIAL_FLUSH_NEW),
    (SERIAL_CLOSE_OLD, SERIAL_CLOSE_NEW),
    (SERIAL_RTS_OLD, SERIAL_RTS_NEW),
    (SERIAL_DTR_GUARD_OLD, SERIAL_DTR_GUARD_NEW),
])


# ── src/misc.c — parse_hoststr rejects android- prefixes (treat as serial) ──

MISC_OLD = '''    // Handle device names 1st
    if (strstr(hoststr, "/dev")) { return -1; }
'''
MISC_NEW = '''    // Handle device names 1st
    if (strstr(hoststr, "/dev")) { return -1; }

#ifdef __ANDROID__
    // android-usb: paths are serial, not network host strings
    if (strncasecmp(hoststr, "android-usb:", 12) == 0) { return -1; }
#endif
'''
edit("src/misc.c", [(MISC_OLD, MISC_NEW)])


# ── src/rig.c — drop pthread_cancel on Android (not in Bionic) ──────────────
# There are two call sites (async + morse data handlers); both identical.

RIG_CANCEL_OLD = "            pthread_cancel(async_data_handler_priv->thread_id);"
RIG_CANCEL_NEW = ("#ifndef __ANDROID__\n"
                  "            pthread_cancel(async_data_handler_priv->thread_id);\n"
                  "#endif")

RIG_CANCEL2_OLD = "            pthread_cancel(morse_data_handler_priv->thread_id);"
RIG_CANCEL2_NEW = ("#ifndef __ANDROID__\n"
                   "            pthread_cancel(morse_data_handler_priv->thread_id);\n"
                   "#endif")

edit("src/rig.c", [
    (RIG_CANCEL_OLD, RIG_CANCEL_NEW),
    (RIG_CANCEL2_OLD, RIG_CANCEL2_NEW),
])


# ── src/Makefile.am — add the bridge sources to RIGSRC ──────────────────────
# The android_serial_bridge.{c,h} files are copied into src/ verbatim by the
# build script; this just adds them to the build so they compile and link.

MAKEFILE_OLD = "RIGSRC = hamlibdatetime.h rig.c serial.c serial.h misc.c misc.h register.c register.h event.c \\"
MAKEFILE_NEW = "RIGSRC = hamlibdatetime.h rig.c serial.c serial.h android_serial_bridge.c android_serial_bridge.h misc.c misc.h register.c register.h event.c \\"
edit("src/Makefile.am", [(MAKEFILE_OLD, MAKEFILE_NEW)])

print("done")
