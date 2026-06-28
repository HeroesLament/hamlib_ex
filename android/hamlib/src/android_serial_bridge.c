/*
 * android_serial_bridge.c — Android USB-serial bridge for Hamlib.
 *
 * Part of hamlib_ex. See android_serial_bridge.h for the architecture summary.
 *
 * Short version: Hamlib cannot open() a USB serial device on Android (there is
 * no /dev/tty for it). So for an "android-usb:<dev>:<port>" pathname we:
 *
 *   1. open the USB device via the host-supplied hlx_android_usb_serial_open(),
 *   2. socketpair(AF_UNIX, SOCK_STREAM) — give Hamlib fd[0] as its "serial fd",
 *      keep fd[1] as our peer,
 *   3. spawn two pump threads:
 *        rx: hlx_android_usb_serial_read()  -> write(peer)   [CAT replies]
 *        tx: read(peer)                      -> hlx_android_usb_serial_write()
 *
 * Hamlib then does ordinary read()/write()/select() on its fd, oblivious to the
 * USB host API underneath. RTS/DTR bypass the socketpair and hit the USB layer
 * directly (RTS is the DigiRig PTT line).
 *
 * Compiles only under __ANDROID__.
 */

#ifdef __ANDROID__

#include "android_serial_bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Host-provided USB functions are weak: a build without the host
 * implementation still links, and every call degrades to "not ready" / EIO
 * rather than a link error. The Android app provides the strong definitions.
 */
__attribute__((weak)) int hlx_android_usb_serial_is_ready(void) { return 0; }
__attribute__((weak)) int hlx_android_usb_serial_open(int device_id,
                                                      int port_index,
                                                      int baud_rate,
                                                      int data_bits,
                                                      int stop_bits,
                                                      int parity)
{
    (void)device_id; (void)port_index; (void)baud_rate;
    (void)data_bits; (void)stop_bits; (void)parity;
    return -1;
}
__attribute__((weak)) int hlx_android_usb_serial_read(unsigned char *buffer,
                                                      unsigned long length,
                                                      int timeout_ms)
{
    (void)buffer; (void)length; (void)timeout_ms;
    return -1;
}
__attribute__((weak)) int hlx_android_usb_serial_write(const unsigned char *buffer,
                                                       unsigned long length,
                                                       int timeout_ms)
{
    (void)buffer; (void)length; (void)timeout_ms;
    return -1;
}
__attribute__((weak)) int hlx_android_usb_serial_set_rts(int state) { (void)state; return -1; }
__attribute__((weak)) int hlx_android_usb_serial_set_dtr(int state) { (void)state; return -1; }
__attribute__((weak)) int hlx_android_usb_serial_flush(void) { return -1; }
__attribute__((weak)) int hlx_android_usb_serial_close(void) { return -1; }

/* Per-open bridge state, stashed in hamlib_port_t::handle. */
typedef struct hlx_android_serial_context {
    int       fd_peer;     /* our end of the socketpair */
    int       running;     /* pump-thread run flag */
    int       timeout_ms;  /* USB read/write timeout */
    pthread_t rx_thread;
    pthread_t tx_thread;
    int       rx_started;
    int       tx_started;
} hlx_android_serial_context_t;

#define HLX_ANDROID_USB_PREFIX     "android-usb:"
#define HLX_ANDROID_USB_PREFIX_LEN 12

static int parse_usb_path(const char *pathname, int *device_id, int *port_index)
{
    const char *cursor;
    char *end = NULL;
    long dev = 0;
    long port = 0;

    if (!pathname
        || strncmp(pathname, HLX_ANDROID_USB_PREFIX, HLX_ANDROID_USB_PREFIX_LEN) != 0)
    {
        return -1;
    }

    cursor = pathname + HLX_ANDROID_USB_PREFIX_LEN;
    if (*cursor == '\0')
    {
        return -1;
    }

    /* base 0 -> accepts decimal or 0x-hex device ids */
    dev = strtol(cursor, &end, 0);
    if (end == cursor)
    {
        return -1;
    }

    /* optional ":port" or ",port"; default 0 */
    if (end && (*end == ':' || *end == ','))
    {
        port = strtol(end + 1, &end, 0);
    }

    if (dev < 0 || port < 0)
    {
        return -1;
    }

    *device_id = (int)dev;
    *port_index = (int)port;
    return 0;
}

