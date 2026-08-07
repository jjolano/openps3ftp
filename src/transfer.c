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

/* ps3dk's net layer lacks MSG_WAITALL (PSL1GHT defines it 0x0040).
 * webMAN ships it on STOR in production: recv blocks until a full
 * chunk arrives, so every disk write is a full chunk. No-op-ish on
 * non-blocking sockets (our poll loop handles partials anyway). */
#ifndef MSG_WAITALL
#define MSG_WAITALL 0x0040
#endif

static bool job_cancelled(struct opftp_transfer_job* j)
{
    return atomic_load_explicit(&j->cancelled, memory_order_relaxed);
}

/* Publish live progress (relaxed; readers only display it). */
static void progress_store(struct opftp_transfer_job* j, uint64_t bytes)
{
    atomic_store_explicit(&j->bytes, bytes, memory_order_relaxed);
}

/* Poll {fd, cancel pipe} with a timeout so cancellation is honored.
 * Returns 0 if fd ready, -ECANCELED if cancelled, -errno on error. */
static int wait_fd(struct opftp_transfer_job* j, int fd, short events, int timeout_ms)
{
#ifdef OPFTP_PS3
    /* ps3 has neither pipe() nor socketpair(), and its poll() only
     * watches sockets — so there is no cancel fd to wait on and
     * cancellation is the atomic flag plus a bounded poll timeout.
     * Cap the wait so ABOR is honoured within OPFTP_PS3_CANCEL_MS
     * rather than the caller's full timeout; every caller already
     * treats -EAGAIN as "loop again". */
    if (timeout_ms < 0 || timeout_ms > OPFTP_PS3_CANCEL_MS)
        timeout_ms = OPFTP_PS3_CANCEL_MS;
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
            if (n == OPFTP_TLS_WANT_WRITE) {
                int w = wait_fd(j, j->data_fd, POLLOUT, 1000);
                if (w == -ECANCELED) return -ECANCELED;
                if (w < 0 && w != -EAGAIN) return w;
                continue;
            }
            if (n == OPFTP_TLS_WANT_READ) {
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
        if (n == OPFTP_TLS_WANT_READ) {
            int w = wait_fd(j, j->data_fd, POLLIN, 1000);
            if (w == -ECANCELED) return -ECANCELED;
            if (w < 0 && w != -EAGAIN) return w;
            return -EAGAIN;
        }
        if (n == OPFTP_TLS_WANT_WRITE) {
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
    ssize_t n = recv(j->data_fd, buf, len, MSG_WAITALL);
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
        if (r == OPFTP_TLS_HS_DONE)
            return 0;
        if (r != OPFTP_TLS_HS_WANT_READ && r != OPFTP_TLS_HS_WANT_WRITE)
            return -EPIPE;          /* error or deadline */
        int w = wait_fd(j, j->data_fd, (r == OPFTP_TLS_HS_WANT_WRITE) ? POLLOUT : POLLIN, 1000);
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
    /* Batch listing lines: one send per entry = one poll+send syscall
     * pair and one TCP segment per entry (a 1,000-entry dir = 1,000 of
     * each, plus an ACK storm). Accumulate and flush instead. */
    char batch[8 * 1024];
    size_t blen = 0;
    int rc = 0;

#define LIST_FLUSH() do { \
        if (blen > 0) { \
            rc = send_all(j, batch, blen); \
            if (rc == 0) { \
                *bytes += (uint64_t) blen; \
                progress_store(j, *bytes); \
            } \
            blen = 0; \
        } \
    } while (0)

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
        int n;
        if (j->mlsd)
            n = opftp_listing_format_mlsd(line, sizeof(line), &de);
        else
            n = opftp_listing_format(line, sizeof(line), &de, NULL);
        if ((size_t) n > sizeof(batch) - blen)
            LIST_FLUSH();
        memcpy(batch + blen, line, (size_t) n);
        blen += (size_t) n;
        LIST_FLUSH();
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
        int n;
        if (j->nlst) {
            /* NLST: name only */
            n = snprintf(line, sizeof(line), "%s\r\n", de.name);
        } else if (j->mlsd) {
            n = opftp_listing_format_mlsd(line, sizeof(line), &de);
        } else {
            n = opftp_listing_format(line, sizeof(line), &de, NULL);
        }
        if ((size_t) n > sizeof(batch) - blen)
            LIST_FLUSH();
        if (rc != 0)
            break;
        memcpy(batch + blen, line, (size_t) n);
        blen += (size_t) n;
    }
    LIST_FLUSH();
    fs->closedir(fs->ctx, dir);
    return rc;
#undef LIST_FLUSH
}

/* ---- RETR ---- */

