/* Private shim-internal declarations (legacy/src only). */
#ifndef LEGACY_SHIM_H
#define LEGACY_SHIM_H

#include "server.h"
#include "client.h"
#include "command.h"
#include "sys.thread.h"
#include "opftp.h"

/* facade -> core client side table (legacy struct has no spare field) */
#define SHIM_MAX_CLIENTS 64

struct shim_client {
    struct Client* legacy;
    struct opftp_client* core;
    bool used;
    /* data executor state (reactor/worker, guarded by legacy mutex) */
    /* job_active is NOT mutex-guarded: it is the handshake that tells a
     * disconnecting reactor the executor has stopped touching `client`
     * (and client->mutex, which the disconnect destroys). The executor
     * publishes false as its very last action. */
    atomic_bool job_active;  /* executor running */
    bool cancel;             /* ABOR/disconnect requested */
    bool data_go;            /* 150 sent: executor may start the callback */
    short events;
};

#define SHIM_MAX_SERVERS 4

struct shim_server {
    struct Server* legacy;
    struct opftp_server* core;
    bool used;
};

/* ftpcommand.c */
void legacy_command_overlay(struct opftp_server* core, struct Command* cmd);

/* ftpclient.c */
struct shim_client* shim_client_register(struct Client* legacy,
                                         struct opftp_client* core);
struct shim_client* shim_client_find(struct Client* legacy);
struct shim_client* shim_client_find_core(struct opftp_client* core);
void shim_client_unregister(struct shim_client* sc);

/* ftpserver.c */
struct shim_server* shim_server_register(struct Server* legacy,
                                         struct opftp_server* core);
struct shim_server* shim_server_find(struct Server* legacy);
void shim_server_unregister(struct shim_server* ss);

#endif /* LEGACY_SHIM_H */
