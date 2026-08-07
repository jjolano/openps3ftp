/*
 * Worker-side transfer loops. A worker owns the data socket and the
 * job for the duration of opftp_transfer_run; on return it has closed
 * the data socket and cancel pipe and posted the completion to the
 * reactor queue (no client state is touched after that).
 */
#include "opftp.h"
#include "tls.h"
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
#include <sys/stat.h>
#include <time.h>



#define TRANSFER_BUF (64 * 1024)

static bool job_cancelled(struct opftp_transfer_job* j)
{
    return atomic_load_explicit(&j->cancelled, memory_order_relaxed);
}

/* Poll {fd, cancel pipe} with a timeout so cancellation is honored.
 * Returns 0 if fd ready, -ECANCELED if cancelled, -errno on error. */
static int wait_fd(struct opftp_transfer_job* j, int fd, short events, int timeout_ms)
{
#ifdef OPFTP_PS3
    /* ps3 poll() only watches sockets — the cancel pipe cannot be
     * polled; rely on the poll timeout + atomic flag (<=1s latency) */
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    int r = poll(&pfd, 1, timeout_ms);
    if (r < 0)
        return -errno;
    if (r == 0)
        return job_cancelled(j) ? -ECANCELED : -EAGAIN;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -EPIPE;
    if (pfd.revents & events)
        return 0;
    return job_cancelled(j) ? -ECANCELED : -EAGAIN;
#else
    struct pollfd pfd[2];
    pfd[0].fd = fd;
    pfd[0].events = events;
    pfd[0].revents = 0;
    pfd[1].fd = j->cancel_pipe[0];
    pfd[1].events = POLLIN;
    pfd[1].revents = 0;

    int r = poll(pfd, 2, timeout_ms);
    if (r < 0)
        return -errno;
    if (r == 0)
        return job_cancelled(j) ? -ECANCELED : -EAGAIN;   /* timeout: retry */
    if (pfd[1].revents & (POLLIN | POLLERR | POLLHUP))
        return job_cancelled(j) ? -ECANCELED : -EAGAIN;
    if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        return -EPIPE;
    if (pfd[0].revents & events)
        return 0;
    return -EAGAIN;
#endif
}

/* Send all bytes, honoring cancellation. Returns 0 or -errno. */
static int send_all(struct opftp_transfer_job* j, const void* buf, size_t len)
{
    const char* p = buf;
    size_t off = 0;
    while (off < len) {
        if (job_cancelled(j))
            return -ECANCELED;
        if (j->tls) {
            ssize_t n = opftp_tls_write(j->tls, p + off, len - off);
            if (n > 0) { off += (size_t) n; continue; }
            if (n == -3) {          /* WANT_WRITE */
                int w = wait_fd(j, j->data_fd, POLLOUT, 1000);
                if (w == -ECANCELED) return -ECANCELED;
                if (w < 0 && w != -EAGAIN) return w;
                continue;
            }
            if (n == -2) {          /* WANT_READ */
                int w = wait_fd(j, j->data_fd, POLLIN, 1000);
                if (w == -ECANCELED) return -ECANCELED;
                if (w < 0 && w != -EAGAIN) return w;
                continue;
            }
            return -EPIPE;
        }
        int w = wait_fd(j, j->data_fd, POLLOUT, 1000);
        if (w == -ECANCELED) return -ECANCELED;
        if (w == -EAGAIN) continue;
        if (w < 0) return w;
        ssize_t n = send(j->data_fd, p + off, len - off, MSG_NOSIGNAL);
        if (n > 0) { off += (size_t) n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) return -errno;
        return -EPIPE;   /* peer closed */
    }
    return 0;
}

/* Receive up to len bytes, honoring cancellation. Returns bytes
 * received, 0 on clean EOF, -errno on error. */
static ssize_t recv_some(struct opftp_transfer_job* j, void* buf, size_t len)
{
    if (job_cancelled(j))
        return -ECANCELED;
    if (j->tls) {
        ssize_t n = opftp_tls_read(j->tls, buf, len);
        if (n >= 0)
            return n;               /* n bytes or clean EOF (0) */
        if (n == -2) {              /* WANT_READ */
            int w = wait_fd(j, j->data_fd, POLLIN, 1000);
            if (w == -ECANCELED) return -ECANCELED;
            if (w < 0 && w != -EAGAIN) return w;
            return -EAGAIN;
        }
        if (n == -3) {              /* WANT_WRITE */
            int w = wait_fd(j, j->data_fd, POLLOUT, 1000);
            if (w == -ECANCELED) return -ECANCELED;
            if (w < 0 && w != -EAGAIN) return w;
            return -EAGAIN;
        }
        return -EPIPE;
    }
    int w = wait_fd(j, j->data_fd, POLLIN, 1000);
    if (w == -ECANCELED) return -ECANCELED;
    if (w == -EAGAIN) return -EAGAIN;
    if (w < 0) return w;
    ssize_t n = recv(j->data_fd, buf, len, 0);
    if (n >= 0) return n;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -EAGAIN;
    return -errno;
}

