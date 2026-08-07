/*
 * Legacy Client facade shim.
 *
 * The legacy struct Client is a facade mirroring one core connection:
 * cvars live in its PTTree (unchanged legacy semantics), sends go
 * through the core client, and data transfers run legacy callbacks on
 * a worker (the legacy ThreadPool) via the data-callback executor.
 *
 * Threading: all legacy Client state is touched either on the reactor
 * thread (command handlers via the dispatch adapter) or by the data
 * executor on a pool worker. The legacy client->mutex serializes the
 * data callback against ABOR/disconnect.
 */
#include "shim.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static struct shim_client shim_clients[SHIM_MAX_CLIENTS];
static int shim_client_count;

struct shim_client* shim_client_register(struct Client* legacy,
                                         struct opftp_client* core)
{
    if (shim_client_count >= SHIM_MAX_CLIENTS)
        return NULL;
    struct shim_client* sc = &shim_clients[shim_client_count++];
    sc->legacy = legacy;
    sc->core = core;
    sc->used = true;
    sc->job_active = false;
    sc->cancel = false;
    return sc;
}

struct shim_client* shim_client_find(struct Client* legacy)
{
    for (int i = 0; i < shim_client_count; i++)
        if (shim_clients[i].used && shim_clients[i].legacy == legacy)
            return &shim_clients[i];
    return NULL;
}

struct shim_client* shim_client_find_core(struct opftp_client* core)
{
    for (int i = 0; i < shim_client_count; i++)
        if (shim_clients[i].used && shim_clients[i].core == core)
            return &shim_clients[i];
    return NULL;
}

void shim_client_unregister(struct shim_client* sc)
{
    if (sc)
        sc->used = false;
}

/* ---- cvar ---- */

void* client_get_cvar(struct Client* client, const char* name)
{
    struct PTNode* n = pttree_search(client->cvar, name);
    return n != NULL ? n->data_ptr : NULL;
}

void client_set_cvar(struct Client* client, const char* name, void* ptr)
{
    pttree_insert(client->cvar, name, ptr);
}

/* ---- control sends (reactor thread, or executor with mutex held) ---- */

/* Send "code-msg\r\n" (multi-line intro) or " msg\r\n" continuation.
 * Returns 0 on success. */
static int send_raw_line(struct Client* client, const char* s)
{
    struct shim_client* sc = shim_client_find(client);
    if (!sc || !sc->core)
        return -1;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s\r\n", s);
    return opftp_client_send_raw(sc->core, buf);
}

void client_send_message(struct Client* client, const char* message)
{
    struct shim_client* sc = shim_client_find(client);
    if (sc)
        opftp_client_send(sc->core, message);
}

void client_send_code(struct Client* client, int code, const char* message)
{
    struct shim_client* sc = shim_client_find(client);
    if (sc) {
        /* The legacy command handler sends 150 AFTER client_data_start
         * returns. The executor runs on a worker and could finish the
         * whole transfer (and send 226) before the reactor sends 150.
         * Gate the executor on the 150 so the reply order stays
         * 150 -> data -> 226 like the old single-threaded server. */
        if (code == 150) {
            sys_thread_mutex_lock(client->mutex);
            sc->data_go = true;
            sys_thread_mutex_unlock(client->mutex);
        }
        opftp_client_send_reply(sc->core, code, message);
    }
}

void client_send_multicode(struct Client* client, int code, const char* message)
{
    char buf[1056];
    snprintf(buf, sizeof(buf), "%d-%s", code, message ? message : "");
    send_raw_line(client, buf);
}

void client_send_multimessage(struct Client* client, const char* message)
{
    char buf[1056];
    snprintf(buf, sizeof(buf), " %s", message ? message : "");
    send_raw_line(client, buf);
}

/* ---- data channel: legacy event-driven callbacks on a worker ---- */

struct data_job {
    struct Client* client;
    struct shim_client* sc;
    short events;
};

/* The legacy data callback does one unit of work per poll readiness and
 * returns true when the transfer is done. The executor replicates the
 * old server loop: poll the data fd, run the callback, repeat. */
