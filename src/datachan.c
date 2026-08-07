/*
 * Data channel: PASV/PORT connection establishment (with peer
 * matching), worker pool, job queue, and completion draining.
 * All functions are reactor-thread except the worker entry.
 */
#include "opftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef OPFTP_PS3
#include <net/poll.h>
#else
#include <poll.h>
#endif
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>



/* ---- worker pool ---- */

int opftp_datachan_init(struct opftp_server* s)
{
    s->queue_cap = (unsigned) (s->workers_req * 2);
    s->queue = calloc(s->queue_cap, sizeof(*s->queue));
    s->queue_mutex = opftp_mutex_create();
    s->queue_cond = opftp_cond_create(s->queue_mutex);
    s->compl_mutex = opftp_mutex_create();
    if (!s->queue || !s->queue_mutex || !s->queue_cond || !s->compl_mutex)
        return -ENOMEM;

    s->num_workers = s->workers_req > 0 ? s->workers_req : 2;
    s->workers = calloc((size_t) s->num_workers, sizeof(*s->workers));
    if (!s->workers)
        return -ENOMEM;

    s->pool_running = true;
    s->workers_alive = 0;
    for (int i = 0; i < s->num_workers; i++) {
        void* t = opftp_thread_create(opftp_transfer_worker, s);
        if (!t) {
            s->pool_running = false;
            opftp_cond_broadcast(s->queue_cond);
            return -ENOMEM;
        }
        s->workers[i] = t;
    }
    return 0;
}

void opftp_datachan_shutdown(struct opftp_server* s)
{
    if (!s->pool_running && s->workers_alive == 0)
        return;

    /* cancel queued jobs so workers drain promptly */
    opftp_mutex_lock(s->queue_mutex);
    s->pool_running = false;
    for (unsigned i = 0; i < s->queue_count; i++) {
        struct opftp_transfer_job* j = s->queue[(s->queue_head + i) % s->queue_cap];
        atomic_store_explicit(&j->cancelled, true, memory_order_relaxed);
#ifndef OPFTP_PS3
        char c = 'x';
        ssize_t w = write(j->cancel_pipe[1], &c, 1);
        (void) w;
#endif
    }
    opftp_cond_broadcast(s->queue_cond);
    opftp_mutex_unlock(s->queue_mutex);

    /* wait for workers (bounded by stop timeout; poll the counter) */
    int waited = 0;
    for (;;) {
        opftp_mutex_lock(s->queue_mutex);
        bool done = (s->workers_alive == 0);
        opftp_mutex_unlock(s->queue_mutex);
        if (done)
            break;
        if (waited >= s->stop_timeout_ms)
            return;              /* workers still draining; caller retries */
        usleep(10000);
        waited += 10;
    }

    for (int i = 0; i < s->num_workers; i++) {
        if (s->workers[i]) {
            opftp_thread_join(s->workers[i]);
            opftp_thread_destroy(s->workers[i]);
            s->workers[i] = NULL;
        }
    }
    free(s->workers);
    s->workers = NULL;
    s->num_workers = 0;

    /* leftover queued jobs (if timeout path was never hit they are
     * gone; if it was, free what remains) */
    opftp_mutex_lock(s->queue_mutex);
    for (unsigned i = 0; i < s->queue_count; i++) {
        struct opftp_transfer_job* j = s->queue[(s->queue_head + i) % s->queue_cap];
        if (j->data_fd >= 0)
            opftp_close_fd(j->data_fd);
        if (j->pasv_fd >= 0)
            opftp_close_fd(j->pasv_fd);
#ifndef OPFTP_PS3
        close(j->cancel_pipe[0]);
        close(j->cancel_pipe[1]);
#endif
        opftp_client_release(j->client);
        free(j);
    }
    s->queue_head = s->queue_tail = s->queue_count = 0;
    opftp_mutex_unlock(s->queue_mutex);

    opftp_mutex_destroy(s->queue_mutex);
    s->queue_mutex = NULL;
    opftp_cond_destroy(s->queue_cond);
    s->queue_cond = NULL;
    opftp_mutex_destroy(s->compl_mutex);
    s->compl_mutex = NULL;
    free(s->queue);
    s->queue = NULL;
}

void opftp_transfer_worker(void* arg)
{
    struct opftp_server* s = arg;

    opftp_mutex_lock(s->queue_mutex);
    s->workers_alive++;
    opftp_mutex_unlock(s->queue_mutex);

    for (;;) {
        opftp_mutex_lock(s->queue_mutex);
        while (s->queue_count == 0 && s->pool_running)
            opftp_cond_wait(s->queue_cond, s->queue_mutex);
        if (s->queue_count == 0 && !s->pool_running) {
            opftp_mutex_unlock(s->queue_mutex);
            break;
        }
        struct opftp_transfer_job* j = s->queue[s->queue_head];
        s->queue_head = (s->queue_head + 1) % s->queue_cap;
        s->queue_count--;
        opftp_mutex_unlock(s->queue_mutex);

        opftp_transfer_run(s, j);
    }

    opftp_mutex_lock(s->queue_mutex);
    s->workers_alive--;
    opftp_cond_broadcast(s->queue_cond);
    opftp_mutex_unlock(s->queue_mutex);
}