static int transfer_retr(struct opftp_server* s, struct opftp_transfer_job* j,
                         uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;
    int fd = -1;
    if (fs->open(fs->ctx, j->path, OPFTP_O_RDONLY, 0, &fd) != 0)
        return -errno;
    opftp_stat_t st;
    if (fs->fstat(fs->ctx, fd, &st) == 0 && (st.mode & S_IFMT) == S_IFREG)
        j->total = st.size;   /* progress denominator for the OSD */
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
        progress_store(j, *bytes);
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
        progress_store(j, *bytes);
    }
    free(buf);
    fs->close(fs->ctx, fd);
    return rc;
}

/* ---- CPFR/CPTO: server-side copy (file or directory tree) ---- */

#define COPY_MAX_DEPTH 32

/* Copy one regular file. Returns 0 or -errno. A destination we created
 * is removed on failure; one that already existed is left untouched. */
static int copy_file(struct opftp_server* s, struct opftp_transfer_job* j,
                     const char* src, const char* dst, uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;
    int in = -1, out = -1;

    if (fs->open(fs->ctx, src, OPFTP_O_RDONLY, 0, &in) != 0)
        return -errno;

    opftp_stat_t st;
    bool dst_existed = (fs->stat(fs->ctx, dst, &st) == 0);

    if (fs->open(fs->ctx, dst, OPFTP_O_WRONLY | OPFTP_O_CREAT |
                               OPFTP_O_TRUNC, 0644, &out) != 0) {
        int e = errno;
        fs->close(fs->ctx, in);
        return -e;
    }

    char* buf = malloc(TRANSFER_BUF);
    if (!buf) {
        fs->close(fs->ctx, in);
        fs->close(fs->ctx, out);
        if (!dst_existed)
            fs->unlink(fs->ctx, dst);
        return -ENOMEM;
    }

    int rc = 0;
    for (;;) {
        if (job_cancelled(j)) { rc = -ECANCELED; break; }
        ssize_t n = fs->read(fs->ctx, in, buf, TRANSFER_BUF);
        if (n < 0) { rc = -errno; break; }
        if (n == 0) break;
        size_t off = 0;
        while (off < (size_t) n) {
            ssize_t w = fs->write(fs->ctx, out, buf + off, (size_t) n - off);
            if (w < 0) { rc = -errno; break; }
            if (w == 0) { rc = -EIO; break; }
            off += (size_t) w;
        }
        if (rc != 0) break;
        *bytes += (uint64_t) n;
        progress_store(j, *bytes);
    }
    free(buf);
    fs->close(fs->ctx, in);
    fs->close(fs->ctx, out);
    /* Never leave a half-written file the client didn't have before. */
    if (rc != 0 && !dst_existed)
        fs->unlink(fs->ctx, dst);
    return rc;
}

/* Pending directory in the tree walk, held as a path relative to the
 * two copy roots so a node costs a few bytes instead of two 1KB paths. */
struct copy_dir {
    struct copy_dir* next;
    int depth;
    char rel[];              /* "" for the root itself */
};

static struct copy_dir* copy_push(struct copy_dir* head, const char* rel, int depth)
{
    size_t n = strlen(rel);
    struct copy_dir* d = malloc(sizeof(*d) + n + 1);
    if (!d)
        return NULL;
    d->next = head;
    d->depth = depth;
    memcpy(d->rel, rel, n + 1);
    return d;
}

static void copy_free_stack(struct copy_dir* head)
{
    while (head) {
        struct copy_dir* d = head;
        head = d->next;
        free(d);
    }
}

/* root + "/" + rel, keeping paths canonical (no "//" for root). */
static int copy_join(char* out, size_t outsz, const char* root, const char* rel)
{
    int n;
    if (!rel[0])
        n = snprintf(out, outsz, "%s", root);
    else if (root[0] == '/' && root[1] == '\0')
        n = snprintf(out, outsz, "/%s", rel);
    else
        n = snprintf(out, outsz, "%s/%s", root, rel);
    return (n < 0 || (size_t) n >= outsz) ? -ENAMETOOLONG : 0;
}

/* Copy the directory tree at j->path to j->dst. Iterative (a PS3 worker
 * stack is 128KB, so no recursion) and depth-limited.
 *
 * ponytail: a partial tree is left in place on failure. Recursively
 * deleting a client-named destination to "clean up" is a far worse
 * failure mode than leaving the partial copy for the client to remove.
 */
