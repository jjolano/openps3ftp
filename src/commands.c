/*
 * Built-in FTP command handlers (RFC 959 + 3659 [MLSD/MLST/MFMT] +
 * 2640 + custom STOP).
 * All handlers run on the reactor thread and must not block on the fs
 * beyond quick stat/open calls; transfers are dispatched to workers.
 */
#include "opftp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Reply texts live in opftp.h: datachan.c sends 226/426/451/550 too,
 * and DESIGN.md requires the wording be identical everywhere. */

/* ---- helpers ---- */

static struct opftp_server* server_of(struct opftp_client* c)
{
    return c->server;
}

static void reply(struct opftp_client* c, int code, const char* msg)
{
    opftp_client_send_reply(c, code, msg);
}

/* 550 reply for an fs failure, with a message clients can classify:
 * ENOENT/ENOTDIR -> "No such file or directory." (FluentFTP etc. map
 * this to FtpMissingObjectException), EACCES/EPERM -> permission, and
 * a generic fallback. errno is set by the fs backend on failure. */
static void reply_fs_error(struct opftp_client* c)
{
    switch (errno) {
    case ENOENT:
    case ENOTDIR:
        reply(c, 550, "No such file or directory.");
        break;
    case EACCES:
    case EPERM:
        reply(c, 550, "Permission denied.");
        break;
    case EISDIR:
        reply(c, 550, "Is a directory.");
        break;
    default:
        reply(c, 550, R550);
        break;
    }
}

/* Parse a UTC timestamp "YYYYMMDDHHMMSS" (first 14 chars; anything
 * after, e.g. ".12 GMT", is ignored). Returns 0 + *out, or -1. */
static int parse_ftp_time(const char* s, time_t* out)
{
    for (int i = 0; i < 14; i++)
        if (!isdigit((unsigned char) s[i]))
            return -1;
    int yy = (s[0] - '0') * 1000 + (s[1] - '0') * 100 +
             (s[2] - '0') * 10 + (s[3] - '0');
    int mm = (s[4] - '0') * 10 + (s[5] - '0');
    int dd = (s[6] - '0') * 10 + (s[7] - '0');
    int hh = (s[8] - '0') * 10 + (s[9] - '0');
    int mi = (s[10] - '0') * 10 + (s[11] - '0');
    int ss = (s[12] - '0') * 10 + (s[13] - '0');
    if (mm < 1 || mm > 12 || dd < 1 || dd > 31 ||
        hh > 23 || mi > 59 || ss > 59)
        return -1;
    struct tm tm = {0};
    tm.tm_year = yy - 1900;
    tm.tm_mon = mm - 1;
    tm.tm_mday = dd;
    tm.tm_hour = hh;
    tm.tm_min = mi;
    tm.tm_sec = ss;
    tm.tm_isdst = 0;
#ifdef OPFTP_PS3
    /* newlib has no timegm(); ps3 has no timezone support, so mktime
     * with the default (UTC) TZ interprets the tm as UTC */
    time_t t = mktime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    if (t == (time_t) -1)
        return -1;
    *out = t;
    return 0;
}

/* Resolve a path argument against root+cwd; empty param -> cwd. */
static int resolve_arg(struct opftp_client* c, const char* param, char* out,
                       size_t outsz, bool* trailing)
{
    struct opftp_server* s = server_of(c);
    const char* arg = (param && param[0]) ? param : c->cwd;
    return opftp_path_resolve(s->root, c->cwd, arg, out, outsz, trailing);
}

static bool parent_is_dir(struct opftp_client* c, const char* path)
{
    struct opftp_server* s = server_of(c);
    char parent[OPFTP_MAX_PATH];
    if (opftp_path_parent(path, parent, sizeof(parent)) != 0)
        return false;
    if (parent[0] == '\0')
        return true;                    /* root: exists */
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, parent, &st) != 0)
        return false;
    return (st.mode & S_IFMT) == S_IFDIR;
}

/* True when `path` lies strictly inside directory `dir`. Both must be
 * canonical (absolute, no trailing slash except root). */
static bool path_is_under(const char* path, const char* dir)
{
    size_t n = strlen(dir);
    if (n == 1 && dir[0] == '/')        /* root contains everything else */
        return path[0] == '/' && path[1] != '\0';
    return strncmp(path, dir, n) == 0 && path[n] == '/';
}

/* Allocate a transfer job with its cancel pipe and client ref, or NULL.
 * Undo with job_free() while the job is still ours; after a successful
 * opftp_job_dispatch it belongs to the worker. */
static struct opftp_transfer_job* job_new(struct opftp_client* c,
                                          enum opftp_job_op op)
{
    struct opftp_transfer_job* j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
#ifdef OPFTP_PS3
    /* ps3 has no pipe(): cancellation relies on the atomic flag and
     * the worker's poll timeout */
    j->cancel_pipe[0] = j->cancel_pipe[1] = -1;
#else
    if (pipe(j->cancel_pipe) != 0) {
        free(j);
        return NULL;
    }
#endif
    j->client = c;
    opftp_client_retain(c);
    j->generation = c->generation;
    j->op = op;
    j->data_fd = -1;
    j->pasv_fd = -1;
    atomic_init(&j->cancelled, false);
    return j;
}

