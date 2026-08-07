#ifndef OPENPS3FTP_H
#define OPENPS3FTP_H

/*
 * OpenPS3FTP — modern FTP server library (rewrite).
 * Public API. Modern C (C11). See DESIGN.md for the full contract.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>   /* off_t, ssize_t */
#include <sys/socket.h>  /* struct sockaddr */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct opftp_server opftp_server_t;
typedef struct opftp_client opftp_client_t;

/* Backend-neutral open flags (backends map to native). */
enum {
    OPFTP_O_RDONLY = 1,
    OPFTP_O_WRONLY = 2,
    OPFTP_O_RDWR   = 3,
    OPFTP_O_CREAT  = 0x40,
    OPFTP_O_TRUNC  = 0x200,
    OPFTP_O_APPEND = 0x400,
};

typedef struct opftp_stat {
    uint16_t mode;        /* S_IFMT | rwxrwxrwx (full mode bits) */
    uint64_t size;
    int64_t  mtime;       /* unix seconds */
    uint32_t uid, gid;    /* 0 when backend has none */
} opftp_stat_t;

typedef struct opftp_dirent {
    char     name[256];   /* UTF-8 */
    uint16_t mode;
    uint64_t size;
    int64_t  mtime;
    uint32_t uid, gid;    /* LIST prints these; 0 -> "1 1" */
} opftp_dirent_t;

/*
 * Filesystem backend (replaces the old ftpio_*). May be called
 * concurrently by workers; backends must be internally thread-safe.
 * Backends are const singletons; the per-server root is applied by a
 * rooted wrapper. Errors: -1 + errno (POSIX values; ps3 backend maps
 * cell errors -> errno). read/write may return fewer bytes than
 * requested (partial I/O); 0 = EOF.
 */
typedef struct opftp_fs {
    void* ctx;
    int   (*open)(void* ctx, const char* path, int flags, uint16_t mode, int* fd);
    int   (*close)(void* ctx, int fd);
    ssize_t (*read)(void* ctx, int fd, void* buf, size_t n);
    ssize_t (*write)(void* ctx, int fd, const void* buf, size_t n);
    int64_t (*seek)(void* ctx, int fd, int64_t off, int whence); /* SEEK_SET/CUR/END; 64-bit (ps3 off_t is 32-bit) */
    int   (*fstat)(void* ctx, int fd, opftp_stat_t* st);
    int   (*stat)(void* ctx, const char* path, opftp_stat_t* st);  /* follows symlinks */
    int   (*opendir)(void* ctx, const char* path, void** dir);
    int   (*readdir)(void* ctx, void* dir, opftp_dirent_t* de);    /* 1=entry 0=eof <0=err; metadata REQUIRED */
    int   (*closedir)(void* ctx, void* dir);
    int   (*mkdir)(void* ctx, const char* path, uint16_t mode);
    int   (*rmdir)(void* ctx, const char* path);
    int   (*unlink)(void* ctx, const char* path);
    int   (*rename)(void* ctx, const char* oldp, const char* newp);
    int   (*chmod)(void* ctx, const char* path, uint16_t mode);
} opftp_fs_t;

/* Built-in backends (const singletons, no root state). */
extern const opftp_fs_t opftp_fs_posix;   /* host linux */
const opftp_fs_t* opftp_fs_ps3(void);     /* ps3 sysfs; ps3dk builds only */

/* Callbacks. */
typedef bool (*opftp_auth_fn)(void* ctx, const char* user, const char* pass);
typedef void (*opftp_hook_fn)(opftp_client_t*, void* ctx); /* connect/disconnect */
typedef void (*opftp_log_fn)(int level, const char* msg);

typedef struct opftp_callbacks {
    opftp_auth_fn auth;        void* auth_ctx;
    opftp_hook_fn connect;     void* connect_ctx;
    opftp_hook_fn disconnect;  void* disconnect_ctx;
    opftp_log_fn  log;         /* NULL = no logging */
    int log_level;             /* 0..3 */
    /* Called on the reactor thread after the default command table is
     * built, before accepting clients. Lets an application (e.g. the
     * legacy shim) install/override command handlers. NULL = unused. */
    void (*after_commands)(opftp_server_t* s, void* ctx);
    void* after_commands_ctx;
    /* Skip the built-in "220 ready" banner; the connect hook sends its
     * own welcome (legacy shim). */
    bool skip_banner;
} opftp_callbacks_t;