int opftp_job_dispatch(struct opftp_server* s, struct opftp_transfer_job* j)
{
    int rc = 0;
    opftp_mutex_lock(s->queue_mutex);
    if (s->queue_count >= s->queue_cap) {
        rc = -EAGAIN;
    } else {
        s->queue[s->queue_tail] = j;
        s->queue_tail = (s->queue_tail + 1) % s->queue_cap;
        s->queue_count++;
        opftp_cond_signal(s->queue_cond);
    }
    opftp_mutex_unlock(s->queue_mutex);
    return rc;
}

/* ---- address helpers ---- */

/* Compare two addresses; a v4-mapped IPv6 address equals its plain
 * IPv4 form (dual-stack listeners accept v4 clients as v4-mapped). */
static bool addr_eq(const struct sockaddr* a, const struct sockaddr* b)
{
    if (a->sa_family == b->sa_family) {
        if (a->sa_family == AF_INET) {
            const struct sockaddr_in* x = (const struct sockaddr_in*) a;
            const struct sockaddr_in* y = (const struct sockaddr_in*) b;
            return x->sin_addr.s_addr == y->sin_addr.s_addr;
        }
        if (a->sa_family == AF_INET6) {
            const struct sockaddr_in6* x = (const struct sockaddr_in6*) a;
            const struct sockaddr_in6* y = (const struct sockaddr_in6*) b;
            return memcmp(&x->sin6_addr, &y->sin6_addr, sizeof(x->sin6_addr)) == 0;
        }
        return false;
    }
    /* mixed family: allow v4-mapped IPv6 == plain IPv4 */
    const struct sockaddr_in6* a6 = NULL;
    const struct sockaddr_in* a4 = NULL;
    if (a->sa_family == AF_INET6 && opftp_is_v4mapped(&((const struct sockaddr_in6*) a)->sin6_addr)) {
        a6 = (const struct sockaddr_in6*) a;
        if (b->sa_family == AF_INET)
            a4 = (const struct sockaddr_in*) b;
    } else if (b->sa_family == AF_INET6 && opftp_is_v4mapped(&((const struct sockaddr_in6*) b)->sin6_addr)) {
        a6 = (const struct sockaddr_in6*) b;
        if (a->sa_family == AF_INET)
            a4 = (const struct sockaddr_in*) a;
    }
    if (a6 && a4) {
        uint32_t m = 0;
        memcpy(&m, &a6->sin6_addr.s6_addr[12], 4);
        return m == a4->sin_addr.s_addr;
    }
    return false;
}

static bool peer_matches(struct opftp_client* c, const struct sockaddr* peer)
{
    if (c->server->allow_foreign_port)
        return true;
    return addr_eq((const struct sockaddr*) &c->peer, peer);
}

/* Reactor-side data-connection precheck: pure address comparison, no
 * blocking. Rejects a PORT/EPRT target that differs from the control
 * peer (bounce) BEFORE any 150 reply, so the client sees 425 first.
 * Returns 0 if the configured target is acceptable (or none yet). */
int opftp_datachan_precheck(struct opftp_client* c)
{
    if (c->have_data_peer &&
        !peer_matches(c, (const struct sockaddr*) &c->data_peer))
        return -EACCES;
    return 0;
}

/* ps3dk's sys/socket layer doesn't define TCP_NODELAY (the legacy
 * common.h did: 0x01). Same value on Linux. */
#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif

/* Data-channel socket tuning: disable Nagle so small transfers (and the
 * last chunk of any transfer) aren't held for a delayed-ACK window. */