static void job_free(struct opftp_transfer_job* j)
{
#ifndef OPFTP_PS3
    if (j->cancel_pipe[0] >= 0) {
        close(j->cancel_pipe[0]);
        close(j->cancel_pipe[1]);
    }
#endif
    if (j->pasv_fd >= 0)
        opftp_close_fd(j->pasv_fd);
    opftp_client_release(j->client);
    free(j);
}

/* ---- login ---- */

static void cmd_user(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (!param || !param[0]) { reply(c, 501, R501); return; }
    snprintf(c->user, sizeof(c->user), "%s", param);
    char buf[256];
    snprintf(buf, sizeof(buf), R331, c->user);
    reply(c, 331, buf);
}

static void cmd_pass(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (c->user[0] == '\0') { reply(c, 503, R503); return; }
    bool ok = true;
    if (s->cb.auth)
        ok = s->cb.auth(s->cb.auth_ctx, c->user, param ? param : "");
    if (ok) {
        c->logged_in = true;
        char buf[256];
        snprintf(buf, sizeof(buf), "Successfully logged in as %s.", c->user);
        reply(c, 230, buf);
    } else {
        c->user[0] = '\0';
        reply(c, 530, R530);
    }
}

/* ---- session ---- */

static void cmd_quit(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 221, R221);
    c->disconnect_requested = true;
}

static void cmd_noop(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 200, R200);
}

static void cmd_syst(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 215, R215);
}

static void cmd_feat(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    opftp_client_send(c, "211-Features:");
    opftp_client_send(c, " UTF8");
    opftp_client_send(c, " SIZE");
    opftp_client_send(c, " MDTM");
    opftp_client_send(c, " MLSD");
    opftp_client_send(c, " MLST type*;size*;modify*;perm*;");
    opftp_client_send(c, " MFMT");
    opftp_client_send(c, " REST STREAM");
    opftp_client_send(c, " EPSV");
    opftp_client_send(c, " EPRT");
    opftp_client_send(c, " CPFR");
    opftp_client_send(c, " CPTO");
    if (server_of(c)->tls) {
        opftp_client_send(c, " AUTH TLS");
        opftp_client_send(c, " PBSZ");
        opftp_client_send(c, " PROT");
    }
    opftp_client_send(c, "211 End");
}

static void cmd_help(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 214, "The following commands are recognized.");
}

static void cmd_opts(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    /* RFC 2640: only "OPTS UTF8 ON" is defined */
    if (param && strncasecmp(param, "UTF8", 4) == 0) {
        const char* rest = param + 4;
        while (*rest == ' ') rest++;
        if (strcasecmp(rest, "ON") == 0) {
            reply(c, 200, R200);
            return;
        }
    }
    reply(c, 501, R501);
}

static void cmd_acct(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 202, R202);   /* accounts not used */
}

static void cmd_allo(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 202, R202);
}

static void cmd_type(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    /* RFC 959: TYPE = "A" [SP N|T|C] | "I" | "E" [SP N|T|C] | "L" [SP size].
     * Command verb is case-insensitive; the format arg is not.
     * We transfer 8-bit data only: "A" is accepted (clients like
     * ftplib's retrlines send it for text listings) but no CRLF
     * conversion is performed — that is a documented deviation.
     * E (EBCDIC) and L (local byte size) are not supported. */
    if (!param || !param[0]) {
        reply(c, 501, R501);
        return;
    }
    char t = (char) toupper((unsigned char) param[0]);
    const char* rest = param + 1;
    while (*rest == ' ') rest++;
    if (t == 'I') {
        if (*rest != '\0') { reply(c, 504, R504); return; }
        reply(c, 200, R200);
        return;
    }
    if (t == 'A') {
        /* optional format control: N (non-print), T (telnet), C (carriage) */
        if (*rest == '\0' ||
            ((*rest == 'N' || *rest == 'T' || *rest == 'C') && rest[1] == '\0')) {
            reply(c, 200, R200);
            return;
        }
        reply(c, 504, R504);
        return;
    }
    reply(c, 504, R504);   /* E, L unsupported */
}

static void cmd_mode(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (param && param[0] == 'S')
        reply(c, 200, R200);
    else
        reply(c, 504, R504);
}

static void cmd_stru(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (param && param[0] == 'F')
        reply(c, 200, R200);
    else
        reply(c, 504, R504);
}

static void cmd_stop(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    reply(c, 200, R200);
    atomic_store_explicit(&server_of(c)->stopping, true, memory_order_relaxed);
}

/* ---- paths ---- */

static void cmd_pwd(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    char buf[OPFTP_MAX_PATH + 16];
    snprintf(buf, sizeof(buf), "\"%s\"", c->cwd);
    reply(c, 257, buf);
}

static void cmd_cwd(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0 ||
        (st.mode & S_IFMT) != S_IFDIR) {
        reply_fs_error(c);
        return;
    }
    /* store the FTP-visible cwd (root-relative) */
    if (strcmp(path, s->root) == 0) {
        strcpy(c->cwd, "/");
    } else {
        snprintf(c->cwd, sizeof(c->cwd), "%s", path + strlen(s->root));
    }
    reply(c, 250, R250);
}

static void cmd_cdup(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    cmd_cwd(c, "..", NULL);
    (void) param;
}

