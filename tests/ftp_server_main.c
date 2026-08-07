/*
 * Test driver: runs an opftp server on an ephemeral port, prints
 * "PORT <n>" on stdout, and shuts down gracefully on SIGTERM/SIGINT.
 * Usage: ftp_server_main <root> [port] [workers] [cert.pem] [key.pem] [require_tls] [allow_stop]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "openps3ftp/openps3ftp.h"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void) sig;
    g_stop = 1;
}

static char* read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t) n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t) n, f) != (size_t) n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : "/tmp";
    unsigned port = argc > 2 ? (unsigned) atoi(argv[2]) : 0;
    int workers = argc > 3 ? atoi(argv[3]) : 2;
    /* empty strings are placeholders so later args stay positional */
    const char* cert_path = (argc > 4 && argv[4][0]) ? argv[4] : NULL;
    const char* key_path = (argc > 5 && argv[5][0]) ? argv[5] : NULL;
    bool require_tls = argc > 6 && atoi(argv[6]) != 0;
    bool allow_stop = argc > 7 && atoi(argv[7]) != 0;

    opftp_server_t* s = opftp_server_create(NULL);
    if (!s) { fprintf(stderr, "create failed\n"); return 1; }
    opftp_server_set_port(s, (uint16_t) port);
    opftp_server_set_root(s, root);
    opftp_server_set_workers(s, workers);
    opftp_server_set_stop_timeout(s, 3);
    if (cert_path && key_path) {
        char* cert = read_file(cert_path);
        char* key = read_file(key_path);
        if (!cert || !key) {
            fprintf(stderr, "read cert/key failed\n");
            return 1;
        }
        int rc = opftp_server_set_tls(s, cert, key);
        free(cert);
        free(key);
        if (rc != 0) {
            fprintf(stderr, "set_tls failed\n");
            return 1;
        }
    }
    opftp_server_set_require_tls(s, require_tls);
    opftp_server_set_allow_stop(s, allow_stop);

    if (opftp_server_start(s) != 0) {
        fprintf(stderr, "start failed\n");
        return 1;
    }
    printf("PORT %u\n", opftp_server_bound_port(s));
    fflush(stdout);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    /* Exit on a signal, or when the server stopped itself (STOP). */
    for (;;) {
        opftp_snapshot_t snap;
        if (g_stop)
            break;
        if (opftp_server_snapshot(s, &snap) == 0 && !snap.started)
            break;
        usleep(100000);
    }

    opftp_server_stop(s);
    opftp_server_destroy(s);
    return 0;
}