/* PROT P: wrap the data fd in TLS and complete the handshake.
 * Cancel-aware (polls the cancel pipe) with a 10s deadline. */
static int data_tls_setup(struct opftp_server* s, struct opftp_transfer_job* j)
{
    if (!s->tls)
        return -ENOTSUP;
    int64_t deadline = opftp_now_ms() + 10000;

    struct opftp_tls_session* tls = NULL;
    if (opftp_tls_session_create(s->tls, j->data_fd, NULL, 0, deadline,
                                 &tls) != 0)
        return -EPIPE;
    j->tls = tls;

    for (;;) {
        if (job_cancelled(j))
            return -ECANCELED;
        int r = opftp_tls_handshake(j->tls);
        if (r == 0)
            return 0;
        if (r != 1 && r != 2)
            return -EPIPE;          /* error or deadline */
        int w = wait_fd(j, j->data_fd, (r == 2) ? POLLOUT : POLLIN, 1000);
        if (w == -ECANCELED)
            return -ECANCELED;
        if (w < 0 && w != -EAGAIN)
            return w;
    }
}

/* ---- LIST / NLST ---- */

static int transfer_list(struct opftp_server* s, struct opftp_transfer_job* j,
                         uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;
    char line[1024];
    int rc = 0;

    opftp_stat_t st;
    if (fs->stat(fs->ctx, j->path, &st) != 0) {
        return -errno;
    }

    if ((st.mode & S_IFMT) != S_IFDIR) {
        /* single file listing */
        opftp_dirent_t de;
        memset(&de, 0, sizeof(de));
        const char* slash = strrchr(j->path, '/');
        snprintf(de.name, sizeof(de.name), "%s", slash ? slash + 1 : j->path);
        de.mode = st.mode;
        de.size = st.size;
        de.mtime = st.mtime;
        de.uid = st.uid;
        de.gid = st.gid;
        int n = opftp_listing_format(line, sizeof(line), &de, NULL);
        rc = send_all(j, line, (size_t) n);
        if (rc == 0) *bytes += (uint64_t) n;
        return rc;
    }

    void* dir = NULL;
    if (fs->opendir(fs->ctx, j->path, &dir) != 0)
        return -errno;

    opftp_dirent_t de;
    for (;;) {
        if (job_cancelled(j)) { rc = -ECANCELED; break; }
        int r = fs->readdir(fs->ctx, dir, &de);
        if (r < 0) { rc = -errno; break; }
        if (r == 0) break;
        int n = opftp_listing_format(line, sizeof(line), &de, NULL);
        if (j->nlst) {
            /* NLST: name only */
            n = snprintf(line, sizeof(line), "%s\r\n", de.name);
        }
        rc = send_all(j, line, (size_t) n);
        if (rc != 0) break;
        *bytes += (uint64_t) n;
    }
    fs->closedir(fs->ctx, dir);
    return rc;
}

/* ---- RETR ---- */

static int transfer_retr(struct opftp_server* s, struct opftp_transfer_job* j,
                         uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;
    int fd = -1;
    if (fs->open(fs->ctx, j->path, OPFTP_O_RDONLY, 0, &fd) != 0)
        return -errno;
    if (j->rest > 0) {
        if (fs->seek(fs->ctx, fd, (int64_t) j->rest, SEEK_SET) < 0) {
            int e = errno;
            fs->close(fs->ctx, fd);
            return -e;
        }
    }

    char* buf = malloc(TRANSFER_BUF);
    if (!buf) { fs->close(fs->ctx, fd); return -ENOMEM; }

    int rc = 0;
    for (;;) {
        if (job_cancelled(j)) { rc = -ECANCELED; break; }
        ssize_t n = fs->read(fs->ctx, fd, buf, TRANSFER_BUF);
        if (n < 0) { rc = -errno; break; }
        if (n == 0) break;
        rc = send_all(j, buf, (size_t) n);
        if (rc != 0) break;
        *bytes += (uint64_t) n;
    }
    free(buf);
    fs->close(fs->ctx, fd);
    return rc;
}

