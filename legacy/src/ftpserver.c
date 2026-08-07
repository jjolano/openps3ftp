/*
 * Legacy Server facade shim.
 *
 * server_init/run/stop/free map onto the new core:
 *   server_run  = opftp_server_run_loop (blocking, old semantics)
 *   server_stop = opftp_server_stop + drain
 * The legacy struct Server fields (running, port, nfds, pollfds,
 * clients) are maintained as a facade for consumers like main.cpp.
 *
 * Connect/disconnect hooks create and destroy the legacy Client
 * facade (stored in the core client's userdata) and fan out to the
 * legacy connect/disconnect callback arrays.
 */
#include <signal.h>
#include "shim.h"

#include <stdlib.h>
#include <string.h>

static struct shim_server shim_servers[SHIM_MAX_SERVERS];

struct shim_server* shim_server_register(struct Server* legacy,
                                         struct opftp_server* core)
{
    for (int i = 0; i < SHIM_MAX_SERVERS; i++) {
        if (!shim_servers[i].used) {
            shim_servers[i].legacy = legacy;
            shim_servers[i].core = core;
            shim_servers[i].used = true;
            return &shim_servers[i];
        }
    }
    return NULL;
}

struct shim_server* shim_server_find(struct Server* legacy)
{
    for (int i = 0; i < SHIM_MAX_SERVERS; i++)
        if (shim_servers[i].used && shim_servers[i].legacy == legacy)
            return &shim_servers[i];
    return NULL;
}

void shim_server_unregister(struct shim_server* ss)
{
    if (ss)
        ss->used = false;
}

/* ---- facade bookkeeping (pollfds/clients mirror) ---- */

void server_pollfds_add(struct Server* server, int fd, short events)
{
    if(server->mutex != NULL)
    {
        sys_thread_mutex_lock(server->mutex);
    }

    server->pollfds = (struct pollfd*) realloc(server->pollfds, ++server->nfds * sizeof(struct pollfd));

    struct pollfd* pfd = &server->pollfds[server->nfds - 1];

    pfd->fd = fd;
    pfd->events = events;

    if(server->mutex != NULL)
    {
        sys_thread_mutex_unlock(server->mutex);
    }
}

void server_pollfds_remove(struct Server* server, int fd)
{
    if(server->pollfds != NULL)
    {
        if(server->mutex != NULL)
        {
            sys_thread_mutex_lock(server->mutex);
        }

        nfds_t i;

        for(i = 0; i < server->nfds; ++i)
        {
            if(server->pollfds[i].fd == fd)
            {
                break;
            }
        }

        if(i == server->nfds)
        {
            if(server->mutex != NULL)
            {
                sys_thread_mutex_unlock(server->mutex);
            }
            return;
        }

        server->pollfds[i] = server->pollfds[server->nfds - 1];

        server->pollfds = (struct pollfd*) realloc(server->pollfds, --server->nfds * sizeof(struct pollfd));

        if(server->mutex != NULL)
        {
            sys_thread_mutex_unlock(server->mutex);
        }
    }
}

void server_client_add(struct Server* server, int fd, struct Client** client_ptr)
{
    if(server->mutex != NULL)
    {
        sys_thread_mutex_lock(server->mutex);
    }

    if(*client_ptr != NULL)
    {
        avltree_insert(server->clients, fd, *client_ptr);

        if(server->mutex != NULL)
        {
            sys_thread_mutex_unlock(server->mutex);
        }
        return;
    }

    if(server->mutex != NULL)
    {
        sys_thread_mutex_unlock(server->mutex);
    }
}

void server_client_find(struct Server* server, int fd, struct Client** client_ptr)
{
    *client_ptr = NULL;

    if(server->mutex != NULL)
    {
        sys_thread_mutex_lock(server->mutex);
    }

    struct AVLNode* n = avltree_search(server->clients, fd);

    if(n)
    {
        *client_ptr = (struct Client*) n->data_ptr;
    }

    if(server->mutex != NULL)
    {
        sys_thread_mutex_unlock(server->mutex);
    }
}

