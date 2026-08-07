/*
 * Server lifecycle + reactor loop.
 *
 * The reactor thread owns: the listener, all control sockets, command
 * parsing and replies, the client list, and job bookkeeping. Workers
 * own transfers only (see datachan.c / transfer.c).
 */
#include "opftp.h"
#include "tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#ifdef OPFTP_PS3
#include <net/poll.h>
#else
#include <poll.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>



void opftp_log(struct opftp_server* s, int level, const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s && s->cb.log)
        s->cb.log(level, buf);
    else if (!s || level <= s->cb.log_level)
        fprintf(stderr, "[opftp] %s\n", buf);
}

/* ---- lifecycle ---- */

opftp_server_t* opftp_server_create(const opftp_callbacks_t* cb)
{
    struct opftp_server* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (cb)
        s->cb = *cb;
    s->port = 2121;
    s->workers_req = 2;
    s->stop_timeout_ms = 5000;
    strcpy(s->root, "/");
#ifdef OPFTP_PS3
    s->fs_base = opftp_fs_ps3();
#else
    s->fs_base = &opftp_fs_posix;
#endif
    s->listen_fd = -1;
    atomic_init(&s->started, false);
    atomic_init(&s->stopping, false);
    atomic_init(&s->reactor_running, false);
    atomic_init(&s->reactor_done, false);
    s->life_mutex = opftp_mutex_create();
    s->life_cond = opftp_cond_create(s->life_mutex);
    s->snap_mutex = opftp_mutex_create();
    if (!s->life_mutex || !s->life_cond || !s->snap_mutex) {
        if (s->life_mutex) opftp_mutex_destroy(s->life_mutex);
        if (s->life_cond) opftp_cond_destroy(s->life_cond);
        if (s->snap_mutex) opftp_mutex_destroy(s->snap_mutex);
        free(s);
        return NULL;
    }
    return s;
}

void opftp_server_set_port(opftp_server_t* s, uint16_t port)
{
    s->port = port;
    s->port_ephemeral = (port == 0);
}

void opftp_server_set_fs(opftp_server_t* s, const opftp_fs_t* fs)
{
    if (fs) s->fs_base = fs;
}

void opftp_server_set_root(opftp_server_t* s, const char* root)
{
    if (!root || root[0] != '/')
        return;
    snprintf(s->root, sizeof(s->root), "%s", root);
}

void opftp_server_set_workers(opftp_server_t* s, int n)
{
    if (n > 0 && n <= 64)
        s->workers_req = n;
}

void opftp_server_set_stop_timeout(opftp_server_t* s, int seconds)
{
    s->stop_timeout_ms = seconds > 0 ? seconds * 1000 : 1000;
}

int opftp_server_set_tls(opftp_server_t* s, const char* cert_pem, const char* key_pem)
{
    if (!cert_pem || !key_pem)
        return -EINVAL;
    struct opftp_tls_ctx* ctx = NULL;
    int rc = opftp_tls_ctx_init(&ctx, cert_pem, key_pem);
    if (rc != 0)
        return rc;
    if (s->tls)
        opftp_tls_ctx_free(s->tls);
    s->tls = ctx;
    s->tls_enabled = true;
    return 0;
}

void opftp_server_set_require_tls(opftp_server_t* s, bool on)
{
    s->require_tls = on;
}

void opftp_server_set_allow_foreign_port(opftp_server_t* s, bool on)
{
    s->allow_foreign_port = on;
}

uint16_t opftp_server_bound_port(opftp_server_t* s)
{
    return s->port;
}

/* ---- listener ---- */