static void cmd_mkd(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!param || !param[0]) { reply(c, 501, R501); return; }
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0 ||
        !parent_is_dir(c, path)) {
        reply_fs_error(c);
        return;
    }
    if (s->fs->mkdir(s->fs->ctx, path, 0755) != 0) {
        reply_fs_error(c);
        return;
    }
    char buf[OPFTP_MAX_PATH + 16];
    snprintf(buf, sizeof(buf), "\"%s\"", path);
    reply(c, 257, buf);
}

static void cmd_rmd(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    if (s->fs->rmdir(s->fs->ctx, path) != 0) {
        reply_fs_error(c);
        return;
    }
    reply(c, 250, R250);
}

static void cmd_dele(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    if (s->fs->unlink(s->fs->ctx, path) != 0) {
        reply_fs_error(c);
        return;
    }
    reply(c, 250, R250);
}

static void cmd_rnfr(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
        reply_fs_error(c);
        return;
    }
    snprintf(c->rnfr, sizeof(c->rnfr), "%s", path);
    c->have_rnfr = true;
    reply(c, 350, R350A);
}

static void cmd_rnto(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!c->have_rnfr) { reply(c, 503, R503); return; }
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0 ||
        !parent_is_dir(c, path)) {
        c->have_rnfr = false;
        reply_fs_error(c);
        return;
    }
    c->have_rnfr = false;
    if (s->fs->rename(s->fs->ctx, c->rnfr, path) != 0) {
        reply_fs_error(c);
        return;
    }
    reply(c, 250, R250);
}

/* ---- CPFR / CPTO: server-side copy (draft-bharat-ftp-copy-command) ---- */

/* CPFR <path>: remember the source; reply 350 (or 550 if missing).
 * A file or a whole directory tree may be named. The actual copy runs
 * on a worker via OPFTP_JOB_COPY when CPTO arrives, so multi-GB game
 * copies don't block the reactor. */
static void cmd_cpfr(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    uint16_t type = 0;
    if (s->fs->stat(s->fs->ctx, path, &st) == 0)
        type = st.mode & S_IFMT;
    if (type != S_IFREG && type != S_IFDIR) {
        reply_fs_error(c);
        return;
    }
    snprintf(c->cpfr, sizeof(c->cpfr), "%s", path);
    c->have_cpfr = true;
    reply(c, 350, type == S_IFDIR
                  ? "Directory exists, ready for destination name."
                  : "File exists, ready for destination name.");
}

static void cmd_cpto(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!c->have_cpfr) { reply(c, 503, R503); return; }
    if (c->job) { reply(c, 450, R450); return; }
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0 ||
        !parent_is_dir(c, path)) {
        c->have_cpfr = false;
        reply_fs_error(c);
        return;
    }
    c->have_cpfr = false;

    /* Same path: the copy opens the destination O_TRUNC, which would
     * destroy the source before a single byte is read. Reject before
     * any fd is opened. */
    if (strcmp(c->cpfr, path) == 0) {
        reply(c, 550, "Source and destination are the same file.");
        return;
    }
    /* Destination inside the source tree: a recursive copy would walk
     * into what it is writing, forever. */
    if (path_is_under(path, c->cpfr)) {
        reply(c, 550, "Destination is inside the source directory.");
        return;
    }

    /* COPY has no data connection: job_new leaves pasv_fd/data_fd at -1 */
    struct opftp_transfer_job* j = job_new(c, OPFTP_JOB_COPY);
    if (!j) { reply(c, 451, R451); return; }
    snprintf(j->path, sizeof(j->path), "%s", c->cpfr);
    snprintf(j->dst, sizeof(j->dst), "%s", path);

    if (opftp_job_dispatch(s, j) != 0) {
        job_free(j);
        reply(c, 451, R451);
        return;
    }
    c->job = j;
    /* no immediate reply: 250/550 arrives with the job completion */
}

