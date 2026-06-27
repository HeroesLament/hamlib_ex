/*
 * hamlib_shim.c — implementation of the flat C surface over Hamlib.
 *
 * Including rig.h here means Hamlib's __builtin_FUNCTION() macros for
 * rig_set_freq/get_freq/set_vfo apply correctly, so we call the public API as
 * intended rather than the bare ABI symbols. All Hamlib-typed values stay on
 * this side of the boundary; the header exposes only int/double/char* so the
 * Rust/FFI side never depends on Hamlib's headers or type layout.
 */
#include "hamlib_shim.h"

#include <hamlib/rig.h>
#include <string.h>

/* RIG* <-> opaque integer handle. */
static inline RIG *as_rig(hlx_handle h) { return (RIG *)h; }

void hlx_set_debug(int level)
{
    enum rig_debug_level_e lvl;
    switch (level) {
        case 0:  lvl = RIG_DEBUG_NONE; break;
        case 1:  lvl = RIG_DEBUG_BUG; break;
        case 2:  lvl = RIG_DEBUG_ERR; break;
        case 3:  lvl = RIG_DEBUG_WARN; break;
        case 4:  lvl = RIG_DEBUG_VERBOSE; break;
        default: lvl = RIG_DEBUG_TRACE; break;
    }
    rig_set_debug(lvl);
}

hlx_handle hlx_init(int model)
{
    RIG *rig = rig_init((rig_model_t)model);
    return (hlx_handle)rig;
}

int hlx_set_conf(hlx_handle h, const char *token, const char *value)
{
    RIG *rig = as_rig(h);
    if (!rig || !token || !value) {
        return -RIG_EINVAL;
    }

    /* Resolve the token name to its id, then set it. rig_set_conf with a
     * token id is the version-stable path (struct layout is not). */
    hamlib_token_t tok = rig_token_lookup(rig, token);
    if (tok == RIG_CONF_END) {
        return -RIG_EINVAL;
    }
    return rig_set_conf(rig, tok, value);
}

int hlx_open(hlx_handle h)
{
    RIG *rig = as_rig(h);
    if (!rig) {
        return -RIG_EINVAL;
    }
    return rig_open(rig);
}

int hlx_close(hlx_handle h)
{
    RIG *rig = as_rig(h);
    if (!rig) {
        return -RIG_EINVAL;
    }
    return rig_close(rig);
}

int hlx_cleanup(hlx_handle h)
{
    RIG *rig = as_rig(h);
    if (!rig) {
        return RIG_OK; /* nothing to free */
    }
    return rig_cleanup(rig);
}

int hlx_set_freq(hlx_handle h, double freq_hz)
{
    RIG *rig = as_rig(h);
    if (!rig) {
        return -RIG_EINVAL;
    }
    return rig_set_freq(rig, RIG_VFO_CURR, (freq_t)freq_hz);
}

int hlx_get_freq(hlx_handle h, double *out_hz)
{
    RIG *rig = as_rig(h);
    if (!rig || !out_hz) {
        return -RIG_EINVAL;
    }

    freq_t f = 0;
    int rc = rig_get_freq(rig, RIG_VFO_CURR, &f);
    if (rc == RIG_OK) {
        *out_hz = (double)f;
    }
    return rc;
}

int hlx_set_mode(hlx_handle h, const char *mode_str, long passband_hz)
{
    RIG *rig = as_rig(h);
    if (!rig || !mode_str) {
        return -RIG_EINVAL;
    }

    rmode_t mode = rig_parse_mode(mode_str);
    if (mode == RIG_MODE_NONE) {
        return -RIG_EINVAL;
    }

    /* passband 0 -> RIG_PASSBAND_NORMAL (rig picks its normal width). */
    pbwidth_t pb = (passband_hz == 0) ? RIG_PASSBAND_NORMAL : (pbwidth_t)passband_hz;
    return rig_set_mode(rig, RIG_VFO_CURR, mode, pb);
}

int hlx_get_mode(hlx_handle h, char *out_mode, int out_mode_len, long *out_pb)
{
    RIG *rig = as_rig(h);
    if (!rig || !out_mode || out_mode_len <= 0 || !out_pb) {
        return -RIG_EINVAL;
    }

    rmode_t mode = RIG_MODE_NONE;
    pbwidth_t pb = 0;
    int rc = rig_get_mode(rig, RIG_VFO_CURR, &mode, &pb);
    if (rc == RIG_OK) {
        const char *name = rig_strrmode(mode);
        if (!name) {
            name = "";
        }
        strncpy(out_mode, name, (size_t)out_mode_len - 1);
        out_mode[out_mode_len - 1] = '\0';
        *out_pb = (long)pb;
    }
    return rc;
}

int hlx_set_ptt(hlx_handle h, int on)
{
    RIG *rig = as_rig(h);
    if (!rig) {
        return -RIG_EINVAL;
    }
    ptt_t ptt = on ? RIG_PTT_ON : RIG_PTT_OFF;
    return rig_set_ptt(rig, RIG_VFO_CURR, ptt);
}

int hlx_get_ptt(hlx_handle h, int *out_on)
{
    RIG *rig = as_rig(h);
    if (!rig || !out_on) {
        return -RIG_EINVAL;
    }

    ptt_t ptt = RIG_PTT_OFF;
    int rc = rig_get_ptt(rig, RIG_VFO_CURR, &ptt);
    if (rc == RIG_OK) {
        *out_on = (ptt != RIG_PTT_OFF) ? 1 : 0;
    }
    return rc;
}

const char *hlx_strerror(int errcode)
{
    /* rigerror() takes the (negative) Hamlib error code and returns a static
     * descriptive string. Normalize sign so callers can pass either. */
    int code = (errcode > 0) ? -errcode : errcode;
    return rigerror(code);
}

const char *hlx_version(void)
{
    return hamlib_version;
}