/* Server lifecycle. start and run_loop are mutually exclusive
 * (calling the other while one is active returns an error).
 * A stop requested from within the reactor thread only schedules
 * shutdown; it never waits on itself. */
opftp_server_t* opftp_server_create(const opftp_callbacks_t* cb);
void opftp_server_set_port(opftp_server_t*, uint16_t port);           /* default 2121; 0 = ephemeral */
void opftp_server_set_fs(opftp_server_t*, const opftp_fs_t* fs);      /* default posix/ps3 */
void opftp_server_set_root(opftp_server_t*, const char* root);        /* default "/" */
void opftp_server_set_workers(opftp_server_t*, int n);                /* default 2 */
void opftp_server_set_stop_timeout(opftp_server_t*, int seconds);     /* default 5 */
int  opftp_server_set_tls(opftp_server_t*, const char* cert_pem, const char* key_pem);
void opftp_server_set_require_tls(opftp_server_t*, bool);
void opftp_server_set_allow_foreign_port(opftp_server_t*, bool);      /* default false */
int  opftp_server_start(opftp_server_t*);    /* 0 ok; errno-style code */
int  opftp_server_stop(opftp_server_t*);     /* 0 when drained; -ETIMEDOUT if timeout */
void opftp_server_destroy(opftp_server_t*);  /* no-op if stop timed out; call stop again */
int  opftp_server_run_loop(opftp_server_t*); /* reactor on calling thread until stop */
uint16_t opftp_server_bound_port(opftp_server_t*); /* actual port (ephemeral support) */

/* Client context. Valid inside connect/disconnect hooks and while a
 * command handler runs (reactor thread). Pointers returned are valid
 * until the hook/handler returns. */
const struct sockaddr* opftp_client_peer(opftp_client_t*);  /* IPv4 or IPv6 */
const char* opftp_client_user(opftp_client_t*);
const char* opftp_client_cwd(opftp_client_t*);
void* opftp_client_userdata(opftp_client_t*);
void  opftp_client_userdata_set(opftp_client_t*, void*);
void  opftp_client_send(opftp_client_t*, const char* msg);         /* raw line, CRLF added */
void  opftp_client_send_reply(opftp_client_t*, int code, const char* msg);
void  opftp_client_disconnect(opftp_client_t*);

/* ---- server snapshot (status UIs / OSD) ---- */

#define OPFTP_SNAPSHOT_MAX_CLIENTS 16
#define OPFTP_SNAPSHOT_PATH 1024

/* One connected client with its active transfer, if any. */
typedef struct opftp_snapshot_client {
    char peer[64];            /* "ip" or "ip:port" string */
    char user[64];
    char cwd[OPFTP_SNAPSHOT_PATH];
    bool logged_in;
    bool xfer_active;         /* an opftp transfer/copy job is running */
    char xfer_op[8];          /* "LIST" "RETR" "STOR" "APPE" "COPY" */
    char xfer_path[OPFTP_SNAPSHOT_PATH];
    uint64_t xfer_bytes;      /* progress so far */
    uint64_t xfer_total;      /* expected total; 0 if unknown */
} opftp_snapshot_client_t;

/* Thread-safe server state snapshot. Callable from any thread (the
 * reactor keeps the cache fresh while running). */
typedef struct opftp_snapshot {
    bool started;             /* reactor running */
    uint16_t port;            /* bound port */
    char root[OPFTP_SNAPSHOT_PATH];
    int workers;
    int num_clients;
    opftp_snapshot_client_t clients[OPFTP_SNAPSHOT_MAX_CLIENTS];
} opftp_snapshot_t;

/* Copy the current snapshot into out. Returns 0 (always fills out;
 * fields are zero/empty before start and after stop). */
int opftp_server_snapshot(opftp_server_t*, opftp_snapshot_t* out);

#ifdef __cplusplus
}
#endif

#endif /* OPENPS3FTP_H */
