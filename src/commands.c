/*
 * Built-in FTP command handlers (RFC 959 + 3659 + 2640 + custom STOP).
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

/* Reply texts (kept verbatim from the original const.h). */
#define R150  "Accepted data connection."
#define R200  "OK."
#define R202  "Already logged in."
#define R215  "UNIX Type: L8"
#define R221  "Bye."
#define R226  "Transfer complete."
#define R250  "File operation successful."
#define R331  "Username %s OK. Password required."
#define R350A "Ready for next command."
#define R421  "Closing control connection."
#define R425  "Cannot open data connection."
#define R426  "Connection closed; transfer aborted."
#define R450  "Another data transfer is already in progress."
#define R451  "Data transfer error (network)."
#define R501  "Bad command syntax."
#define R502  "Command not implemented."
#define R503  "Bad command usage."
#define R504  "Parameter not accepted."
#define R530  "Not logged in."
#define R550  "Cannot access specified file or directory."
#define R554  "Invalid REST restart point."

/* ---- helpers ---- */

static struct opftp_server* server_of(struct opftp_client* c)
{
    return c->server;
}

static int reply(struct opftp_client* c, int code, const char* msg)
{
    opftp_client_send_reply(c, code, msg);
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

static int parent_exists(struct opftp_client* c, const char* path)
{
    struct opftp_server* s = server_of(c);
    char parent[OPFTP_MAX_PATH];
    if (opftp_path_parent(path, parent, sizeof(parent)) != 0)
        return -1;
    if (parent[0] == '\0')
        return 0;                       /* root: exists */
    opftp_stat_t st;
    if (s->fs->stat(s->fs->ctx, parent, &st) != 0)
        return -1;
    return ((st.mode & S_IFMT) == S_IFDIR) ? 0 : -1;
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
    if (param && (param[0] == 'A' || param[0] == 'I'))
        reply(c, 200, R200);
    else
        reply(c, 504, R504);
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
        reply(c, 550, R550);
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
        parent_exists(c, path) != 0) {
        reply(c, 550, R550);
        return;
    }
    if (s->fs->mkdir(s->fs->ctx, path, 0755) != 0) {
        reply(c, 550, R550);
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
        reply(c, 550, R550);
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
        reply(c, 550, R550);
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
        reply(c, 550, R550);
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
        parent_exists(c, path) != 0) {
        c->have_rnfr = false;
        reply(c, 550, R550);
        return;
    }
    c->have_rnfr = false;
    if (s->fs->rename(s->fs->ctx, c->rnfr, path) != 0) {
        reply(c, 550, R550);
        return;
    }
    reply(c, 250, R250);
}

/* ---- CPFR / CPTO: server-side copy (draft-bharat-ftp-copy-command) ---- */

/* CPFR <path>: remember the source; reply 350 (or 550 if missing).
 * The actual copy runs on a worker via OPFTP_JOB_COPY when CPTO
 * arrives, so multi-GB game copies don't block the reactor. */
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
    if (s->fs->stat(s->fs->ctx, path, &st) != 0 ||
        (st.mode & S_IFMT) != S_IFREG) {
        reply(c, 550, R550);
        return;
    }
    snprintf(c->cpfr, sizeof(c->cpfr), "%s", path);
    c->have_cpfr = true;
    reply(c, 350, "File exists, ready for destination name.");
}

static void cmd_cpto(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    if (!c->have_cpfr) { reply(c, 503, R503); return; }
    if (c->job) { reply(c, 450, R450); return; }
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0 ||
        parent_exists(c, path) != 0) {
        c->have_cpfr = false;
        reply(c, 550, R550);
        return;
    }
    c->have_cpfr = false;

    struct opftp_transfer_job* j = calloc(1, sizeof(*j));
    if (!j) { reply(c, 451, R451); return; }
#ifdef OPFTP_PS3
    j->cancel_pipe[0] = j->cancel_pipe[1] = -1;
#else
    if (pipe(j->cancel_pipe) != 0) {
        free(j);
        reply(c, 451, R451);
        return;
    }