static int bind_listener(struct opftp_server* s)
{
    /* Dual-stack: prefer AF_INET6 with V4MAPPED (accepts both v4 and
     * v6), fall back to AF_INET if IPv6 is unavailable. */
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    bool is_v6 = (fd >= 0);
    if (is_v6) {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef IPV6_V6ONLY
        int zero = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
#endif
        struct sockaddr_in6 a6 = {0};
        a6.sin6_family = AF_INET6;
        a6.sin6_addr = in6addr_any;
        a6.sin6_port = htons(s->port);
        if (bind(fd, (struct sockaddr*) &a6, sizeof(a6)) == 0 &&
            listen(fd, 16) == 0) {
            socklen_t sl = sizeof(a6);
            getsockname(fd, (struct sockaddr*) &a6, &sl);
            s->port = ntohs(a6.sin6_port);
            goto done;
        }
        int e = errno;
        opftp_close_fd(fd);
        (void) e;   /* fall back to IPv4 */
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -errno;
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a4 = {0};
        a4.sin_family = AF_INET;
        a4.sin_addr.s_addr = htonl(INADDR_ANY);
        a4.sin_port = htons(s->port);
        if (bind(fd, (struct sockaddr*) &a4, sizeof(a4)) != 0) {
            int e = errno;
            opftp_close_fd(fd);
            return -e;
        }
        if (listen(fd, 16) != 0) {
            int e = errno;
            opftp_close_fd(fd);
            return -e;
        }
        socklen_t sl = sizeof(a4);
        getsockname(fd, (struct sockaddr*) &a4, &sl);
        s->port = ntohs(a4.sin_port);
    }
done:
    s->listen_fd = fd;
    (void) is_v6;
    /* non-blocking: the accept loop relies on EAGAIN */
    opftp_set_nonblock(fd);
    return 0;
}

/* ---- client helpers ---- */

static void client_disconnect(struct opftp_server* s, struct opftp_client* c)
{
    if (c->poll_handle >= 0)
        opftp_pollset_remove(s->pollset, c->poll_handle);
    c->poll_handle = -1;

    /* cancel any active transfer; the worker aborts and posts a
     * stale-generation completion which the reactor drops */
    if (c->job) {
        atomic_store_explicit(&c->job->cancelled, true, memory_order_relaxed);
#ifndef OPFTP_PS3
        char ch = 'x';
        ssize_t w = write(c->job->cancel_pipe[1], &ch, 1);
        (void) w;
#endif
        c->job = NULL;
    }
    c->generation++;      /* stale any in-flight job completions */

    if (c->pasv_fd >= 0) {
        opftp_close_fd(c->pasv_fd);
        c->pasv_fd = -1;
    }
    if (c->tls) {
        opftp_tls_session_free(c->tls);
        c->tls = NULL;
        c->tls_handshaking = false;
    }
    opftp_close_fd(c->fd);
    c->fd = -1;

    /* unlink from reactor list */
    struct opftp_client** pp = &s->clients;
    while (*pp && *pp != c) pp = &(*pp)->next;
    if (*pp) *pp = c->next;

    if (s->cb.disconnect)
        s->cb.disconnect(c, s->cb.disconnect_ctx);

    opftp_client_release(c);   /* drop the reactor's ref */
}

static void accept_clients(struct opftp_server* s)
{
    for (;;) {
        opftp_sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        int fd = accept(s->listen_fd, (struct sockaddr*) &ss, &sl);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            if (errno == EINTR)
                continue;
            return;
        }
        int fl = opftp_set_nonblock(fd);
        (void) fl;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &one, sizeof(one));

        struct opftp_client* c = opftp_client_new(s, fd,
                                                  (const struct sockaddr*) &ss, sl);
        if (!c) { opftp_close_fd(fd); continue; }
        c->poll_handle = opftp_pollset_add(s->pollset, fd,
                                           POLLIN | POLLPRI, c);
        if (c->poll_handle < 0) {
            opftp_close_fd(fd);
            c->fd = -1;
            opftp_client_release(c);
            continue;
        }
        c->next = s->clients;
        s->clients = c;

        if (!s->cb.skip_banner)
            opftp_client_send_reply(c, 220, "OpenPS3FTP ready.");

        if (s->require_tls) {
            opftp_client_send_reply(c, 534, "AUTH TLS required.");
            c->disconnect_requested = true;
        }

        if (s->cb.connect)
            s->cb.connect(c, s->cb.connect_ctx);
    }
}

/* ---- command parsing ---- */