static void drain_socket(int fd)
{
    char buffer[256];

    for (;;)
    {
        ssize_t n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (n <= 0)
        {
            return; /* drained, closed, or would-block */
        }
    }
}

/* rx pump: USB -> socketpair (so Hamlib's read() sees CAT replies). */
static void *rx_thread_fn(void *arg)
{
    hlx_android_serial_context_t *ctx = (hlx_android_serial_context_t *)arg;
    unsigned char buffer[512];
    int error_streak = 0;

    while (ctx->running)
    {
        int got = hlx_android_usb_serial_read(buffer, sizeof(buffer), ctx->timeout_ms);

        if (got > 0)
        {
            ssize_t written = 0;
            error_streak = 0;
            while (written < got && ctx->running)
            {
                ssize_t r = write(ctx->fd_peer, buffer + written, (size_t)(got - written));
                if (r > 0)
                {
                    written += r;
                }
                else if (r < 0 && errno == EINTR)
                {
                    continue;
                }
                else
                {
                    break;
                }
            }
        }
        else if (got == 0)
        {
            error_streak = 0; /* timeout / no data — normal */
        }
        else
        {
            error_streak++;
            if (error_streak == 1 || (error_streak % 50) == 0)
            {
                rig_debug(RIG_DEBUG_WARN, "%s: android USB read failed (streak %d)\n",
                          __func__, error_streak);
            }
            usleep(100000); /* 100ms backoff to avoid a hot spin on hard error */
        }
    }

    return NULL;
}

/* tx pump: socketpair -> USB (Hamlib's write() goes out the radio). */
static void *tx_thread_fn(void *arg)
{
    hlx_android_serial_context_t *ctx = (hlx_android_serial_context_t *)arg;
    unsigned char buffer[512];

    while (ctx->running)
    {
        ssize_t n = read(ctx->fd_peer, buffer, sizeof(buffer));

        if (n > 0)
        {
            ssize_t remaining = n;
            unsigned char *cursor = buffer;
            while (remaining > 0 && ctx->running)
            {
                int w = hlx_android_usb_serial_write(cursor,
                                                     (unsigned long)remaining,
                                                     ctx->timeout_ms);
                if (w > 0)
                {
                    remaining -= w;
                    cursor += w;
                }
                else
                {
                    break;
                }
            }
        }
        else if (n == 0)
        {
            break; /* peer closed */
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            break;
        }
    }

    return NULL;
}

int hlx_android_serial_is_path(const char *pathname)
{
    if (!pathname)
    {
        return 0;
    }
    return strncmp(pathname, HLX_ANDROID_USB_PREFIX, HLX_ANDROID_USB_PREFIX_LEN) == 0;
}