static void data_executor(void* arg)
{
    struct data_job* job = arg;
    struct Client* client = job->client;
    struct shim_client* sc = job->sc;
    short events = job->events;
    int fd = client->socket_data;
    int loop = 0;

    /* Wait for the 150 reply to be sent before running the callback,
     * so the reply order matches the old single-threaded server
     * (150 -> data -> 226). Bounded: give up if cancelled. */
    for (;;) {
        sys_thread_mutex_lock(client->mutex);
        bool go = sc->data_go || sc->cancel;
        sys_thread_mutex_unlock(client->mutex);
        if (go)
            break;
        usleep(1000);
    }

    for (;;) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;

        int r = poll(&pfd, 1, 1000);
        loop++;
        if (r < 0)
            { break; }
        if (r == 0) {
            /* timeout: re-check cancellation */
            sys_thread_mutex_lock(client->mutex);
            bool cancel = sc->cancel || client->cb_data == NULL;
            sys_thread_mutex_unlock(client->mutex);
            if (cancel)
                { break; }
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            sys_thread_mutex_lock(client->mutex);
            bool cancel = sc->cancel || client->cb_data == NULL;
            sys_thread_mutex_unlock(client->mutex);
            if (cancel)
                { break; }
            sys_thread_mutex_lock(client->mutex);
            bool done = client->cb_data ? client->cb_data(client) : true;
            sys_thread_mutex_unlock(client->mutex);
            if (done)
                break;
            continue;
        }
        if (!(pfd.revents & events)) {
            continue;
        }

        sys_thread_mutex_lock(client->mutex);
        bool done = client->cb_data ? client->cb_data(client) : true;
        sys_thread_mutex_unlock(client->mutex);
        if (done)
            break;
    }

    /* executor owns the data socket and the job */
    sys_thread_mutex_lock(client->mutex);
    if (client->socket_data != -1) {
        socketclose(client->socket_data);
        client->socket_data = -1;
    }
    client->cb_data = NULL;
    sc->job_active = false;
    sys_thread_mutex_unlock(client->mutex);

    /* legacy bookkeeping: drop the data fd from the facade */
    server_pollfds_remove(client->server_ptr, fd);
    server_client_remove(client->server_ptr, fd);

    free(job);
}

/* Establish the data connection (PASV accept or PORT connect), then
 * hand the fd to the executor. Mirrors the old client_data_start. */
