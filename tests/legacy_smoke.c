/*
 * Legacy API smoke test (host).
 *
 * Uses the OLD OpenPS3FTP API unchanged — the same sequence the legacy
 * PS3 app used: command_init, the feat/base/ext/site command imports,
 * server_init, server_run on a thread. The python driver
 * (test_legacy_ftplib.py) connects with ftplib and exercises
 * login, LIST, RETR, STOR, PASV/PORT, SIZE, MDTM, ABOR, QUIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "server.h"
#include "client.h"
#include "command.h"

#include "base/base.h"
#include "ext/ext.h"
#include "site/site.h"
#include "feat/feat.h"

static struct Server g_server;

static void* server_thread(void* arg)
{
    (void) arg;
    uint32_t rc = server_run(&g_server);
    fprintf(stderr, "server_run returned %u\n", rc);
    return NULL;
}

static void on_signal(int sig)
{
    (void) sig;
    server_stop(&g_server);
}

int main(int argc, char** argv)
{
    unsigned short port = (argc > 1) ? (unsigned short) atoi(argv[1]) : 0;

    struct Command ftp_command;
    command_init(&ftp_command);
    feat_command_import(&ftp_command);
    base_command_import(&ftp_command);
    ext_command_import(&ftp_command);
    site_command_import(&ftp_command);

    server_init(&g_server, &ftp_command, port);

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    pthread_t tid;
    if (pthread_create(&tid, NULL, server_thread, NULL) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        return 1;
    }

    /* wait for the listener to come up (the shim refreshes
     * g_server.port with the bound port), then report it */
    int waited = 0;
    while ((!g_server.running || g_server.port == 0) && waited < 100) {
        usleep(100000);
        waited++;
    }
    printf("PORT %u\n", g_server.port);
    fflush(stdout);

    while (g_server.running)
        usleep(100000);

    pthread_join(tid, NULL);
    server_free(&g_server);
    command_free(&ftp_command);
    return 0;
}