int hlx_android_serial_open(hamlib_port_t *port)
{
    hlx_android_serial_context_t *ctx = NULL;
    int fds[2] = { -1, -1 };
    int device_id = -1;
    int port_index = 0;
    int baud_rate;
    int data_bits;
    int stop_bits;
    int parity;

    if (!port)
    {
        return -RIG_EINVAL;
    }

    if (parse_usb_path(port->pathname, &device_id, &port_index) != 0)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: invalid android-usb path %s\n",
                  __func__, port->pathname);
        return -RIG_EINVAL;
    }

    if (!hlx_android_usb_serial_is_ready())
    {
        rig_debug(RIG_DEBUG_ERR, "%s: android USB bridge not ready\n", __func__);
        return -RIG_ENIMPL;
    }

    baud_rate = port->parm.serial.rate;
    data_bits = port->parm.serial.data_bits;
    stop_bits = port->parm.serial.stop_bits == 2 ? 2 : 1;
    parity    = port->parm.serial.parity;
    if (data_bits != 7 && data_bits != 8)
    {
        data_bits = 8;
    }

    if (hlx_android_usb_serial_open(device_id, port_index, baud_rate,
                                    data_bits, stop_bits, parity) != 0)
    {
        rig_debug(RIG_DEBUG_ERR, "%s: android USB open failed\n", __func__);
        return -RIG_EIO;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    {
        hlx_android_usb_serial_close();
        return -RIG_EIO;
    }

    (void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    ctx = (hlx_android_serial_context_t *)calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        close(fds[0]);
        close(fds[1]);
        hlx_android_usb_serial_close();
        return -RIG_ENOMEM;
    }

    ctx->fd_peer    = fds[1];
    ctx->running    = 1;
    ctx->timeout_ms = port->timeout > 0 ? port->timeout : 1000;

    port->fd     = fds[0];   /* Hamlib's "serial fd" */
    port->handle = ctx;

    if (pthread_create(&ctx->rx_thread, NULL, rx_thread_fn, ctx) != 0)
    {
        ctx->running = 0;
        close(fds[0]);
        close(fds[1]);
        free(ctx);
        port->fd = -1;
        port->handle = NULL;
        hlx_android_usb_serial_close();
        return -RIG_EIO;
    }
    ctx->rx_started = 1;

    if (pthread_create(&ctx->tx_thread, NULL, tx_thread_fn, ctx) != 0)
    {
        ctx->running = 0;
        shutdown(fds[1], SHUT_RDWR);
        if (ctx->rx_started)
        {
            pthread_join(ctx->rx_thread, NULL);
        }
        close(fds[0]);
        close(fds[1]);
        free(ctx);
        port->fd = -1;
        port->handle = NULL;
        hlx_android_usb_serial_close();
        return -RIG_EIO;
    }
    ctx->tx_started = 1;

    rig_debug(RIG_DEBUG_VERBOSE, "%s: android-usb bridge up (dev=%d port=%d)\n",
              __func__, device_id, port_index);
    return RIG_OK;
}

int hlx_android_serial_setup(hamlib_port_t *port)
{
    (void)port;
    return RIG_OK;
}

int hlx_android_serial_close(hamlib_port_t *port)
{
    hlx_android_serial_context_t *ctx;

    if (!port)
    {
        return -RIG_EINVAL;
    }

    ctx = (hlx_android_serial_context_t *)port->handle;
    if (!ctx)
    {
        return RIG_OK;
    }

    ctx->running = 0;
    hlx_android_usb_serial_close();

    /* Unblock both pumps: shut down both ends, then join. */
    if (ctx->fd_peer >= 0)
    {
        shutdown(ctx->fd_peer, SHUT_RDWR);
    }
    if (port->fd >= 0)
    {
        shutdown(port->fd, SHUT_RDWR);
    }

    if (ctx->rx_started)
    {
        pthread_join(ctx->rx_thread, NULL);
    }
    if (ctx->tx_started)
    {
        pthread_join(ctx->tx_thread, NULL);
    }

    if (ctx->fd_peer >= 0)
    {
        close(ctx->fd_peer);
    }
    if (port->fd >= 0)
    {
        close(port->fd);
    }

    free(ctx);
    port->handle = NULL;
    port->fd = -1;

    return RIG_OK;
}

int hlx_android_serial_flush(hamlib_port_t *port)
{
    if (!port)
    {
        return -RIG_EINVAL;
    }

    if (port->fd >= 0)
    {
        drain_socket(port->fd);
    }

    (void)hlx_android_usb_serial_flush();
    return RIG_OK;
}

int hlx_android_serial_set_rts(hamlib_port_t *port, int state)
{
    (void)port;
    return hlx_android_usb_serial_set_rts(state) == 0 ? RIG_OK : -RIG_EIO;
}

int hlx_android_serial_set_dtr(hamlib_port_t *port, int state)
{
    (void)port;
    return hlx_android_usb_serial_set_dtr(state) == 0 ? RIG_OK : -RIG_EIO;
}

#endif /* __ANDROID__ */