static void cmd_site(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!param || !param[0]) {
        reply(c, 502, R502);
        return;
    }
    if (strncasecmp(param, "CHMOD", 5) == 0) {
        /* SITE CHMOD <mode> <path> */
        const char* p = param + 5;
        while (*p == ' ') p++;
        char* end = NULL;
        long mode = strtol(p, &end, 8);
        if (end == p || !end || *end != ' ') {
            reply(c, 501, R501);
            return;
        }
        while (*end == ' ') end++;
        char path[OPFTP_MAX_PATH];
        if (resolve_arg(c, end, path, sizeof(path), NULL) != 0) {
            reply(c, 501, R501);
            return;
        }
        if (s->fs->chmod(s->fs->ctx, path, (uint16_t) mode) != 0) {
            reply_fs_error(c);
            return;
        }
        reply(c, 200, R200);
        return;
    }
    if (strncasecmp(param, "UTIME", 5) == 0) {
        /* SITE UTIME <path> <YYYYMMDDHHMMSS>[.frac][ GMT] — the path may
         * contain spaces; the time token is the LAST whitespace-separated
         * token starting with 14 digits. */
        const char* p = param + 5;
        while (*p == ' ') p++;
        const char* tok = NULL;       /* start of the time token */
        const char* path_end = NULL;  /* space before it */
        const char* cur = p;
        while (*cur) {
            while (*cur == ' ') cur++;
            if (!*cur) break;
            const char* start = cur;
            while (*cur && *cur != ' ') cur++;
            if ((size_t) (cur - start) >= 14) {
                int digits = 1;
                for (int i = 0; i < 14; i++)
                    if (!isdigit((unsigned char) start[i])) { digits = 0; break; }
                if (digits) { tok = start; path_end = start - 1; }
            }
        }
        if (!tok || path_end <= p) {
            reply(c, 501, R501);
            return;
        }
        while (path_end > p && *path_end == ' ') path_end--;
        char pathbuf[OPFTP_MAX_PATH];
        size_t plen = (size_t) (path_end - p) + 1;
        if (plen >= sizeof(pathbuf)) {
            reply(c, 501, R501);
            return;
        }
        memcpy(pathbuf, p, plen);
        pathbuf[plen] = '\0';
        time_t t;
        if (parse_ftp_time(tok, &t) != 0) {
            reply(c, 501, R501);
            return;
        }
        char path[OPFTP_MAX_PATH];
        if (resolve_arg(c, pathbuf, path, sizeof(path), NULL) != 0) {
            reply(c, 501, R501);
            return;
        }
        opftp_stat_t st;
        if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
            reply_fs_error(c);
            return;
        }
        if (s->fs->utimes(s->fs->ctx, path, (int64_t) t) != 0) {
            reply_fs_error(c);
            return;
        }
        reply(c, 200, R200);
        return;
    }
    /* SITE STOP: server shutdown — opt-in only (same gate as the bare
     * STOP alias), never a default. */
    if (strncasecmp(param, "STOP", 4) == 0) {
        const char* rest = param + 4;
        while (*rest == ' ') rest++;
        if (*rest != '\0') {
            reply(c, 501, R501);
            return;
        }
        if (!s->allow_stop) {
            reply(c, 502, R502);
            return;
        }
        cmd_stop(c, param, ctx);
        return;
    }
    reply(c, 502, R502);
}

/* ---- stat-like ---- */

static void cmd_size(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0 ||
        (st.mode & S_IFMT) != S_IFREG) {
        reply_fs_error(c);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long) st.size);
    reply(c, 213, buf);
}

static void cmd_mdtm(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
        reply_fs_error(c);
        return;
    }
    time_t t = (time_t) st.mtime;
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) {
        reply_fs_error(c);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    reply(c, 213, buf);
}

static void cmd_rest(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (!param || !param[0]) { reply(c, 501, R501); return; }
    char* end = NULL;
    unsigned long long v = strtoull(param, &end, 10);
    if (end == param || *end != '\0') {
        reply(c, 554, R554);
        return;
    }
    c->rest = v;
    char buf[64];
    snprintf(buf, sizeof(buf), "Restarting at %llu.", (unsigned long long) v);
    reply(c, 350, buf);
}

static void cmd_stat(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!param || !param[0]) {
        char buf[OPFTP_MAX_PATH + 64];
        snprintf(buf, sizeof(buf), "211-Status of server: cwd=%s", c->cwd);
        opftp_client_send(c, buf);
        opftp_client_send(c, "211 End");
        return;
    }
    /* STAT <path>: control-channel listing */
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
        reply_fs_error(c);
        return;
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "211-Status of %s:", path);
    opftp_client_send(c, buf);
    if ((st.mode & S_IFMT) == S_IFDIR) {
        void* dir = NULL;
        if (s->fs->opendir(s->fs->ctx, path, &dir) == 0) {
            opftp_dirent_t de;
            while (s->fs->readdir(s->fs->ctx, dir, &de) == 1) {
                int n = opftp_listing_format(buf, sizeof(buf), &de, NULL);
                opftp_client_send_raw(c, buf);
                (void) n;
            }
            s->fs->closedir(s->fs->ctx, dir);
        }
    } else {
        opftp_dirent_t de;
        memset(&de, 0, sizeof(de));
        const char* slash = strrchr(path, '/');
        snprintf(de.name, sizeof(de.name), "%s", slash ? slash + 1 : path);
        de.mode = st.mode;
        de.size = st.size;
        de.mtime = st.mtime;
        de.uid = st.uid;
        de.gid = st.gid;
        opftp_listing_format(buf, sizeof(buf), &de, NULL);
        opftp_client_send_raw(c, buf);
    }
    opftp_client_send(c, "211 End");
}

/* ---- data channel setup ---- */