bool client_data_start(struct Client* client, data_callback callback, short pevents)
{
    struct shim_client* sc = shim_client_find(client);
    if (!sc || !client->server_ptr || !client->server_ptr->pool)
        return false;

    sys_thread_mutex_lock(client->mutex);
    if (client->cb_data != NULL) {      /* transfer already active */
        sys_thread_mutex_unlock(client->mutex);
        return false;
    }

    if (client->socket_data == -1) {
        if (client->socket_pasv == -1) {
            /* active mode: connect to the PORT target (or peer:20) */
            void* cvar_ptr = client_get_cvar(client, "port_addr");
            struct sockaddr_in active_addr;
            socklen_t len = sizeof(struct sockaddr_in);

            if (cvar_ptr != NULL) {
                struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
                memcpy(&active_addr, port_addr, len);
                free(port_addr);
                client_set_cvar(client, "port_addr", NULL);
            } else {
                getpeername(client->socket_control, (struct sockaddr*) &active_addr, &len);
                active_addr.sin_port = htons(20);
            }

            client->socket_data = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (client->socket_data < 0) {
                client->socket_data = -1;
                sys_thread_mutex_unlock(client->mutex);
                return false;
            }

            struct timeval opttv;
            opttv.tv_sec = 5;
            opttv.tv_usec = 0;
            setsockopt(client->socket_data, SOL_SOCKET, SO_SNDTIMEO, &opttv, sizeof(opttv));

            if (connect(client->socket_data, (struct sockaddr*) &active_addr, len) != 0) {
                socketclose(client->socket_data);
                client->socket_data = -1;
                sys_thread_mutex_unlock(client->mutex);
                return false;
            }
        } else {
            /* passive mode: accept on the PASV listener */
            struct pollfd pasv_pollfd;
            pasv_pollfd.fd = client->socket_pasv;
            pasv_pollfd.events = POLLIN;

            int p = poll(&pasv_pollfd, 1, 5000);

            if (p <= 0) {
                socketclose(client->socket_pasv);
                client->socket_pasv = -1;
                sys_thread_mutex_unlock(client->mutex);
                return false;
            }

            client->socket_data = accept(client->socket_pasv, NULL, NULL);

            socketclose(client->socket_pasv);
            client->socket_pasv = -1;

            if (client->socket_data < 0) {
                sys_thread_mutex_unlock(client->mutex);
                return false;
            }
        }
    }

    int optval = 1;
    setsockopt(client->socket_data, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

    server_pollfds_add(client->server_ptr, client->socket_data, pevents | POLLIN);
    server_client_add(client->server_ptr, client->socket_data, &client);

    client->cb_data = callback;
    sc->events = pevents;
    sc->cancel = false;
    sc->data_go = false;

    struct data_job* job = malloc(sizeof(*job));
    if (!job) {
        client->cb_data = NULL;
        sys_thread_mutex_unlock(client->mutex);
        return false;
    }
    job->client = client;
    job->sc = sc;
    job->events = pevents;
    sc->job_active = true;

    sys_thread_mutex_unlock(client->mutex);

    threadpool_dispatch(client->server_ptr->pool, data_executor, job);
    return true;
}

void client_data_end(struct Client* client)
{
    struct shim_client* sc = shim_client_find(client);
    if (!sc)
        return;

    sys_thread_mutex_lock(client->mutex);
    if (sc->job_active) {
        /* cancel: shutdown the data socket; the executor wakes with
         * POLLERR/HUP, the callback exits, the executor cleans up */
        sc->cancel = true;
        if (client->socket_data != -1)
            shutdown(client->socket_data, SHUT_RDWR);
    } else if (client->socket_data != -1) {
        socketclose(client->socket_data);
        client->socket_data = -1;
        client->cb_data = NULL;
        server_pollfds_remove(client->server_ptr, client->socket_data);
        server_client_remove(client->server_ptr, client->socket_data);
    }
    sys_thread_mutex_unlock(client->mutex);
}

bool client_pasv_enter(struct Client* client, struct sockaddr_in* pasv_addr)
{
    void* cvar_ptr = client_get_cvar(client, "port_addr");

    if(cvar_ptr != NULL)
    {
        struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
        free(port_addr);
        client_set_cvar(client, "port_addr", NULL);
    }

    if(client->socket_pasv != -1)
    {
        socketclose(client->socket_pasv);
    }

    client->socket_pasv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(client->socket_pasv < 0)
    {
        client->socket_pasv = -1;
        return false;
    }

    int optval = 1;
    setsockopt(client->socket_pasv, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    setsockopt(client->socket_pasv, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    socklen_t len = sizeof(struct sockaddr_in);

    /* The legacy PASV listener is IPv4-only. The new core control
     * socket is dual-stack, so its local address may come back as a
     * v4-mapped IPv6 address — extract the IPv4 form. */
    getsockname(client->socket_control, (struct sockaddr*) pasv_addr, &len);

    if (pasv_addr->sin_family == AF_INET6) {
        struct sockaddr_in6 a6;
        socklen_t l6 = sizeof(a6);
        if (getsockname(client->socket_control, (struct sockaddr*) &a6, &l6) == 0 &&
            opftp_is_v4mapped(&a6.sin6_addr)) {
            memset(pasv_addr, 0, sizeof(*pasv_addr));
            pasv_addr->sin_family = AF_INET;
            memcpy(&pasv_addr->sin_addr, &a6.sin6_addr.s6_addr[12], 4);
            len = sizeof(*pasv_addr);
        }
    }

    pasv_addr->sin_port = 0;

    if(bind(client->socket_pasv, (struct sockaddr*) pasv_addr, len) != 0)
    {
        fprintf(stderr, "pasv: bind failed errno=%d (%s)\n", errno, strerror(errno));
        socketclose(client->socket_pasv);
        client->socket_pasv = -1;
        return false;
    }

    listen(client->socket_pasv, 1);

    getsockname(client->socket_pasv, (struct sockaddr*) pasv_addr, &len);

    return true;
}

/* ---- legacy event entry points (kept for API compatibility) ---- */

int client_socket_event(struct Client* client, int socket_ev)
{
    if (socket_ev == -1)
        return 0;

    if (socket_ev == client->socket_data) {
        if (client->cb_data != NULL) {
            bool ended = (*client->cb_data)(client);
            if (ended) {
                client_data_end(client);
                return 0;
            }
        } else {
            client_data_end(client);
            return 0;
        }
        return 0;
    }

    /* control socket events are handled by the core reactor */
    return 0;
}

void client_socket_disconnect(struct Client* client, int socket_dc)
{
    if (socket_dc == -1)
        return;

    if (socket_dc == client->socket_data) {
        client_data_end(client);
        return;
    }

    if (socket_dc == client->socket_control) {
        client_data_end(client);
        if (client->socket_pasv != -1) {
            socketclose(client->socket_pasv);
            client->socket_pasv = -1;
        }
        struct shim_client* sc = shim_client_find(client);
        if (sc && sc->core)
            opftp_client_disconnect(sc->core);
    }
}

void client_free(struct Client* client)
{
    if (!client)
        return;

    if (client->cvar != NULL)
        pttree_destroy(client->cvar);

    if (client->mutex != NULL) {
        sys_thread_mutex_destroy(client->mutex);
        sys_thread_mutex_free(client->mutex);
    }

    free(client->buffer_control);
    free(client->buffer_data);
    free(client->buffer_command);
    free(client);
}