static int copy_tree(struct opftp_server* s, struct opftp_transfer_job* j,
                     uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;
    struct copy_dir* stack = copy_push(NULL, "", 0);
    if (!stack)
        return -ENOMEM;

    char srcp[OPFTP_MAX_PATH], dstp[OPFTP_MAX_PATH], childrel[OPFTP_MAX_PATH];
    int rc = 0;

    while (stack) {
        struct copy_dir* d = stack;
        stack = d->next;

        if (job_cancelled(j)) { rc = -ECANCELED; free(d); break; }

        rc = copy_join(srcp, sizeof(srcp), j->path, d->rel);
        if (rc == 0)
            rc = copy_join(dstp, sizeof(dstp), j->dst, d->rel);
        if (rc != 0) { free(d); break; }

        /* mkdir is fine to lose: an existing destination directory is a
         * merge, which is what clients expect from a tree copy. */
        opftp_stat_t dst_st;
        if (fs->stat(fs->ctx, dstp, &dst_st) != 0 &&
            fs->mkdir(fs->ctx, dstp, 0755) != 0) {
            rc = -errno;
            free(d);
            break;
        }

        void* dir = NULL;
        if (fs->opendir(fs->ctx, srcp, &dir) != 0) {
            rc = -errno;
            free(d);
            break;
        }

        opftp_dirent_t de;
        for (;;) {
            if (job_cancelled(j)) { rc = -ECANCELED; break; }
            int r = fs->readdir(fs->ctx, dir, &de);
            if (r < 0) { rc = -errno; break; }
            if (r == 0) break;
            /* backends don't emit these, but a walk that trusted a
             * backend that did would never terminate */
            if (de.name[0] == '.' &&
                (de.name[1] == '\0' ||
                 (de.name[1] == '.' && de.name[2] == '\0')))
                continue;

            if (d->rel[0])
                rc = (snprintf(childrel, sizeof(childrel), "%s/%s",
                               d->rel, de.name) >= (int) sizeof(childrel))
                     ? -ENAMETOOLONG : 0;
            else
                rc = (snprintf(childrel, sizeof(childrel), "%s", de.name)
                      >= (int) sizeof(childrel)) ? -ENAMETOOLONG : 0;
            if (rc != 0) break;

            uint16_t type = de.mode & S_IFMT;
            if (type == S_IFDIR) {
                if (d->depth + 1 >= COPY_MAX_DEPTH) { rc = -ELOOP; break; }
                struct copy_dir* nd = copy_push(stack, childrel, d->depth + 1);
                if (!nd) { rc = -ENOMEM; break; }
                stack = nd;
            } else if (type == S_IFREG) {
                char cs[OPFTP_MAX_PATH], cd[OPFTP_MAX_PATH];
                rc = copy_join(cs, sizeof(cs), j->path, childrel);
                if (rc == 0)
                    rc = copy_join(cd, sizeof(cd), j->dst, childrel);
                if (rc == 0)
                    rc = copy_file(s, j, cs, cd, bytes);
                if (rc != 0) break;
            }
            /* anything else (device, fifo, symlink) is skipped */
        }
        fs->closedir(fs->ctx, dir);
        free(d);
        if (rc != 0)
            break;
    }

    copy_free_stack(stack);
    return rc;
}

static int transfer_copy(struct opftp_server* s, struct opftp_transfer_job* j,
                         uint64_t* bytes)
{
    const opftp_fs_t* fs = s->fs;

    /* Defense in depth: cmd_cpto rejects this, but the worker runs
     * later and opening the destination O_TRUNC would destroy the
     * source before a byte is read. */
    if (strcmp(j->path, j->dst) == 0)
        return -EINVAL;

    opftp_stat_t st;
    if (fs->stat(fs->ctx, j->path, &st) != 0)
        return -errno;

    if ((st.mode & S_IFMT) == S_IFDIR) {
        /* ponytail: total stays 0 (unknown) for a tree — sizing it up
         * front means walking the whole thing twice. */
        return copy_tree(s, j, bytes);
    }

    j->total = st.size;      /* progress denominator for the OSD */
    return copy_file(s, j, j->path, j->dst, bytes);
}

/* ---- worker entry ---- */

void opftp_transfer_run(struct opftp_server* s, struct opftp_transfer_job* j)
{
    int rc;
    uint64_t bytes = 0;

    /* Establish the data connection on the worker so a slow/broken
     * client can never stall the reactor (PASV accept / PORT connect,
     * both bounded + cancel-aware). COPY jobs have no data connection. */
    if (j->op != OPFTP_JOB_COPY) {
        rc = opftp_datachan_connect_job(s, j);
        if (rc != 0)
            goto out;               /* completion with the failure */
    }

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
    case OPFTP_JOB_COPY:
        rc = transfer_copy(s, j, &bytes);
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
                if (cr == OPFTP_TLS_HS_DONE)
                    break;
                if (cr != OPFTP_TLS_HS_WANT_READ && cr != OPFTP_TLS_HS_WANT_WRITE)
                    break;
                struct pollfd p = { .fd = j->data_fd,
                                    .events = (cr == OPFTP_TLS_HS_WANT_WRITE) ? POLLOUT : POLLIN };
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
    atomic_store_explicit(&j->bytes, bytes, memory_order_relaxed);
    j->result = rc;
    j->final_bytes = bytes;
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