static void cmd_pasv(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    struct opftp_server* s = server_of(c);
    /* PASV is IPv4-only: the advertised address must be a plain v4
     * address. On a dual-stack control connection the local address
     * is v4-mapped — extract the v4 part; a pure v6 connection must
     * use EPSV. */
    opftp_sockaddr_storage lss = {0};
    socklen_t lsl = sizeof(lss);
    getsockname(c->fd, (struct sockaddr*) &lss, &lsl);
    struct sockaddr_in local4 = {0};
    if (opftp_sockaddr_family(&lss) == AF_INET) {
        memcpy(&local4, &lss, sizeof(local4));
    } else if (opftp_sockaddr_family(&lss) == AF_INET6) {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*) &lss;
        if (!opftp_is_v4mapped(&a6->sin6_addr)) {
            reply(c, 522, "Network protocol not supported, use (1)");
            return;
        }
        local4.sin_family = AF_INET;
        memcpy(&local4.sin_addr, &a6->sin6_addr.s6_addr[12], 4);
        local4.sin_port = a6->sin6_port;
    } else {
        reply(c, 425, R425);
        return;
    }

    if (c->pasv_fd >= 0) {
        opftp_close_fd(c->pasv_fd);
        c->pasv_fd = -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { reply(c, 425, R425); return; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    /* configured range: try each port so a firewall/Docker mapping can
     * predict the listener (0,0 = ephemeral, one bind attempt) */
    unsigned lo = s->pasv_min, hi = s->pasv_max;
    if (lo == 0 || hi < lo) { lo = hi = 0; }
    unsigned cand = lo;
    bool bound = false;
    while (cand <= hi || (lo == 0 && cand == 0)) {
        addr.sin_port = htons((uint16_t) cand);
        if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) == 0) {
            bound = true;
            break;
        }
        if (lo == 0)
            break;              /* ephemeral: single attempt */
        cand++;
        if (cand > hi) {
            /* out of range: fall back to ephemeral rather than fail */
            addr.sin_port = 0;
            if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) == 0)
                bound = true;
            break;
        }
    }
    if (!bound || listen(fd, 1) != 0) {
        int e = errno;
        opftp_close_fd(fd);
        errno = e;
        reply(c, 425, R425);
        return;
    }
    socklen_t sl = sizeof(addr);
    getsockname(fd, (struct sockaddr*) &addr, &sl);

    if (local4.sin_addr.s_addr == 0)
        local4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    c->pasv_fd = fd;
    c->have_data_peer = false;

    uint8_t* ip = (uint8_t*) &local4.sin_addr.s_addr;
    unsigned p = ntohs(addr.sin_port);
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
             ip[0], ip[1], ip[2], ip[3], (p >> 8) & 0xff, p & 0xff);
    reply(c, 227, buf);
}

static void cmd_port(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    unsigned tuple[6];
    if (!param) { reply(c, 501, R501); return; }
    int n = sscanf(param, "%u,%u,%u,%u,%u,%u",
                   &tuple[0], &tuple[1], &tuple[2],
                   &tuple[3], &tuple[4], &tuple[5]);
    if (n != 6 || tuple[0] > 255 || tuple[1] > 255 || tuple[2] > 255 ||
        tuple[3] > 255 || tuple[4] > 255 || tuple[5] > 255) {
        reply(c, 501, R501);
        return;
    }
    struct sockaddr_in* dst = (struct sockaddr_in*) &c->data_peer;
    memset(dst, 0, sizeof(*dst));
    dst->sin_family = AF_INET;
    dst->sin_addr.s_addr = htonl((tuple[0] << 24) | (tuple[1] << 16) |
                                 (tuple[2] << 8) | tuple[3]);
    dst->sin_port = htons((uint16_t) ((tuple[4] << 8) | tuple[5]));
    c->data_peerlen = sizeof(*dst);
    c->have_data_peer = true;
    if (c->pasv_fd >= 0) { opftp_close_fd(c->pasv_fd); c->pasv_fd = -1; }
    reply(c, 200, R200);
}

/* EPSV: extended passive mode (RFC 2428). Listener is dual-stack so
 * it serves both v4-mapped and pure v6 data connections. */
