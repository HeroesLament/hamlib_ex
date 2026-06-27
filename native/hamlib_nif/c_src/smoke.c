/* Standalone host smoke test for hamlib_shim — NOT part of the NIF build.
 * Compiles the shim against Homebrew hamlib and drives the dummy rig (model 1)
 * to prove the shim's signatures match the installed library before we wrap it
 * in Rust. Build/run via c_src/smoke.sh. */
#include "hamlib_shim.h"
#include <stdio.h>

int main(void)
{
    hlx_set_debug(0); /* silence hamlib stdout firehose */
    printf("version: %s\n", hlx_version());

    /* Model 1 = dummy/virtual rig: no hardware, every call succeeds. */
    hlx_handle h = hlx_init(1);
    if (!h) {
        printf("init FAILED\n");
        return 1;
    }
    printf("init ok, handle=%p\n", (void *)h);

    int rc = hlx_open(h);
    printf("open: %d (%s)\n", rc, hlx_strerror(rc));

    rc = hlx_set_freq(h, 14074000.0);
    printf("set_freq 14.074MHz: %d (%s)\n", rc, hlx_strerror(rc));

    double f = 0;
    rc = hlx_get_freq(h, &f);
    printf("get_freq: %d -> %.0f Hz\n", rc, f);

    rc = hlx_set_mode(h, "PKTUSB", 0);
    printf("set_mode PKTUSB: %d (%s)\n", rc, hlx_strerror(rc));

    char mode[32];
    long pb = 0;
    rc = hlx_get_mode(h, mode, sizeof(mode), &pb);
    printf("get_mode: %d -> %s pb=%ld\n", rc, mode, pb);

    rc = hlx_set_ptt(h, 1);
    printf("set_ptt ON: %d (%s)\n", rc, hlx_strerror(rc));

    int on = -1;
    rc = hlx_get_ptt(h, &on);
    printf("get_ptt: %d -> %d\n", rc, on);

    rc = hlx_set_ptt(h, 0);
    printf("set_ptt OFF: %d (%s)\n", rc, hlx_strerror(rc));

    hlx_close(h);
    hlx_cleanup(h);
    printf("done\n");
    return 0;
}