void server_client_remove(struct Server* server, int fd)
{
    if(server->mutex != NULL)
    {
        sys_thread_mutex_lock(server->mutex);
    }

    avltree_remove(server->clients, fd);

    if(server->mutex != NULL)
    {
        sys_thread_mutex_unlock(server->mutex);
    }
}

/* ---- core callbacks ---- */

static void shim_on_connect(struct opftp_client* core, void* ctx)
{
    struct shim_server* ss = ctx;
    struct Server* server = ss->legacy;

    struct Client* client = (struct Client*) calloc(1, sizeof(struct Client));
    if (!client)
        return;

    client->server_ptr = server;
    client->cvar = pttree_create();
    client->socket_control = ((struct opftp_client*) core)->fd;
    client->socket_data = -1;
    client->socket_pasv = -1;
    client->cb_data = NULL;
    client->lastcmd[0] = '\0';
    client->socket_event = 0;
    client->buffer_control = (char*) malloc(BUFFER_CONTROL * sizeof(char));
    client->buffer_data = (char*) malloc(BUFFER_DATA * sizeof(char));
    client->buffer_command = (char*) malloc(BUFFER_COMMAND * sizeof(char));
    client->mutex = sys_thread_mutex_alloc(1);
    if (!client->cvar || !client->buffer_control || !client->buffer_data ||
        !client->buffer_command || !client->mutex) {
        if (client->cvar) pttree_destroy(client->cvar);
        free(client->buffer_control);
        free(client->buffer_data);
        free(client->buffer_command);
        if (client->mutex) sys_thread_mutex_free(client->mutex);
        free(client);
        return;
    }
    sys_thread_mutex_create(client->mutex);

    if (!shim_client_register(client, core)) {
        sys_thread_mutex_destroy(client->mutex);
        sys_thread_mutex_free(client->mutex);
        pttree_destroy(client->cvar);
        free(client->buffer_control);
        free(client->buffer_data);
        free(client->buffer_command);
        free(client);
        return;
    }

    opftp_client_userdata_set(core, client);

    /* The legacy server had no prelogin gate: handlers enforce auth
     * themselves (base.c checks the "auth" cvar). Bypass the core gate. */
    ((struct opftp_client*) core)->logged_in = true;

    server_client_add(server, client->socket_control, &client);
    server_pollfds_add(server, client->socket_control, POLLIN | POLLRDNORM);

    if (server->command_ptr)
        command_call_connect(server->command_ptr, client);

    /* core banner was skipped; legacy connect callbacks (base_connect)
     * send "220-Welcome" — terminate the multiline reply */
    opftp_client_send_reply(core, 220, "OpenPS3FTP ready.");
}

static void shim_on_disconnect(struct opftp_client* core, void* ctx)
{
    struct shim_server* ss = ctx;
    struct Server* server = ss->legacy;
    struct Client* client = (struct Client*) opftp_client_userdata(core);
    if (!client)
        return;

    /* Stop the data executor FIRST. It is still running the legacy
     * data_callback, which reads the client's cvars — and the legacy
     * disconnect callbacks below free exactly those. Cancelling and
     * waiting here is what keeps that from being a use-after-free. */
    struct shim_client* sc = shim_client_find(client);
    if (sc) {
        sys_thread_mutex_lock(client->mutex);
        sc->cancel = true;
        if (client->socket_data != -1)
            shutdown(client->socket_data, SHUT_RDWR);
        sys_thread_mutex_unlock(client->mutex);

        /* The shutdown above wakes the executor's poll immediately, so
         * this normally returns on the first check. job_active is read
         * without client->mutex on purpose: it is the executor's final
         * release store, published after its last use of `client`.
         * ponytail: bounded spin rather than refcounting the legacy
         * Client — add a condvar if disconnect latency ever matters. */
        for (int i = 0; i < 1000; i++) {
            if (!atomic_load_explicit(&sc->job_active, memory_order_acquire))
                break;
            usleep(2000);            /* ~2s ceiling */
        }
    }

    if (server->command_ptr)
        command_call_disconnect(server->command_ptr, client);

    server_pollfds_remove(server, client->socket_control);
    server_client_remove(server, client->socket_control);
    if (client->socket_pasv != -1) {
        socketclose(client->socket_pasv);
        client->socket_pasv = -1;
    }

    shim_client_unregister(sc);

    sys_thread_mutex_destroy(client->mutex);
    sys_thread_mutex_free(client->mutex);
    client->mutex = NULL;
    client_free(client);
}