static void cmd_epsv(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (param && param[0]) {
        int proto = atoi(param);
        int ctrl = (opftp_sockaddr_family(&c->peer) == AF_INET6 &&
                    !opftp_is_v4mapped(&((struct sockaddr_in6*) &c->peer)->sin6_addr))
                   ? 2 : 1;
        if (proto != ctrl) {
            reply(c, 522, "Network protocol not supported, use (1,2)");
            return;
        }
    }
    if (c->pasv_fd >= 0) {
        opftp_close_fd(c->pasv_fd);
        c->pasv_fd = -1;
    }
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    bool v6 = (fd >= 0);
    if (!v6)
        fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { reply(c, 425, R425); return; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    opftp_sockaddr_storage addr = {0};
    socklen_t alen;
    uint16_t port;
    if (v6) {
#ifdef IPV6_V6ONLY
        int zero = 0;                    /* dual-stack: serve v4 too */
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
#endif
        struct sockaddr_in6* a6 = (struct sockaddr_in6*) &addr;
        a6->sin6_family = AF_INET6;
        a6->sin6_addr = in6addr_any;
        a6->sin6_port = 0;
        alen = sizeof(*a6);
    } else {
        struct sockaddr_in* a4 = (struct sockaddr_in*) &addr;
        a4->sin_family = AF_INET;
        a4->sin_addr.s_addr = htonl(INADDR_ANY);
        a4->sin_port = 0;
        alen = sizeof(*a4);
    }

    /* configured range: try each port so a firewall/Docker mapping can
     * predict the listener (0,0 = ephemeral, one bind attempt) */
    unsigned lo = s->pasv_min, hi = s->pasv_max;
    if (lo == 0 || hi < lo) { lo = hi = 0; }
    unsigned p = lo;
    bool bound = false;
    while (p <= hi || (lo == 0 && p == 0)) {
        if (v6)
            ((struct sockaddr_in6*) &addr)->sin6_port = htons((uint16_t) p);
        else
            ((struct sockaddr_in*) &addr)->sin_port = htons((uint16_t) p);
        if (bind(fd, (struct sockaddr*) &addr, alen) == 0) {
            bound = true;
            break;
        }
        if (lo == 0)
            break;              /* ephemeral: single attempt */
        p++;
        if (p > hi) {
            /* out of range: fall back to ephemeral rather than fail */
            if (v6)
                ((struct sockaddr_in6*) &addr)->sin6_port = 0;
            else
                ((struct sockaddr_in*) &addr)->sin_port = 0;
            if (bind(fd, (struct sockaddr*) &addr, alen) == 0)
                bound = true;
            break;
        }
    }
    if (!bound || listen(fd, 1) != 0) {
        opftp_close_fd(fd);
        reply(c, 425, R425);
        return;
    }
    socklen_t sl = alen;
    getsockname(fd, (struct sockaddr*) &addr, &sl);
    port = ntohs(v6 ? ((struct sockaddr_in6*) &addr)->sin6_port
                    : ((struct sockaddr_in*) &addr)->sin_port);

    c->pasv_fd = fd;
    c->have_data_peer = false;
    char buf[64];
    snprintf(buf, sizeof(buf), "Entering Extended Passive Mode (|||%u|)", port);
    reply(c, 229, buf);
}

/* EPRT: extended port (RFC 2428): |proto|addr|port| */
static void cmd_eprt(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (!param || param[0] != '|') { reply(c, 501, R501); return; }
    char proto[8], addr[128], port[16];
    if (sscanf(param, "|%7[^|]|%127[^|]|%15[^|]|", proto, addr, port) != 3) {
        reply(c, 501, R501);
        return;
    }
    int p = atoi(proto);
    long pnum = strtol(port, NULL, 10);
    if (pnum < 1 || pnum > 65535) { reply(c, 501, R501); return; }

    opftp_sockaddr_storage* dst = &c->data_peer;
    memset(dst, 0, sizeof(*dst));
    if (p == 1) {
        struct sockaddr_in* a4 = (struct sockaddr_in*) dst;
        a4->sin_family = AF_INET;
        if (inet_pton(AF_INET, addr, &a4->sin_addr) != 1) {
            reply(c, 501, R501);
            return;
        }
        a4->sin_port = htons((uint16_t) pnum);
        c->data_peerlen = sizeof(*a4);
    } else if (p == 2) {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*) dst;
        a6->sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, addr, &a6->sin6_addr) != 1) {
            reply(c, 501, R501);
            return;
        }
        a6->sin6_port = htons((uint16_t) pnum);
        c->data_peerlen = sizeof(*a6);
    } else {
        reply(c, 522, "Network protocol not supported, use (1,2)");
        return;
    }
    c->have_data_peer = true;
    if (c->pasv_fd >= 0) { opftp_close_fd(c->pasv_fd); c->pasv_fd = -1; }
    reply(c, 200, R200);
}

static void cmd_auth(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!param || strcasecmp(param, "TLS") != 0) {
        reply(c, 504, R504);
        return;
    }
    if (!s->tls) {
        reply(c, 502, R502);
        return;
    }
    if (c->tls) {
        reply(c, 503, R503);
        return;
    }
    if (opftp_client_start_tls(c) != 0) {
        reply(c, 421, R421);
        c->disconnect_requested = true;
        return;
    }
    /* RFC 4217 practice: 234 precedes the handshake (plaintext), so
     * clients like ftplib can read it before wrapping the socket. */
    reply(c, 234, "AUTH TLS successful.");
}

static void cmd_pbsz(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (!c->tls) {
        reply(c, 503, R503);
        return;
    }
    /* PBSZ 0 is the only meaningful value; accept any integer syntax */
    if (!param || !param[0]) {
        reply(c, 501, R501);
        return;
    }
    reply(c, 200, R200);
}

static void cmd_prot(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    if (!c->tls) {
        reply(c, 503, R503);
        return;
    }
    if (param && param[0] == 'P' && param[1] == '\0') {
        c->tls_prot = 1;            /* PROT P: private (TLS data) */
        reply(c, 200, R200);
        return;
    }
    if (param && param[0] == 'C' && param[1] == '\0') {
        c->tls_prot = 0;            /* PROT C: clear */
        reply(c, 200, R200);
        return;
    }
    reply(c, 504, R504);
}

/* ---- transfers ---- */

static void cmd_abor(struct opftp_client* c, const char* param, void* ctx)
{
    (void) param; (void) ctx;
    if (c->job) {
        atomic_store_explicit(&c->job->cancelled, true, memory_order_relaxed);
#ifndef OPFTP_PS3
        char ch = 'x';
        ssize_t w = write(c->job->cancel_pipe[1], &ch, 1);
        (void) w;
#endif
        c->abor_pending = true;     /* 426 + 226 arrive with completion */
    } else {
        reply(c, 226, "Abort successful.");
    }
}

/* Build + dispatch a transfer job. Replies 150 on success; 450/425
 * otherwise. pre-check failures (missing target) reply 550. */
