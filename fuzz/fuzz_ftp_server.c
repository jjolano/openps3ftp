/*
 * libFuzzer target: OpenPS3FTP, end to end.
 *
 * Each input is a raw byte stream fed to a fresh in-process server
 * (fs_mem backend, ephemeral loopback port, 1 worker). The harness
 * follows the session: 227/229 replies are parsed and the data channel
 * is connected, so PASV/EPSV/PORT + RETR/STOR transfer paths run for
 * real. Any crash or ASan report is attributed to the exact input.
 *
 * Build + run:
 *   cmake -B build-fuzz -DOPFTP_FUZZ=ON -DOPFTP_SANITIZE=address \
 *         -DCMAKE_C_COMPILER=clang
 *   cmake --build build-fuzz -j
 *   ./build-fuzz/fuzz_ftp_server -max_total_time=300 fuzz/corpus
 *
 * Long runs: ASAN_OPTIONS=quarantine_size_mb=8 keeps RSS flat.
 * PORT seeds use 127.0.0.1:2026 (the harness's data listener).
 */
#include "opftp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT_LISTEN_PORT 2026
#define SESSION_DEADLINE_MS 1500

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Nonblocking connect to the PASV data port on loopback. */
static int tcp_connect(uint16_t port, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct sockaddr_in a = { .sin_family = AF_INET,
                             .sin_port = htons(port) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*) &a, sizeof(a)) != 0) {
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        if (poll(&p, 1, timeout_ms) <= 0) {
            close(fd);
            return -1;
        }
        int err = 0;
        socklen_t el = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

/* Direction of the transfer that follows a PASV/EPSV/PORT: 1 = client
 * sends (STOR/APPE/STOU), -1 = client receives (RETR), 0 = unknown.
 * Replies come back in command order, so a per-occurrence queue maps
 * each 227/229 (or PORT accept) to the direction of the command that
 * follows the corresponding command in the input. */
#define MAX_DIRS 32
static int dir_of(const char* from)
{
    if (strstr(from, "STOR") || strstr(from, "APPE") || strstr(from, "STOU"))
        return 1;
    if (strstr(from, "RETR"))
        return -1;
    return 0;
}

static void fill_dirs(const uint8_t* d, size_t n, int* out_dirs, size_t* out_n,
                      int* port_dirs, size_t* port_n)
{
    char in[4097];
    size_t cpy = n > 4096 ? 4096 : n;
    memcpy(in, d, cpy);
    in[cpy] = '\0';
    *out_n = *port_n = 0;
    const char* pos = in;
    while (*pos) {
        const char* a = strstr(pos, "PASV");
        const char* b = strstr(pos, "EPSV");
        const char* c = strstr(pos, "PORT");
        const char* hit = a;
        if (b && (!hit || b < hit)) hit = b;
        if (c && (!hit || c < hit)) hit = c;
        if (!hit)
            break;
        int dir = dir_of(hit);
        if (*out_n + *port_n < MAX_DIRS) {
            if (hit == c)
                port_dirs[(*port_n)++] = dir;
            else
                out_dirs[(*out_n)++] = dir;
        }
        pos = hit + 4;
    }
}

/* Data socket exchange. For STOR the server reads what we write (the
 * input bytes, up to 8K); for RETR we drain until the server closes. */
static void data_exchange(int dfd, const uint8_t* data, size_t size, int dir)
{
    if (dir != -1) {
        char buf[8192];
        size_t len = size > sizeof(buf) ? sizeof(buf) : size;
        if (len == 0) {
            buf[0] = 'x';
            len = 1;
        } else {
            memcpy(buf, data, len);
        }
        size_t off = 0;
        while (off < len) {
            ssize_t n = send(dfd, buf + off, len - off, MSG_NOSIGNAL);
            if (n > 0) {
                off += (size_t) n;
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                struct pollfd p = { .fd = dfd, .events = POLLOUT };
                if (poll(&p, 1, 100) <= 0)
                    break;
                continue;
            }
            break;
        }
        close(dfd);
        return;
    }
    char buf[8192];
    int64_t t0 = now_ms();
    for (;;) {
        struct pollfd p = { .fd = dfd, .events = POLLIN };
        int r = poll(&p, 1, 50);
        if (r > 0) {
            if (recv(dfd, buf, sizeof(buf), 0) <= 0)
                break;   /* EOF: server done */
            continue;
        }
        if (now_ms() - t0 > 400)
            break;
    }
    close(dfd);
}

/* Find a 227/229 data port in the accumulated reply text. */
static int parse_data_port(const char* buf)
{
    for (const char* h = strstr(buf, "227"); h; h = strstr(h + 3, "227")) {
        const char* op = strchr(h, '(');
        if (op) {
            int a, b, c, d, p1, p2;
            if (sscanf(op, "(%d,%d,%d,%d,%d,%d)", &a, &b, &c, &d, &p1, &p2) == 6)
                return p1 * 256 + p2;
        }
    }
    for (const char* h = strstr(buf, "229"); h; h = strstr(h + 3, "229")) {
        const char* op = strstr(h, "(|||");
        if (op) {
            unsigned port;
            if (sscanf(op, "(|||%u|", &port) == 1 && port > 0 && port < 65536)
                return (int) port;
        }
    }
    return -1;
}

/* PORT-mode listener the seeds point at (127.0.0.1:2026). */
static int make_listener(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = { .sin_family = AF_INET,
                             .sin_port = htons(PORT_LISTEN_PORT) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr*) &a, sizeof(a)) != 0 ||
        listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Drive the control session: read replies, connect data channels
 * (PASV/EPSV), accept PORT connections, then go quiet and finish. */
static void run_session(int cfd, int listener, const uint8_t* data, size_t size)
{
    static char buf[65536];
    size_t blen = 0;
    int out_dirs[MAX_DIRS], port_dirs[MAX_DIRS];
    size_t out_n, port_n, dir_idx = 0, port_idx = 0;
    fill_dirs(data, size, out_dirs, &out_n, port_dirs, &port_n);
    int last_port = -1;
    int64_t t0 = now_ms();
    int64_t last_traffic = t0;

    while (now_ms() - t0 < SESSION_DEADLINE_MS) {
        struct pollfd p = { .fd = cfd, .events = POLLIN };
        int r = poll(&p, 1, 5);
        if (r > 0 && blen < sizeof(buf) - 1) {
            ssize_t n = recv(cfd, buf + blen, sizeof(buf) - 1 - blen,
                             MSG_DONTWAIT);
            if (n > 0) {
                blen += (size_t) n;
                buf[blen] = '\0';
                last_traffic = now_ms();
                int port = parse_data_port(buf);
                if (port > 0 && port != last_port) {
                    last_port = port;
                    int dfd = tcp_connect((uint16_t) port, 150);
                    if (dfd >= 0) {
                        int dir = dir_idx < out_n ? out_dirs[dir_idx++] : 0;
                        data_exchange(dfd, data, size, dir);
                        last_traffic = now_ms();
                    }
                }
            }
        }
        if (listener >= 0) {
            struct pollfd lp = { .fd = listener, .events = POLLIN };
            while (poll(&lp, 1, 0) > 0) {
                int dfd = accept(listener, NULL, NULL);
                if (dfd < 0)
                    break;
                int dir = port_idx < port_n ? port_dirs[port_idx++] : 0;
                data_exchange(dfd, data, size, dir);
                last_traffic = now_ms();
            }
        }
        if (now_ms() - last_traffic > 30)
            break;   /* session quiet: done */
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    opftp_server_t* s = opftp_server_create(NULL);
    const opftp_fs_t* fs = opftp_fs_mem_create();
    if (!s || !fs) {
        if (s) opftp_server_destroy(s);
        if (fs) opftp_fs_mem_destroy(fs);
        return 0;
    }
    opftp_server_set_fs(s, fs);
    opftp_server_set_port(s, 0);          /* ephemeral, no port clashes */
    opftp_server_set_root(s, "/");
    opftp_server_set_workers(s, 1);
    opftp_server_set_stop_timeout(s, 1);  /* seconds: bounded teardown */

    if (opftp_server_start(s) == 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a = {
            .sin_family = AF_INET,
            .sin_port = htons(opftp_server_bound_port(s)),
        };
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int listener = make_listener();
        if (fd >= 0 &&
            connect(fd, (struct sockaddr*) &a, sizeof(a)) == 0) {
            if (size)
                send(fd, data, size, MSG_NOSIGNAL);
            run_session(fd, listener, data, size);
            close(fd);
        }
        if (listener >= 0)
            close(listener);
        opftp_server_stop(s);
    }
    opftp_server_destroy(s);
    opftp_fs_mem_destroy(fs);
    return 0;
}
