#ifndef OPFTP_INTERNAL_H
#define OPFTP_INTERNAL_H

/*
 * OpenPS3FTP — internal shared declarations.
 * This header is the contract between the foundation modules and the
 * reactor/transfer modules. See DESIGN.md for the ownership protocol.
 */

#include "openps3ftp/openps3ftp.h"
#include <stdatomic.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef OPFTP_PS3
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/poll.h>
#include <sys/sys_time.h>
/* this toolchain's libnet exports closesocket(); sys/socket.h declares
 * socketclose() which is only in the stub lib. Declare it here. */
int closesocket(int s);
#else
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#define OPFTP_MAX_PATH 1024
#define OPFTP_MAX_CMD  32          /* command name length incl. NUL */
#define OPFTP_RBUF     4096        /* control read buffer */

/* PS3 abort latency ceiling. The host polls {data fd, cancel pipe} and
 * aborts the instant the reactor writes the pipe; the ps3dk net layer
 * has no pipe()/socketpair() and its poll() only takes sockets, so the
 * worker there re-checks the atomic cancel flag on this cadence. */
#define OPFTP_PS3_CANCEL_MS 200

/* Reply texts, kept verbatim from the original const.h — clients and
 * tests depend on the exact wording (DESIGN.md, "Protocol support").
 * Shared so commands.c and datachan.c cannot drift apart. */
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

/* ---- ps3 socket portability ---- */

#ifdef OPFTP_PS3
/* ps3 has no sockaddr_storage; sockaddr_in6 (28B) is the largest */
typedef union {
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
    uint8_t raw[32];
} opftp_sockaddr_storage;
#else
typedef struct sockaddr_storage opftp_sockaddr_storage;
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Family of an opftp_sockaddr_storage. Works on both platforms:
 * host sockaddr_storage has ss_family first; ps3 sockaddr_in/in6 both
 * start with {sa_len, sa_family}. */
static inline sa_family_t opftp_sockaddr_family(const opftp_sockaddr_storage* s)
{
    return ((const struct sockaddr*) s)->sa_family;
}

#ifdef OPFTP_PS3
static const struct in6_addr opftp_in6addr_any = {{0}};
#define in6addr_any opftp_in6addr_any
#endif

#ifdef OPFTP_PS3
static inline bool opftp_is_v4mapped(const struct in6_addr* a)
{
    return a->s6_addr[0] == 0 && a->s6_addr[1] == 0 &&
           a->s6_addr[2] == 0 && a->s6_addr[3] == 0 &&
           a->s6_addr[4] == 0 && a->s6_addr[5] == 0 &&
           a->s6_addr[6] == 0 && a->s6_addr[7] == 0 &&
           a->s6_addr[8] == 0 && a->s6_addr[9] == 0 &&
           a->s6_addr[10] == 0xff && a->s6_addr[11] == 0xff;
}
#else
static inline bool opftp_is_v4mapped(const struct in6_addr* a)
{
    return IN6_IS_ADDR_V4MAPPED(a) != 0;
}
#endif

/* Monotonic-ish milliseconds: CLOCK_MONOTONIC on host, the lv2
 * microsecond counter on ps3 (no clock_gettime there). */