static bool prelogin_command(const char* name)
{
    static const char* const ok[] = {
        "USER", "PASS", "QUIT", "HELP", "FEAT", "SYST", "NOOP",
        "OPTS", "AUTH", "PBSZ", "PROT", NULL
    };
    for (int i = 0; ok[i]; i++)
        if (strcmp(ok[i], name) == 0)
            return true;
    return false;
}

static void process_line(struct opftp_server* s, struct opftp_client* c,
                         const char* line)
{
    const char* p = line;
    while (*p == ' ') p++;
    char name[8];
    size_t i = 0;
    while (*p && *p != ' ' && i + 1 < sizeof(name))
        name[i++] = (char) toupper((unsigned char) *p++);
    name[i] = '\0';
    if (name[0] == '\0')
        return;
    const char* param = p;
    while (*param == ' ') param++;

    if (!c->logged_in && !prelogin_command(name)) {
        opftp_client_send_reply(c, 530, "Not logged in.");
        return;
    }
    if (opftp_dispatch_call(s, c, name, param) != 0)
        opftp_client_send_reply(c, 502, "Command not implemented.");
}

static void handle_client_events(struct opftp_server* s, struct opftp_client* c,
                                 short revents)
{
    /* TLS control handshake: advance on POLLIN/POLLOUT; no command
     * parsing until it completes (RFC 4217). */
    if (c->tls_handshaking) {
        int r = opftp_tls_handshake(c->tls);
        if (r == 0) {
            c->tls_handshaking = false;
            /* the 234 was already sent by cmd_auth (pre-handshake) */
            if (s->pollset && c->poll_handle >= 0)
                opftp_pollset_mod(s->pollset, c->poll_handle,
                                  POLLIN | POLLPRI);
        } else if (r == 1 || r == 2) {
            /* poll only for what the handshake wants — POLLOUT is
             * always ready on an idle socket and would busy-loop */
            short want = (r == 2) ? POLLOUT : POLLIN;
            if (s->pollset && c->poll_handle >= 0)
                opftp_pollset_mod(s->pollset, c->poll_handle,
                                  want | POLLPRI);
        } else {
            c->disconnect_requested = true;   /* error or deadline */
        }
        if (c->disconnect_requested)
            client_disconnect(s, c);
        return;
    }

    if (revents & POLLPRI) {
        /* OOB byte (ftplib sends ABOR out-of-band) */
        opftp_dispatch_call(s, c, "ABOR", NULL);
    }
    if (revents & POLLIN) {
        ssize_t n = 0;
        for (;;) {
            n = opftp_client_read(c);
            if (n < 0)
                break;               /* EAGAIN (-2) or error (-1) */
            if (n == 0) {            /* EOF */
                c->disconnect_requested = true;
                break;
            }
            char line[1024];
            while (opftp_client_getline(c, line, sizeof(line))) {
                process_line(s, c, line);
                if (c->disconnect_requested)
                    break;
                if (c->tls_handshaking)
                    break;   /* AUTH TLS: handshake bytes must stay in
                              * the socket/pending, not the read buffer */
            }
            if (c->disconnect_requested || c->tls_handshaking)
                break;
        }
        if (n < 0 && n != -2)
            c->disconnect_requested = true;
    }
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
        c->disconnect_requested = true;
    }

    if (c->disconnect_requested)
        client_disconnect(s, c);
}

/* ---- status snapshot (reactor refreshes; readers take snap_mutex) ---- */

static const char* job_op_name(enum opftp_job_op op)
{
    switch (op) {
    case OPFTP_JOB_LIST: return "LIST";
    case OPFTP_JOB_RETR: return "RETR";
    case OPFTP_JOB_STOR: return "STOR";
    case OPFTP_JOB_APPE: return "APPE";
    case OPFTP_JOB_COPY: return "COPY";
    }
    return "?";
}

/* Format a peer address for display ("192.168.1.50" / "fe80::1").
 * v4-mapped v6 peers display as their v4 form. */