static void tune_data_fd(int fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

/* ---- connection establishment ---- */

/* Worker-side data-connection establishment, called from the worker
 * before the transfer loop. Uses the setup copied into the job at
 * dispatch, so a slow/broken client can never stall the reactor.
 * On success sets j->data_fd and j->conn_ok. Fds are left open on
 * failure — fd numbers are only freed on the reactor thread (the
 * completion handler closes them). Returns 0 or -errno. */
int opftp_datachan_connect_job(struct opftp_server* s,
                               struct opftp_transfer_job* j)
{
    int fd = -1;

    if (j->pasv_fd >= 0) {
        /* PASV: wait for the client's data connection (bounded,
         * cancel-aware), then accept and verify the peer. */
        struct pollfd p[2];
        int np = 0;
        p[np].fd = j->pasv_fd;
        p[np].events = POLLIN;
        p[np].revents = 0;
        np++;
#ifndef OPFTP_PS3
        if (j->cancel_pipe[0] >= 0) {
            p[np].fd = j->cancel_pipe[0];
            p[np].events = POLLIN;
            p[np].revents = 0;
            np++;
        }
#endif
        int r = poll(p, np, 2000);
        if (r <= 0)
            return r == 0 ? -ETIMEDOUT : -errno;
#ifndef OPFTP_PS3
        if (np > 1 && (p[1].revents & (POLLIN | POLLERR | POLLHUP)))
            return atomic_load_explicit(&j->cancelled, memory_order_relaxed)
                       ? -ECANCELED : -EAGAIN;
#endif
        opftp_sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        fd = accept(j->pasv_fd, (struct sockaddr*) &ss, &sl);
        if (fd < 0)
            return -errno;
        j->data_fd = fd;          /* reactor closes at completion */
        opftp_set_nonblock(fd);
        if (!s->allow_foreign_port &&
            !addr_eq((const struct sockaddr*) &ss,
                     (const struct sockaddr*) &j->ctl_peer)) {
            errno = EACCES;
            return -EACCES;       /* reactor closes fd */
        }
        tune_data_fd(fd);
        j->conn_ok = true;
        return 0;
    }

    if (j->have_data_peer) {
        /* PORT/EPRT: connect to the client's listener (bounded). The
         * target was already bounce-checked against the control peer
         * on the reactor (pure address compare, no blocking). */
        const struct sockaddr* dst = (const struct sockaddr*) &j->data_peer;
        fd = socket(dst->sa_family, SOCK_STREAM, 0);
        if (fd < 0)
            return -errno;
        j->data_fd = fd;          /* reactor closes at completion */
        opftp_set_nonblock(fd);
        int r = connect(fd, dst, j->data_peerlen);
        if (r != 0 && errno != EINPROGRESS)
            return -errno;
        struct pollfd p[2];
        int np = 0;
        p[np].fd = fd;
        p[np].events = POLLOUT;
        p[np].revents = 0;
        np++;
#ifndef OPFTP_PS3
        if (j->cancel_pipe[0] >= 0) {
            p[np].fd = j->cancel_pipe[0];
            p[np].events = POLLIN;
            p[np].revents = 0;
            np++;
        }
#endif
        r = poll(p, np, 2000);
        if (r <= 0)
            return r == 0 ? -ETIMEDOUT : -errno;
#ifndef OPFTP_PS3
        if (np > 1 && (p[1].revents & (POLLIN | POLLERR | POLLHUP)))
            return atomic_load_explicit(&j->cancelled, memory_order_relaxed)
                       ? -ECANCELED : -EAGAIN;
#endif
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) {
            errno = soerr;
            return -soerr;
        }
        j->conn_ok = true;
        return 0;
    }

    return -ENOTCONN;   /* no PASV, no PORT */
}

/* ---- completion draining (reactor thread) ---- */

void opftp_datachan_complete(struct opftp_server* s, struct opftp_transfer_job* j)
{
    /* Called with the completion already popped; j owns a client ref
     * and a data fd/pipe pair closed by the worker. The reactor sends
     * the final reply and releases the ref. */
    struct opftp_client* c = j->client;

    /* Reactor-thread fd cleanup: the worker is done with these, but
     * only the reactor may free fd numbers (single closer, no reuse
     * races with accept/socket). */
    if (j->data_fd >= 0) {
        opftp_close_fd(j->data_fd);
        j->data_fd = -1;
    }
    if (j->pasv_fd >= 0) {
        opftp_close_fd(j->pasv_fd);
        j->pasv_fd = -1;
    }
#ifndef OPFTP_PS3
    if (j->cancel_pipe[0] >= 0) {
        close(j->cancel_pipe[0]);
        close(j->cancel_pipe[1]);
        j->cancel_pipe[0] = j->cancel_pipe[1] = -1;
    }
#endif

    if (j->generation != c->generation) {
        /* client disconnected mid-transfer: suppress reply */
        goto out;
    }

    c->job = NULL;

    if (!j->conn_ok && j->op != OPFTP_JOB_COPY) {
        /* the worker never established the data connection (e.g. PASV
         * accept timeout, bounce reject, PORT connect failure).
         * COPY jobs have no data connection: conn_ok stays false but
         * the op result decides the reply. */
        if (j->result == -ECANCELED || j->result == -ETIMEDOUT ||
            j->result == -EACCES)
            opftp_client_send_reply(c, 425, R425);
        else
            opftp_client_send_reply(c, 451, R451);
    } else if (j->result == 0) {
        if (j->op == OPFTP_JOB_COPY)
            opftp_client_send_reply(c, 250, "Copy successful.");
        else
            opftp_client_send_reply(c, 226, R226);
    } else if (j->result == -ECANCELED ||
               j->result == -EPIPE || j->result == -ECONNRESET ||
               j->result == -ETIMEDOUT) {
        opftp_client_send_reply(c, 426, R426);
    } else if (j->result == -ENOENT || j->result == -EACCES ||
               j->result == -ENOTDIR || j->result == -EISDIR ||
               /* copy rejected the paths themselves (same file, name too
                * long, tree too deep) — a 550 class, not a 451 */
               j->result == -EINVAL || j->result == -ENAMETOOLONG ||
               j->result == -ELOOP) {
        opftp_client_send_reply(c, 550, R550);
    } else {
        opftp_client_send_reply(c, 451, R451);
    }

    if (c->abor_pending) {
        opftp_client_send_reply(c, 226, "Abort successful.");
        c->abor_pending = false;
    }

out:
    opftp_client_release(c);
    free(j);
}
