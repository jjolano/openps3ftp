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

static bool is_v4_mapped(const struct sockaddr_in6* a6)
{
    return opftp_is_v4mapped(&a6->sin6_addr);
}

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
    if (a->sa_family == AF_INET6 && is_v4_mapped((const struct sockaddr_in6*) a)) {
        a6 = (const struct sockaddr_in6*) a;
        if (b->sa_family == AF_INET)
            a4 = (const struct sockaddr_in*) b;
    } else if (b->sa_family == AF_INET6 && is_v4_mapped((const struct sockaddr_in6*) b)) {
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

static int set_nonblock(int fd)
{
    return opftp_set_nonblock(fd);
}

int opftp_datachan_connect(struct opftp_client* c)
{
    struct opftp_server* s = c->server;
    int fd = -1;

    if (c->pasv_fd >= 0) {
        /* accept on the PASV listener with a deadline; verify the
         * connecting peer against the control peer */
        struct pollfd p = { .fd = c->pasv_fd, .events = POLLIN };
        if (poll(&p, 1, 10000) <= 0) {
            opftp_close_fd(c->pasv_fd);
            c->pasv_fd = -1;
            errno = ETIMEDOUT;
            return -1;
        }
        opftp_sockaddr_storage ss;
        socklen_t sl = sizeof(ss);
        fd = accept(c->pasv_fd, (struct sockaddr*) &ss, &sl);
        int accept_errno = errno;
        opftp_close_fd(c->pasv_fd);
        c->pasv_fd = -1;
        if (fd < 0) {
            errno = accept_errno;
            return -1;
        }
        if (!peer_matches(c, (const struct sockaddr*) &ss)) {
            opftp_close_fd(fd);
            errno = EACCES;
            return -1;
        }
        tune_data_fd(fd);
        return fd;
    }

    if (c->have_data_peer) {
        /* connect to the PORT target; verify the target IP matches the
         * control peer (bounce protection) */
        struct sockaddr* dst = (struct sockaddr*) &c->data_peer;
        if (!peer_matches(c, dst)) {
            errno = EACCES;
            return -1;
        }
        fd = socket(dst->sa_family, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        set_nonblock(fd);
        int r = connect(fd, dst, c->data_peerlen);
        if (r != 0 && errno != EINPROGRESS) {
            int e = errno;
            opftp_close_fd(fd);
            errno = e;
            return -1;
        }
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        r = poll(&p, 1, 10000);
        if (r <= 0) {
            int e = r == 0 ? ETIMEDOUT : errno;
            opftp_close_fd(fd);
            errno = e;
            return -1;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) {
            opftp_close_fd(fd);
            errno = soerr;
            return -1;
        }
        set_nonblock(fd);
        tune_data_fd(fd);
        return fd;
    }

    errno = ENOTCONN;   /* no PASV, no PORT */
    return -1;
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

    if (j->result == 0) {
        if (j->op == OPFTP_JOB_COPY)
            opftp_client_send_reply(c, 250, "Copy successful.");
        else
            opftp_client_send_reply(c, 226, "Transfer complete.");
    } else if (j->result == -ECANCELED ||
               j->result == -EPIPE || j->result == -ECONNRESET ||
               j->result == -ETIMEDOUT) {
        opftp_client_send_reply(c, 426, "Connection closed; transfer aborted.");
    } else if (j->result == -ENOENT || j->result == -EACCES ||
               j->result == -ENOTDIR || j->result == -EISDIR) {
        opftp_client_send_reply(c, 550, "Cannot access specified file or directory.");
    } else {
        opftp_client_send_reply(c, 451, "Data transfer error (network).");
    }

    if (c->abor_pending) {
        opftp_client_send_reply(c, 226, "Abort successful.");
        c->abor_pending = false;
    }

out:
    opftp_client_release(c);
    free(j);
}