static void format_peer(const struct opftp_client* c, char* out, size_t n)
{
    const opftp_sockaddr_storage* p = &c->peer;
    sa_family_t fam = opftp_sockaddr_family(p);
    if (fam == AF_INET6 && opftp_is_v4mapped(&((const struct sockaddr_in6*) p)->sin6_addr)) {
        const unsigned char* b = ((const struct sockaddr_in6*) p)->sin6_addr.s6_addr;
        snprintf(out, n, "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
        return;
    }
    if (fam == AF_INET) {
        const unsigned char* b = (const unsigned char*) &((const struct sockaddr_in*) p)->sin_addr;
        snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        return;
    }
    if (fam == AF_INET6) {
        const char* s = inet_ntop(AF_INET6, &((const struct sockaddr_in6*) p)->sin6_addr,
                                  out, (socklen_t) n);
        if (s)
            return;
    }
    snprintf(out, n, "?");
}

/* Reactor thread: republish the snapshot cache. Cheap (client count is
 * small); called once per reactor loop iteration. Builds into a local
 * and publishes under the mutex so readers never see partial writes. */
static void snapshot_refresh(struct opftp_server* s)
{
    struct opftp_snapshot tmp;
    memset(&tmp, 0, sizeof(tmp));
    int n = 0;
    for (struct opftp_client* cl = s->clients; cl && n < OPFTP_SNAPSHOT_MAX_CLIENTS;
         cl = cl->next, n++) {
        struct opftp_snapshot_client* sc = &tmp.clients[n];
        format_peer(cl, sc->peer, sizeof(sc->peer));
        snprintf(sc->user, sizeof(sc->user), "%s", cl->user);
        snprintf(sc->cwd, sizeof(sc->cwd), "%s", cl->cwd);
        sc->logged_in = cl->logged_in;
        if (cl->job) {
            sc->xfer_active = true;
            snprintf(sc->xfer_op, sizeof(sc->xfer_op), "%s",
                     job_op_name(cl->job->op));
            snprintf(sc->xfer_path, sizeof(sc->xfer_path), "%s", cl->job->path);
            sc->xfer_bytes = atomic_load_explicit(&cl->job->bytes,
                                                  memory_order_relaxed);
            sc->xfer_total = cl->job->total;
        }
    }
    tmp.started = atomic_load_explicit(&s->started, memory_order_acquire);
    tmp.port = s->port;
    snprintf(tmp.root, sizeof(tmp.root), "%s", s->root);
    tmp.workers = s->workers_req;
    tmp.num_clients = n;
    opftp_mutex_lock(s->snap_mutex);
    s->snap_cache = tmp;
    opftp_mutex_unlock(s->snap_mutex);
}

int opftp_server_snapshot(opftp_server_t* s, opftp_snapshot_t* out)
{
    if (!s || !out)
        return -EINVAL;
    opftp_mutex_lock(s->snap_mutex);
    *out = s->snap_cache;
    opftp_mutex_unlock(s->snap_mutex);
    return 0;
}

/* ---- completion draining ---- */

static void drain_completions(struct opftp_server* s)
{
    struct opftp_transfer_job* head = NULL;
    opftp_mutex_lock(s->compl_mutex);
    head = s->completions;
    s->completions = NULL;
    opftp_mutex_unlock(s->compl_mutex);

    while (head) {
        struct opftp_transfer_job* j = head;
        head = j->compl_next;
        opftp_datachan_complete(s, j);
    }
}

/* ---- reactor loop ---- */

void opftp_reactor_loop(struct opftp_server* s)
{
    opftp_pollset_t* ps = opftp_pollset_create();
    if (!ps)
        return;

    /* publish the pollset under the completion lock so worker wakes
     * (which read s->pollset under the same lock) see it or skip it */
    opftp_mutex_lock(s->compl_mutex);
    s->pollset = ps;
    opftp_mutex_unlock(s->compl_mutex);

    s->poll_handle_listener = opftp_pollset_add(ps, s->listen_fd, POLLIN, NULL);
    s->clients = NULL;

    while (!atomic_load_explicit(&s->stopping, memory_order_relaxed)) {
        /* Refresh the snapshot at ~10Hz while any transfer is active
         * so status UIs see live progress; idle keeps the 1s poll. */
        int timeout = 1000;
        for (struct opftp_client* cl = s->clients; cl; cl = cl->next) {
            if (cl->job) { timeout = 100; break; }
        }
        int n = opftp_pollset_wait(ps, timeout);
        drain_completions(s);
        snapshot_refresh(s);
        if (n < 0)
            break;
        for (int i = 0; i < n; i++) {
            void* user = opftp_pollset_event_user(ps, i);
            short ev = opftp_pollset_event_events(ps, i);
            if (user == NULL) {
                if (ev & POLLIN)
                    accept_clients(s);
                continue;
            }
            handle_client_events(s, user, ev);
            if (atomic_load_explicit(&s->stopping, memory_order_relaxed))
                break;
        }
    }

    /* teardown: drain once more, then drop every client. The pollset
     * itself stays alive until opftp_reactor_shutdown (after workers
     * are joined) — workers may still call opftp_pollset_wake while
     * posting late completions, and waking a destroyed pollset would
     * race on the pipe fds. */
    drain_completions(s);
    while (s->clients)
        client_disconnect(s, s->clients);
    opftp_close_fd(s->listen_fd);
    s->listen_fd = -1;
}

int opftp_reactor_init(struct opftp_server* s){
    int rc = bind_listener(s);
    if (rc != 0)
        return rc;
    opftp_dispatch_init(s);
    opftp_commands_init(s);
    if (s->cb.after_commands)
        s->cb.after_commands((opftp_server_t*) s, s->cb.after_commands_ctx);
    rc = opftp_datachan_init(s);
    if (rc != 0)
        return rc;
    s->fs = opftp_fs_rooted(s->fs_base, s->root);
    if (!s->fs)
        return -ENOMEM;
    return 0;
}

void opftp_reactor_shutdown(struct opftp_server* s)
{
    /* Stop workers from waking the pollset: NULL the pointer under the
     * completion lock (a wake in flight is serialized with this), then
     * join the workers, then destroy the pollset. */
    opftp_pollset_t* ps = NULL;
    if (s->compl_mutex) {
        opftp_mutex_lock(s->compl_mutex);
        ps = s->pollset;
        s->pollset = NULL;
        opftp_mutex_unlock(s->compl_mutex);
    }
    opftp_datachan_shutdown(s);
    if (ps) {
        opftp_pollset_destroy(ps);
    }
    if (s->fs) {
        opftp_fs_rooted_free(s->fs);
        s->fs = NULL;
    }
    opftp_dispatch_free(s);
}

/* ---- public lifecycle ---- */

static void reactor_thread_entry(void* arg)
{
    struct opftp_server* s = arg;
    opftp_reactor_loop(s);
    atomic_store_explicit(&s->reactor_done, true, memory_order_release);
}

static void publish_ready(struct opftp_server* s)
{
    opftp_mutex_lock(s->life_mutex);
    s->ready = true;
    s->starting = false;
    opftp_cond_broadcast(s->life_cond);
    opftp_mutex_unlock(s->life_mutex);
}

int opftp_server_start(opftp_server_t* s)
{
    if (atomic_load_explicit(&s->started, memory_order_acquire))
        return -EALREADY;

    opftp_mutex_lock(s->life_mutex);
    s->starting = true;
    opftp_mutex_unlock(s->life_mutex);

    int rc = opftp_reactor_init(s);
    if (rc != 0) {
        opftp_mutex_lock(s->life_mutex);
        s->starting = false;
        opftp_cond_broadcast(s->life_cond);
        opftp_mutex_unlock(s->life_mutex);
        return rc;
    }

    atomic_store_explicit(&s->started, true, memory_order_release);
    atomic_store_explicit(&s->reactor_done, false, memory_order_relaxed);
    atomic_store_explicit(&s->stopping, false, memory_order_relaxed);
    s->reactor = opftp_thread_create(reactor_thread_entry, s);
    if (!s->reactor) {
        atomic_store_explicit(&s->started, false, memory_order_relaxed);
        opftp_mutex_lock(s->life_mutex);
        s->starting = false;
        opftp_cond_broadcast(s->life_cond);
        opftp_mutex_unlock(s->life_mutex);
        opftp_reactor_shutdown(s);
        return -ENOMEM;
    }
    atomic_store_explicit(&s->reactor_running, true, memory_order_release);
    publish_ready(s);
    return 0;
}

int opftp_server_run_loop(opftp_server_t* s)
{
    if (atomic_load_explicit(&s->started, memory_order_acquire))
        return -EALREADY;

    opftp_mutex_lock(s->life_mutex);
    s->starting = true;
    opftp_mutex_unlock(s->life_mutex);

    int rc = opftp_reactor_init(s);
    if (rc != 0) {
        opftp_mutex_lock(s->life_mutex);
        s->starting = false;
        opftp_cond_broadcast(s->life_cond);
        opftp_mutex_unlock(s->life_mutex);
        return rc;
    }
    atomic_store_explicit(&s->started, true, memory_order_release);
    atomic_store_explicit(&s->reactor_running, false, memory_order_relaxed);
    atomic_store_explicit(&s->stopping, false, memory_order_relaxed);
    publish_ready(s);
    opftp_reactor_loop(s);
    opftp_reactor_shutdown(s);
    atomic_store_explicit(&s->started, false, memory_order_relaxed);
    return 0;
}

int opftp_server_stop(opftp_server_t* s)
{
    /* Wait for readiness: a stop arriving during start/run_loop init
     * must not touch mutexes/pollset that are still being created. */
    opftp_mutex_lock(s->life_mutex);
    while (s->starting && !s->ready)
        opftp_cond_wait(s->life_cond, s->life_mutex);
    if (!s->ready) {
        opftp_mutex_unlock(s->life_mutex);
        return 0;   /* never started (or init failed) */
    }
    opftp_mutex_unlock(s->life_mutex);

    atomic_store_explicit(&s->stopping, true, memory_order_release);
    /* wake under the completion lock: serialized with the shutdown
     * NULLing of s->pollset (fd-reuse safety) */
    if (s->compl_mutex) {
        opftp_mutex_lock(s->compl_mutex);
        if (s->pollset)
            opftp_pollset_wake(s->pollset);
        opftp_mutex_unlock(s->compl_mutex);
    } else if (s->pollset) {
        opftp_pollset_wake(s->pollset);
    }

    if (atomic_load_explicit(&s->reactor_running, memory_order_acquire)) {
        /* wait for the reactor thread to finish (bounded) */
        int waited = 0;
        while (!atomic_load_explicit(&s->reactor_done, memory_order_acquire) &&
               waited < s->stop_timeout_ms) {
            usleep(10000);
            waited += 10;
        }
        if (!atomic_load_explicit(&s->reactor_done, memory_order_acquire))
            return -ETIMEDOUT;
        opftp_thread_join(s->reactor);
        opftp_thread_destroy(s->reactor);
        s->reactor = NULL;
        atomic_store_explicit(&s->reactor_running, false, memory_order_relaxed);
        opftp_reactor_shutdown(s);
        atomic_store_explicit(&s->started, false, memory_order_relaxed);
        return 0;
    }

    /* run_loop mode: the loop exits by itself; nothing to join */
    return 0;
}

void opftp_server_destroy(opftp_server_t* s)
{
    if (!s)
        return;
    if (atomic_load_explicit(&s->started, memory_order_acquire)) {
        /* never destroy with live workers: if stop timed out the
         * object stays alive for a retry */
        if (opftp_server_stop(s) != 0)
            return;
    }
    if (s->life_mutex)
        opftp_mutex_destroy(s->life_mutex);
    if (s->life_cond)
        opftp_cond_destroy(s->life_cond);
    if (s->snap_mutex)
        opftp_mutex_destroy(s->snap_mutex);
    if (s->tls)
        opftp_tls_ctx_free(s->tls);
    free(s->cert_pem);
    free(s->key_pem);
    free(s);
}