/* ---- STOR / APPE ---- */

static int transfer_stor(struct opftp_server* s, struct opftp_transfer_job* j,
                         uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;
    int flags = OPFTP_O_WRONLY | OPFTP_O_CREAT;
    if (j->op == OPFTP_JOB_STOR && j->rest == 0)
        flags |= OPFTP_O_TRUNC;    /* REST STOR resumes: no truncate */
    if (j->op == OPFTP_JOB_APPE)
        flags |= OPFTP_O_APPEND;

    int fd = -1;
    if (fs->open(fs->ctx, j->path, flags, 0644, &fd) != 0)
        return -errno;
    if (j->rest > 0 && j->op == OPFTP_JOB_STOR) {
        if (fs->seek(fs->ctx, fd, (int64_t) j->rest, SEEK_SET) < 0) {
            int e = errno;
            fs->close(fs->ctx, fd);
            return -e;
        }
    }

    char* buf = malloc(TRANSFER_BUF);
    if (!buf) { fs->close(fs->ctx, fd); return -ENOMEM; }

    int rc = 0;
    for (;;) {
        if (job_cancelled(j)) { rc = -ECANCELED; break; }
        ssize_t n = recv_some(j, buf, TRANSFER_BUF);
        if (n == -EAGAIN) continue;
        if (n == 0) break;                 /* client closed data conn: done */
        if (n < 0) { rc = (int) n; break; }
        /* write fully (fs may do partial writes) */
        size_t off = 0;
        while (off < (size_t) n) {
            ssize_t w = fs->write(fs->ctx, fd, buf + off, (size_t) n - off);
            if (w < 0) { rc = -errno; break; }
            if (w == 0) { rc = -EIO; break; }
            off += (size_t) w;
        }
        if (rc != 0) break;
        *bytes += (uint64_t) n;
    }
    free(buf);
    fs->close(fs->ctx, fd);
    return rc;
}

/* ---- worker entry ---- */

void opftp_transfer_run(struct opftp_server* s, struct opftp_transfer_job* j)
{
    int rc;
    uint64_t bytes = 0;

    /* PROT P: TLS-wrap the data channel and handshake first. */
    if (j->need_tls) {
        rc = data_tls_setup(s, j);
        if (rc != 0)
            goto out;               /* completion with the failure */
    }

    switch (j->op) {
    case OPFTP_JOB_LIST:
        rc = transfer_list(s, j, &bytes);
        break;
    case OPFTP_JOB_RETR:
        rc = transfer_retr(s, j, &bytes);
        break;
    case OPFTP_JOB_STOR:
        rc = transfer_stor(s, j, &bytes);
        break;
    case OPFTP_JOB_APPE:
        rc = transfer_stor(s, j, &bytes);
        break;
    default:
        rc = -EINVAL;
        break;
    }

out:
    if (j->tls) {
        /* Polite shutdown: send close_notify so clients (e.g. ftplib's
         * conn.unwrap()) can complete their TLS shutdown. Bounded. */
        if (rc == 0) {
            int deadline = 0;
            for (int i = 0; i < 50; i++) {   /* ~5s max */
                int cr = opftp_tls_close_notify(j->tls);
                if (cr == 0)
                    break;
                if (cr != 1 && cr != 2)
                    break;
                struct pollfd p = { .fd = j->data_fd,
                                    .events = (cr == 2) ? POLLOUT : POLLIN };
                if (poll(&p, 1, 100) <= 0)
                    break;
            }
        }
        opftp_tls_session_free(j->tls);   /* worker-owned; heap only */
        j->tls = NULL;
    }

    /* The worker is done with the data socket and cancel pipe — but
     * does NOT close them: fd numbers must only be freed on the
     * reactor thread (serialized with accept/socket allocation) to
     * avoid a close racing a reused fd. The reactor closes them when
     * it processes this completion. */
    j->result = rc;
    j->bytes = bytes;
    j->posted = true;

    /* Post completion and wake the reactor under the completion lock:
     * the reactor NULLs s->pollset under the same lock before tearing
     * down, so a wake here can never race a pollset destroy/recreate
     * (fd-reuse safety across restarts). */
    opftp_mutex_lock(s->compl_mutex);
    j->compl_next = s->completions;
    s->completions = j;
    if (s->pollset)
        opftp_pollset_wake(s->pollset);
    opftp_mutex_unlock(s->compl_mutex);
}