#endif
    j->client = c;
    opftp_client_retain(c);
    j->generation = c->generation;
    j->op = OPFTP_JOB_COPY;
    j->data_fd = -1;
    snprintf(j->path, sizeof(j->path), "%s", c->cpfr);
    snprintf(j->dst, sizeof(j->dst), "%s", path);
    atomic_init(&j->cancelled, false);

    if (opftp_job_dispatch(s, j) != 0) {
#ifndef OPFTP_PS3
        close(j->cancel_pipe[0]);
        close(j->cancel_pipe[1]);
#endif
        opftp_client_release(c);
        free(j);
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
    if (!param || strncasecmp(param, "CHMOD", 5) != 0) {
        reply(c, 502, R502);
        return;
    }
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
        reply(c, 550, R550);
        return;
    }
    reply(c, 200, R200);
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
        reply(c, 550, R550);
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
        reply(c, 550, R550);
        return;
    }
    time_t t = (time_t) st.mtime;
    struct tm tm;
    if (gmtime_r(&t, &tm) == NULL) {
        reply(c, 550, R550);
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
        reply(c, 550, R550);
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
    if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 ||
        listen(fd, 1) != 0) {
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
        int zero = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
#endif
        struct sockaddr_in6* a6 = (struct sockaddr_in6*) &addr;
        a6->sin6_family = AF_INET6;
        a6->sin6_addr = in6addr_any;
        a6->sin6_port = 0;
        alen = sizeof(*a6);
        if (bind(fd, (struct sockaddr*) &addr, alen) != 0 ||
            listen(fd, 1) != 0) {
            int e = errno;
            opftp_close_fd(fd);
            errno = e;
            reply(c, 425, R425);
            return;
        }
        socklen_t sl = alen;
        getsockname(fd, (struct sockaddr*) &addr, &sl);
        port = ntohs(((struct sockaddr_in6*) &addr)->sin6_port);
    } else {
        struct sockaddr_in* a4 = (struct sockaddr_in*) &addr;
        a4->sin_family = AF_INET;
        a4->sin_addr.s_addr = htonl(INADDR_ANY);
        a4->sin_port = 0;
        alen = sizeof(*a4);
        if (bind(fd, (struct sockaddr*) &addr, alen) != 0 ||
            listen(fd, 1) != 0) {
            int e = errno;
            opftp_close_fd(fd);
            errno = e;
            reply(c, 425, R425);
            return;
        }
        socklen_t sl = alen;
        getsockname(fd, (struct sockaddr*) &addr, &sl);
        port = ntohs(((struct sockaddr_in*) &addr)->sin_port);
    }

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
                           const char* path, bool nlst)
{
    struct opftp_server* s = server_of(c);

    if (c->job) {
        reply(c, 450, R450);
        return;
    }
    int fd = opftp_datachan_connect(c);
    if (fd < 0) {
        reply(c, 425, R425);
        return;
    }

    struct opftp_transfer_job* j = calloc(1, sizeof(*j));
    if (!j) {
        opftp_close_fd(fd);
        reply(c, 425, R425);
        return;
    }
#ifdef OPFTP_PS3
    /* ps3 has no pipe(): cancellation relies on the atomic flag and
     * the worker's poll timeout */
    j->cancel_pipe[0] = j->cancel_pipe[1] = -1;
#else
    if (pipe(j->cancel_pipe) != 0) {
        free(j);
        opftp_close_fd(fd);
        reply(c, 425, R425);
        return;
    }
#endif
    j->client = c;
    opftp_client_retain(c);
    j->generation = c->generation;
    j->op = op;
    j->nlst = nlst;
    j->rest = c->rest;
    j->need_tls = (c->tls_prot != 0);   /* PROT P: TLS data channel */
    snprintf(j->path, sizeof(j->path), "%s", path);
    j->data_fd = fd;
    atomic_init(&j->cancelled, false);

    if (opftp_job_dispatch(s, j) != 0) {
#ifndef OPFTP_PS3
        close(j->cancel_pipe[0]);
        close(j->cancel_pipe[1]);
#endif
        opftp_close_fd(fd);
        opftp_client_release(c);
        free(j);
        reply(c, 425, R425);
        return;
    }
    c->job = j;
    c->rest = 0;
    reply(c, 150, R150);
}

/* LIST / NLST: param may be a path or "-<flags>" style; empty -> cwd. */
static void cmd_list(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    const char* arg = param;
    if (arg && arg[0] == '-') {
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
        reply(c, 550, R550);
        return;
    }
    start_transfer(c, OPFTP_JOB_LIST, path, false);
}

static void cmd_nlst(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    struct opftp_server* s = server_of(c);
    const char* arg = param;
    if (arg && arg[0] == '-') {
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
        reply(c, 550, R550);
        return;
    }
    start_transfer(c, OPFTP_JOB_LIST, path, true);
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
        reply(c, 550, R550);
        return;
    }
    start_transfer(c, OPFTP_JOB_RETR, path, false);
}

static void cmd_stor(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    if (parent_exists(c, path) != 0) {
        reply(c, 550, R550);
        return;
    }
    start_transfer(c, OPFTP_JOB_STOR, path, false);
}

static void cmd_appe(struct opftp_client* c, const char* param, void* ctx)
{
    (void) ctx;
    char path[OPFTP_MAX_PATH];
    if (resolve_arg(c, param, path, sizeof(path), NULL) != 0) {
        reply(c, 501, R501);
        return;
    }
    if (parent_exists(c, path) != 0) {
        reply(c, 550, R550);
        return;
    }
    start_transfer(c, OPFTP_JOB_APPE, path, false);
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
    opftp_dispatch_register(s, "RETR", cmd_retr, NULL);
    opftp_dispatch_register(s, "STOR", cmd_stor, NULL);
    opftp_dispatch_register(s, "APPE", cmd_appe, NULL);
}