static inline int64_t opftp_now_ms(void)
{
#ifdef OPFTP_PS3
    return (int64_t) (sys_time_get_system_time() / 1000);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* Close a socket fd. ps3 sockets must go through closesocket(); its
 * fds carry SOCKET_FD_MASK (file fds from sysFs don't). */
static inline int opftp_close_fd(int fd)
{
#ifdef OPFTP_PS3
    if (fd & SOCKET_FD_MASK)
        return closesocket(fd);
    return close(fd);
#else
    return close(fd);
#endif
}

/* Non-blocking mode: fcntl on host, SO_NBIO setsockopt on ps3. */
static inline int opftp_set_nonblock(int fd)
{
#ifdef OPFTP_PS3
    int one = 1;
    return setsockopt(fd, SOL_SOCKET, SO_NBIO, &one, sizeof(one));
#else
    int fl = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* ---- threading portability (host: pthread) ---- */

/* Opaque handles, mirroring the legacy sys.thread API shape so the
 * compat shim can map onto them directly. PS3 backend (P4) reuses the
 * same interface. Thread func: void (*)(void*). */
void* opftp_mutex_create(void);
int   opftp_mutex_lock(void* m);
int   opftp_mutex_unlock(void* m);
int   opftp_mutex_destroy(void* m);

void* opftp_cond_create(void* m);  /* m = mutex it will be waited on (ps3 binds at create) */
int   opftp_cond_wait(void* c, void* m);       /* unlocks m while waiting */
int   opftp_cond_signal(void* c);
int   opftp_cond_broadcast(void* c);
int   opftp_cond_destroy(void* c);

void* opftp_thread_create(void (*fn)(void*), void* arg); /* joinable */
void* opftp_thread_join(void* t);                        /* returns fn's void* retval */
void  opftp_thread_destroy(void* t);

/* Calling thread's identity. Used to tell "am I the reactor?" apart
 * from "am I a worker?" at the control-socket write boundary. */
typedef struct {
#ifdef OPFTP_PS3
    uint64_t id;
#else
    pthread_t id;
#endif
} opftp_tid_t;

void opftp_thread_self(opftp_tid_t* out);
bool opftp_tid_eq(const opftp_tid_t* a, const opftp_tid_t* b);

/* ---- pollset (reactor multiplexer) ---- */

typedef struct opftp_pollset opftp_pollset_t;

opftp_pollset_t* opftp_pollset_create(void);
void opftp_pollset_destroy(opftp_pollset_t*);

/* Register/unregister a socket fd. Returns a handle >= 0, or -1.
 * user is returned with events (used to map fd -> client/job). */
int  opftp_pollset_add(opftp_pollset_t*, int fd, short events, void* user);
void opftp_pollset_mod(opftp_pollset_t*, int handle, short events);
void opftp_pollset_remove(opftp_pollset_t*, int handle);

/* Wait up to timeout_ms. Returns event count (0 on timeout, -1 on
 * error). The self-pipe is consumed internally; a pollset_wake() caller
 * does not need to read it. After wait, call opftp_pollset_collect once
 * to copy the ready (user, revents) pairs into caller arrays (bounded
 * by max; returns the count). Iterate the copy, not the live pollset —
 * handlers may disconnect clients mid-iteration. */
int  opftp_pollset_wait(opftp_pollset_t*, int timeout_ms);
void opftp_pollset_wake(opftp_pollset_t*);
int  opftp_pollset_collect(opftp_pollset_t*, void** users, short* events, int max);

/* ---- path resolution (core, not fs) ---- */

/*
 * Resolve `arg` against `cwd` into canonical absolute `out`.
 * Rules: absolute args used as-is; "." skipped; ".." pops and clamps
 * at root; "//" collapsed; no realpath()/symlink handling here.
 * Returns 0 on success; -1 with errno=ERANGE if out is too small.
 * *trailing_slash (optional) set when the arg ended with '/'.
 */
int opftp_path_resolve(const char* root, const char* cwd, const char* arg,
                       char* out, size_t outsz, bool* trailing_slash);

/* Write the parent directory of `path` into `out` ("" for "/" itself).
 * `path` must be canonical (absolute). Returns 0 or -1/ERANGE. */
int opftp_path_parent(const char* path, char* out, size_t outsz);

/* ---- listing / utf8 ---- */

/* Format mode bits into "drwxrwxrwx" (11 chars + NUL). */
void opftp_mode_str(uint16_t mode, char out[11]);
/* Format one LIST line ("-rwxr-xr-x 1 uid gid size Mon DD HH:MM name").
 * uid/gid printed as "1 1" when both are 0. Returns chars written. */
int opftp_listing_format(char* out, size_t n, const opftp_dirent_t* de,
                         const char* display_name);
/* Format one RFC 3659 MLSD line ("type=file;size=..;modify=..;perm=..; name").
 * Returns chars written. */
int opftp_listing_format_mlsd(char* out, size_t n, const opftp_dirent_t* de);
/* RFC 2640 minimal: valid UTF-8, no embedded NUL. Byte-preserving. */
bool opftp_utf8_valid(const char* s);

/* ---- fs: rooted wrapper ---- */

/*
 * Wrap `base` with `root` (must be canonical, e.g. "/" or "/srv/ftp").
 * The wrapper verifies every path stays under root (textual prefix
 * check on canonical paths) and forwards to base unchanged otherwise.
 * When base == &opftp_fs_posix, existing targets are additionally
 * realpath-checked against root (dev-only symlink containment;
 * documented TOCTOU window). The returned wrapper is owned by the
 * caller (opftp_fs_rooted_free).
 */
const opftp_fs_t* opftp_fs_rooted(const opftp_fs_t* base, const char* root);
void opftp_fs_rooted_free(const opftp_fs_t* wrapper);

/* In-memory backend for unit tests: creates a fresh context (caller
 * destroys with opftp_fs_mem_destroy). Declared here for tests. */
const opftp_fs_t* opftp_fs_mem_create(void);
void opftp_fs_mem_destroy(const opftp_fs_t* fs);

/* ---- transfer jobs (worker pool) ---- */

/* Per-client generation: bumped by the reactor on disconnect; jobs are
 * stamped at dispatch and completions with a stale generation are
 * dropped. */
#define OPFTP_MAX_NAME 256

struct opftp_client {
    /* Atomic: a queued off-reactor reply retains the client from a
     * worker thread. The client is still only ever *freed* by the
     * reactor (it drains the queue and drops the last ref there). */
    atomic_int refs;
    uint64_t generation;
    int fd;                      /* control socket (reactor-owned) */
    struct opftp_server* server;

    opftp_sockaddr_storage peer;
    uint16_t peerlen;

    char rbuf[OPFTP_RBUF];
    unsigned rlen, rpos;         /* unconsumed bytes in rbuf */

    bool logged_in;
    bool disconnect_requested;
    char user[OPFTP_MAX_NAME];
    char cwd[OPFTP_MAX_PATH];
    void* userdata;

    struct opftp_transfer_job* job;   /* non-NULL while a transfer is active */
    bool abor_pending;                /* ABOR received; reply deferred */

    uint64_t rest;                    /* REST offset */
    char rnfr[OPFTP_MAX_PATH];        /* RNFR source */
    bool have_rnfr;
    char cpfr[OPFTP_MAX_PATH];        /* CPFR source (server-side copy) */
    bool have_cpfr;

    /* TLS (FTPS) */
    struct opftp_tls_session* tls;    /* control-channel TLS, or NULL */
    bool tls_handshaking;             /* control handshake in progress */
    int tls_prot;                     /* 1 = PROT P (data TLS) */

    /* data-channel setup (reactor-owned) */
    int pasv_fd;                 /* listening PASV socket, or -1 */
    opftp_sockaddr_storage data_peer;  /* PORT target or PASV peer */
    uint16_t data_peerlen;
    bool have_data_peer;

    struct opftp_client* next;   /* reactor client list */
    int poll_handle;             /* pollset handle, -1 when removed */
};

enum opftp_job_op {
    OPFTP_JOB_LIST,   /* LIST/NLST: write listing to data socket */
    OPFTP_JOB_STOR,   /* read data socket -> fs */
    OPFTP_JOB_APPE,   /* read data socket -> fs (append) */
    OPFTP_JOB_RETR,   /* fs -> data socket */
    OPFTP_JOB_COPY,   /* CPFR/CPTO: fs -> fs (server-side copy) */
};

/* One in-flight transfer. Worker-owned after dispatch; the reactor
 * touches only: cancelled (atomic), cancel_pipe write end, and the
 * completion fields on receive. */
struct opftp_transfer_job {
    struct opftp_client* client;   /* holds a ref */
    uint64_t generation;           /* client->generation at dispatch */
    enum opftp_job_op op;
    char path[OPFTP_MAX_PATH];
    char dst[OPFTP_MAX_PATH];        /* OPFTP_JOB_COPY destination */
    uint64_t rest;                 /* REST offset (STREAM mode) */
    bool nlst;                     /* NLST (names only) vs LIST */
    bool mlsd;                     /* MLSD (RFC 3659 machine listing) */

    atomic_bool cancelled;
    int cancel_pipe[2];            /* [0] worker polls; [1] reactor writes */
    int data_fd;                   /* worker-owned after dispatch */
    bool need_tls;                 /* PROT P: wrap data fd in TLS */
    struct opftp_tls_session* tls; /* worker-owned data TLS session */

    /* data-connection setup (copied from the client at dispatch; the
     * worker establishes the connection so a slow/broken client can
     * never stall the reactor). */
    int pasv_fd;                       /* PASV listener, or -1 */
    opftp_sockaddr_storage data_peer;  /* PORT target */
    uint16_t data_peerlen;
    bool have_data_peer;
    opftp_sockaddr_storage ctl_peer;   /* control peer (bounce check) */
    uint16_t ctl_peerlen;
    bool conn_ok;                      /* worker: connection established */

    /* live progress (worker writes, reactor/OSD reads) */
    _Atomic uint64_t bytes;
    uint64_t total;                /* expected total; 0 = unknown */

    /* completion posted by worker (reactor clears/consumes): */
    int result;                    /* 0 ok, -ECANCELED aborted, -errno other */
    uint64_t final_bytes;
    bool posted;
    struct opftp_transfer_job* compl_next;
};

/* One control-channel line produced off the reactor thread, waiting to
 * be written by the reactor. Holds a client ref; `generation` drops it
 * if the client went away in the meantime. */
struct opftp_pending_reply {
    struct opftp_client* c;
    uint64_t generation;
    struct opftp_pending_reply* next;
    char text[];
};

/* ---- server ---- */

struct opftp_server {
    opftp_callbacks_t cb;
    uint16_t port;
    const opftp_fs_t* fs_base;     /* configured backend */
    const opftp_fs_t* fs;          /* rooted wrapper in use */
    char root[OPFTP_MAX_PATH];
    int workers_req;               /* configured worker count (default 2) */
    int stop_timeout_ms;

    /* tls config (P3); cert/key owned when set */
    bool require_tls;
    char* cert_pem; char* key_pem;
    bool allow_foreign_port;
    bool allow_stop;               /* register the STOP command (opt-in) */
    struct opftp_tls_ctx* tls;     /* parsed server TLS context, or NULL */

    /* reactor state */
    int listen_fd;
    int poll_handle_listener;
    atomic_bool started;             /* 1 once start/run_loop entered */
    atomic_bool stopping;            /* stop requested */
    atomic_bool reactor_running;     /* true after start() spawns thread */
    atomic_bool reactor_done;        /* set by reactor thread on exit */
    void* reactor;                   /* opftp thread handle (start mode) */
    struct opftp_pollset* pollset;   /* reactor-local; NULL when idle */
    struct opftp_client* clients;    /* reactor-owned list */
    void* compl_mutex;               /* completion + reply queue lock */
    struct opftp_transfer_job* completions;  /* worker->reactor stack */
    /* worker->reactor control replies, FIFO (reply order is protocol) */
    struct opftp_pending_reply* replies_head;
    struct opftp_pending_reply* replies_tail;
    /* reactor thread identity, published once the loop is running */
    opftp_tid_t reactor_tid;
    atomic_bool reactor_tid_set;

    /* lifecycle sync: stop() from another thread waits for readiness
     * before touching mutexes/pollset (publication barrier) */
    void* life_mutex;
    void* life_cond;
    bool ready;                      /* init finished, reactor about to run */
    bool starting;                   /* start/run_loop in progress */

    /* dispatch table */
    struct opftp_cmd_entry* dispatch_entries;
    unsigned dispatch_cap;

    /* worker pool */
    int num_workers;
    void** workers;              /* opftp thread handles */
    struct opftp_transfer_job** queue;
    unsigned queue_head, queue_tail, queue_count;
    unsigned queue_cap;
    void* queue_mutex;
    void* queue_cond;            /* signaled on enqueue */
    bool pool_running;
    int workers_alive;

    /* status snapshot (reactor refreshes; readers take snap_mutex) */
    void* snap_mutex;
    struct opftp_snapshot snap_cache;
};

/* ---- reactor module (implemented by the reactor lane) ---- */

int opftp_reactor_init(struct opftp_server* s);
void opftp_reactor_shutdown(struct opftp_server* s);
void opftp_reactor_loop(struct opftp_server* s);

/* dispatch a job; returns 0 or -errno (EBUSY = one transfer per client) */
int opftp_job_dispatch(struct opftp_server* s, struct opftp_transfer_job* j);

/* worker entry point (datachan lane) */
void opftp_transfer_worker(void* arg);

/* internal logging */
void opftp_log(struct opftp_server* s, int level, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* ---- client (client.c) ---- */

struct opftp_client* opftp_client_new(struct opftp_server* s, int fd,
                                      const struct sockaddr* peer, socklen_t peerlen);
void opftp_client_retain(struct opftp_client* c);
void opftp_client_release(struct opftp_client* c);  /* frees at refs==0 */

/* reactor-side control I/O. Returns bytes read (0 = EOF, -1 = error). */
ssize_t opftp_client_read(struct opftp_client* c);
/* Try to extract one CRLF/LF-terminated line from the buffer.
 * Returns 0 if no complete line, 1 on line (into out, NUL-terminated). */
bool opftp_client_getline(struct opftp_client* c, char* out, size_t outsz);
/* Write a control line. On the reactor thread this sends directly; from
 * any other thread it queues the line for the reactor (DESIGN.md: only
 * the reactor writes to control sockets). */
int opftp_client_send_raw(struct opftp_client* c, const char* s);
/* Drain queued off-reactor replies (reactor thread). */
void opftp_client_drain_replies(struct opftp_server* s);
int opftp_client_start_tls(struct opftp_client* c);   /* AUTH TLS: create session, begin handshake */

/* ---- dispatch (dispatch.c) ---- */

typedef void (*opftp_cmd_fn)(struct opftp_client*, const char* param, void* ctx);
struct opftp_cmd_entry {
    char name[8];
    opftp_cmd_fn fn;
    void* ctx;
    bool used;
};
void opftp_dispatch_init(struct opftp_server* s);
void opftp_dispatch_free(struct opftp_server* s);
int opftp_dispatch_register(struct opftp_server* s, const char* name, opftp_cmd_fn fn, void* ctx);
int opftp_dispatch_call(struct opftp_server* s, struct opftp_client* c,
                        const char* name, const char* param);  /* 0 = handled, 1 = unknown */
void opftp_commands_init(struct opftp_server* s);

/* ---- datachan (datachan.c / transfer.c) ---- */

int opftp_datachan_init(struct opftp_server* s);   /* spawn workers */
void opftp_datachan_shutdown(struct opftp_server* s); /* drain + join */
/* Establish a data connection for a transfer (PASV accept or PORT
 * connect, with peer-matching). Returns fd or -1 (errno set). */
int opftp_datachan_connect_job(struct opftp_server* s, struct opftp_transfer_job* j);
int opftp_datachan_precheck(struct opftp_client* c);
/* reply + bookkeeping after a completion (reactor side) */
void opftp_datachan_complete(struct opftp_server* s, struct opftp_transfer_job* j);

/* transfer.c: run one job on the worker thread; never returns until
 * the job is finished. Posts completion to the reactor queue. */
void opftp_transfer_run(struct opftp_server* s, struct opftp_transfer_job* j);

#endif /* OPFTP_INTERNAL_H */