static void start_transfer(struct opftp_client* c, enum opftp_job_op op,
                           const char* path, bool nlst, bool mlsd,
                           const char* reply150)
{
    struct opftp_server* s = server_of(c);

    if (c->job) {
        reply(c, 450, R450);
        return;
    }
    /* PORT/EPRT bounce: pure address compare, no blocking, and must
     * reply 425 BEFORE any 150. PASV-peer bounce is checked by the
     * worker after accept (no test relies on it replying first). */
    if (opftp_datachan_precheck(c) != 0) {
        reply(c, 425, R425);
        return;
    }
    if (c->pasv_fd < 0 && !c->have_data_peer) {
        reply(c, 425, R425);   /* no PASV, no PORT */
        return;
    }

    struct opftp_transfer_job* j = job_new(c, op);
    if (!j) {
        reply(c, 425, R425);
        return;
    }
    j->nlst = nlst;
    j->mlsd = mlsd;
    j->rest = c->rest;
    j->need_tls = (c->tls_prot != 0);   /* PROT P: TLS data channel */
    snprintf(j->path, sizeof(j->path), "%s", path);
    j->pasv_fd = c->pasv_fd;             /* ownership moves to the job */
    c->pasv_fd = -1;
    j->data_peer = c->data_peer;
    j->data_peerlen = c->data_peerlen;
    j->have_data_peer = c->have_data_peer;
    c->have_data_peer = false;
    j->ctl_peer = c->peer;               /* control peer for bounce check */
    j->ctl_peerlen = c->peerlen;

    if (opftp_job_dispatch(s, j) != 0) {
        job_free(j);
        reply(c, 425, R425);
        return;
    }
    c->job = j;
    c->rest = 0;
    reply(c, 150, reply150 ? reply150 : R150);
}

/* LIST / NLST: param may be a path or "-<flags>" style; empty -> cwd. */
/* LIST / NLST / MLSD differ only in the listing format the worker
 * emits: same argument parsing, same "-flags" tolerance, same checks. */
static void start_listing(struct opftp_client* c, const char* param,
                          bool nlst, bool mlsd)
{
    struct opftp_server* s = server_of(c);
    const char* arg = param;
    if (arg && arg[0] == '-') {          /* "LIST -la /path" */
        arg = strchr(arg, ' ');
        if (arg) while (*arg == ' ') arg++;
    }
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, arg, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
        reply_fs_error(c);
        return;
    }
    start_transfer(c, OPFTP_JOB_LIST, path, nlst, mlsd, NULL);
}

static void cmd_list(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    start_listing(c, param, false, false);
}

static void cmd_nlst(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    start_listing(c, param, true, false);
}

static void cmd_retr(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0 ||
        (st.mode & S_IFMT) != S_IFREG) {
        reply_fs_error(c);
        return;
    }
    start_transfer(c, OPFTP_JOB_RETR, path, false, false, NULL);
}

static void cmd_stor(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    if (!parent_is_dir(c, path)) {
        reply_fs_error(c);
        return;
    }
    start_transfer(c, OPFTP_JOB_STOR, path, false, false, NULL);
}

static void cmd_appe(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    if (!parent_is_dir(c, path)) {
        reply_fs_error(c);
        return;
    }
    start_transfer(c, OPFTP_JOB_APPE, path, false, false, NULL);
}

/* ---- RFC 3659: MLSD / MLST / MFMT, RFC 959 STOU ---- */

static void cmd_mlsd(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    start_listing(c, param, false, true);
}

static void cmd_mlst(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!param || !param[0]) { reply(c, 501, R501); return; }
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
        reply_fs_error(c);
        return;
    }
    opftp_dirent_t de;
    memset(&de, 0, sizeof(de));
    const char* slash = strrchr(path, '/');
    snprintf(de.name, sizeof(de.name), "%s", slash ? slash + 1 : path);
    de.mode = st.mode;
    de.size = st.size;
    de.mtime = st.mtime;
    de.uid = st.uid;
    de.gid = st.gid;
    char line[1024];
    opftp_listing_format_mlsd(line, sizeof(line), &de);
    opftp_client_send(c, "250-Start of list");
    char mlstline[1025];
    snprintf(mlstline, sizeof(mlstline), " %s", line);   /* RFC 3659 7.2 */
    opftp_client_send_raw(c, mlstline);
    reply(c, 250, "End");
}

static void cmd_mfmt(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    /* MFMT YYYYMMDDHHMMSS <path> (path may contain spaces) */
    if (!param || strlen(param) < 15 || param[14] != ' ') {
        reply(c, 501, R501);
        return;
    }
    time_t t;
    if (parse_ftp_time(param, &t) != 0) {
        reply(c, 501, R501);
        return;
    }
    const char* arg = param + 15;
    while (*arg == ' ') arg++;
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, arg, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, path, &st) != 0) {
        reply_fs_error(c);
        return;
    }
    if (s->fs->utimes(s->fs->ctx, path, (int64_t) t) != 0) {
        reply_fs_error(c);
        return;
    }
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) {
        reply_fs_error(c);
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Modify=%04d%02d%02d%02d%02d%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    reply(c, 213, buf);
}