/* ---- lifecycle ---- */

/* Runs on the reactor thread during reactor_init (after the listener
 * is bound, before clients are accepted): refreshes the facade port
 * (ephemeral port 0 support) and installs legacy command handlers. */
static void shim_after_commands(struct opftp_server* core, void* ctx)
{
    struct Server* server = ctx;
    server->port = opftp_server_bound_port(core);
    legacy_command_overlay(core, server->command_ptr);
}

void server_init(struct Server* server, struct Command* command_ptr, unsigned short port)
{
    memset(server, 0, sizeof(*server));
    server->command_ptr = command_ptr;
    server->port = port;
    server->running = false;
    server->should_stop = false;
    server->socket = -1;
    server->pollfds = NULL;
    server->nfds = 0;
    server->clients = avltree_create();
    server->pool = threadpool_create(NUM_THREADS);
    server->mutex = sys_thread_mutex_alloc(1);
    if (server->mutex)
        sys_thread_mutex_create(server->mutex);
}

uint32_t server_run(struct Server* server)
{
    opftp_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.connect = shim_on_connect;
    cb.disconnect = shim_on_disconnect;
    cb.skip_banner = true;   /* legacy connect callbacks send the welcome */

    struct opftp_server* core = opftp_server_create(&cb);
    if (!core)
        return 1;

    struct shim_server* ss = shim_server_register(server, core);
    if (!ss) {
        opftp_server_destroy(core);
        return 1;
    }
    ss->legacy = server;

    opftp_server_set_port(core, server->port);
    opftp_server_set_root(core, "/");
    opftp_server_set_workers(core, NUM_THREADS);
    opftp_server_set_stop_timeout(core, 5);

    /* legacy registrations take precedence over core defaults; also
     * refresh the facade port (ephemeral support) after binding */
    ((struct opftp_server*) core)->cb.connect_ctx = ss;
    ((struct opftp_server*) core)->cb.disconnect_ctx = ss;
    ((struct opftp_server*) core)->cb.after_commands = shim_after_commands;
    ((struct opftp_server*) core)->cb.after_commands_ctx = server;

#ifndef OPFTP_PS3
    /* The new core sends with MSG_NOSIGNAL everywhere, but the legacy
     * data_callbacks we are about to run are old consumer code that
     * calls plain send(). Writing to a socket the peer just closed —
     * or that a disconnect shut down mid-transfer — then kills the
     * whole process with SIGPIPE. Ignoring it makes those writes fail
     * with EPIPE instead, which the callbacks already handle.
     * (No SIGPIPE on ps3.) */
    signal(SIGPIPE, SIG_IGN);
#endif

    server->running = true;
    server->should_stop = false;

    if (server->pool)
        threadpool_start(server->pool);

    int rc = opftp_server_run_loop(core);

    server->running = false;

    if (server->pool && server->pool->running)
        threadpool_stop(server->pool);

    opftp_server_destroy(core);
    shim_server_unregister(ss);
    return rc == 0 ? 0 : 1;
}

void server_stop(struct Server* server)
{
    server->should_stop = true;
    struct shim_server* ss = shim_server_find(server);
    if (ss)
        opftp_server_stop(ss->core);
}

void server_free(struct Server* server)
{
    if (server->pollfds != NULL)
        free(server->pollfds);
    server->pollfds = NULL;
    server->nfds = 0;

    if (server->clients != NULL)
        avltree_destroy(server->clients);
    server->clients = NULL;

    if (server->pool != NULL)
        threadpool_destroy(server->pool);
    server->pool = NULL;

    if (server->mutex != NULL) {
        sys_thread_mutex_destroy(server->mutex);
        sys_thread_mutex_free(server->mutex);
    }
    server->mutex = NULL;

    shim_server_unregister(shim_server_find(server));
}