static void cmd_stou(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    (void) param;   /* RFC 959 allows a name hint; ignored -> cwd */
    struct opftp_server* s = server_of(c);
    static unsigned seq = 0;
    if (seq == 0)
        seq = (unsigned) time(NULL) % 1000000;   /* reactor thread: single-threaded */

    char name[OPFTP_MAX_PATH];
    opftp_stat_t st;
    for (int i = 0; i < 10; i++) {
        char rel[32];
        snprintf(rel, sizeof(rel), "ftp%06u", seq++);
        if (resolve_arg(c, rel, name, sizeof(name), NULL) != 0) {
            reply(c, 550, R550);
            return;
        }
        if (s->fs->stat(s->fs->ctx, name, &st) != 0 && errno == ENOENT)
            break;
        if (i == 9) {
            reply(c, 450, "No unique file name available.");
            return;
        }
    }
    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    char reply150[64];
    snprintf(reply150, sizeof(reply150), "FILE: %s", base);
    start_transfer(c, OPFTP_JOB_STOR, name, false, false, reply150);
}

/* ---- registration ---- */

void opftp_commands_init(struct opftp_server* s)
{
    opftp_dispatch_register(s, "USER", cmd_user, NULL);
    opftp_dispatch_register(s, "PASS", cmd_pass, NULL);
    opftp_dispatch_register(s, "QUIT", cmd_quit, NULL);
    opftp_dispatch_register(s, "NOOP", cmd_noop, NULL);
    opftp_dispatch_register(s, "SYST", cmd_syst, NULL);
    opftp_dispatch_register(s, "FEAT", cmd_feat, NULL);
    opftp_dispatch_register(s, "HELP", cmd_help, NULL);
    opftp_dispatch_register(s, "OPTS", cmd_opts, NULL);
    opftp_dispatch_register(s, "ACCT", cmd_acct, NULL);
    opftp_dispatch_register(s, "ALLO", cmd_allo, NULL);
    opftp_dispatch_register(s, "TYPE", cmd_type, NULL);
    opftp_dispatch_register(s, "MODE", cmd_mode, NULL);
    opftp_dispatch_register(s, "STRU", cmd_stru, NULL);
    /* STOP shuts the server down from an FTP client — opt-in only
     * (DESIGN.md "Protocol support"), never a default. */
    if (s->allow_stop)
        opftp_dispatch_register(s, "STOP", cmd_stop, NULL);
    opftp_dispatch_register(s, "PWD", cmd_pwd, NULL);
    opftp_dispatch_register(s, "XPWD", cmd_pwd, NULL);
    opftp_dispatch_register(s, "CWD", cmd_cwd, NULL);
    opftp_dispatch_register(s, "XCWD", cmd_cwd, NULL);
    opftp_dispatch_register(s, "CDUP", cmd_cdup, NULL);
    opftp_dispatch_register(s, "XCUP", cmd_cdup, NULL);
    opftp_dispatch_register(s, "MKD", cmd_mkd, NULL);
    opftp_dispatch_register(s, "XMKD", cmd_mkd, NULL);
    opftp_dispatch_register(s, "RMD", cmd_rmd, NULL);
    opftp_dispatch_register(s, "XRMD", cmd_rmd, NULL);
    opftp_dispatch_register(s, "DELE", cmd_dele, NULL);
    opftp_dispatch_register(s, "RNFR", cmd_rnfr, NULL);
    opftp_dispatch_register(s, "RNTO", cmd_rnto, NULL);
    opftp_dispatch_register(s, "CPFR", cmd_cpfr, NULL);
    opftp_dispatch_register(s, "CPTO", cmd_cpto, NULL);
    opftp_dispatch_register(s, "SITE", cmd_site, NULL);
    opftp_dispatch_register(s, "SIZE", cmd_size, NULL);
    opftp_dispatch_register(s, "MDTM", cmd_mdtm, NULL);
    opftp_dispatch_register(s, "REST", cmd_rest, NULL);
    opftp_dispatch_register(s, "STAT", cmd_stat, NULL);
    opftp_dispatch_register(s, "PASV", cmd_pasv, NULL);
    opftp_dispatch_register(s, "PORT", cmd_port, NULL);
    opftp_dispatch_register(s, "EPSV", cmd_epsv, NULL);
    opftp_dispatch_register(s, "EPRT", cmd_eprt, NULL);
    opftp_dispatch_register(s, "AUTH", cmd_auth, NULL);
    opftp_dispatch_register(s, "PBSZ", cmd_pbsz, NULL);
    opftp_dispatch_register(s, "PROT", cmd_prot, NULL);
    opftp_dispatch_register(s, "ABOR", cmd_abor, NULL);
    opftp_dispatch_register(s, "LIST", cmd_list, NULL);
    opftp_dispatch_register(s, "NLST", cmd_nlst, NULL);
    opftp_dispatch_register(s, "MLSD", cmd_mlsd, NULL);
    opftp_dispatch_register(s, "MLST", cmd_mlst, NULL);
    opftp_dispatch_register(s, "MFMT", cmd_mfmt, NULL);
    opftp_dispatch_register(s, "STOU", cmd_stou, NULL);
    opftp_dispatch_register(s, "RETR", cmd_retr, NULL);
    opftp_dispatch_register(s, "STOR", cmd_stor, NULL);
    opftp_dispatch_register(s, "APPE", cmd_appe, NULL);
}
