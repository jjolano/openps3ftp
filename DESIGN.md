# OpenPS3FTP Rewrite — Design

Full rewrite of the FTP server/client library. Modern C (C11/C17 style), free-form
API, same features plus explicit FTPS (AUTH TLS), IPv6 (EPSV/EPRT), UTF-8 (RFC 2640).
Old public API is preserved via a compatibility shim so multiMAN/webMAN/IRISMAN
can still link the new library.

Revision history: v3 — closes round-2 oracle blockers (completion ownership,
start/run_loop exclusivity, rooted-fs plumbing, P1 test scope, queue reply codes).

## Non-goals

- Cell SDK (official Sony SDK) build — ps3dk/PSL1GHT and host Linux only.
- PRX/VSH builds — app (EBOOT) and library only.
- Backwards source compatibility inside the new core — the shim owns that.
- Unicode normalization (NFC/NFD) — byte-preserving UTF-8 in v1; validation only.
- Symlinks — PS3 sysfs has no symlink syscall; host backend handles containment.

## Architecture

```
┌────────────────────────────────────────────────────────┐
│ app (EBOOT main, or any consumer)                      │
│   uses new API directly  OR  legacy headers → shim     │
└──────────────┬──────────────────────────┬──────────────┘
               │ opftp_* (new API)        │ legacy API
┌──────────────▼─────────────┐   ┌────────▼─────────────────┐
│ src/  core library         │   │ legacy/ compat shim      │
│  reactor (poll, control)   │   │  old headers, facades,   │
│  workers (data transfers)  │   │  ftpio_* impl, adapter   │
│  protocol, dispatch        │   │  callbacks → opftp_*     │
│  fs backend interface      │   └────────▲─────────────────┘
└──────────────┬─────────────┘            │
               │ fs vtable                │ same vtable
   ┌───────────▼───────────┐   ┌──────────┴─────────┐
   │ backends: linux, ps3  │   │ (shim ships its    │
   │ (sysfs), mem (tests)  │   │  own linux/ps3)    │
   └───────────────────────┘   └────────────────────┘
```

All network and protocol logic is platform-independent; only the fs backend and
the thread/mutex primitives are platform-specific. No global state: one
`opftp_server_t` owns everything; multiple servers can coexist.

## Concurrency model (ownership protocol)

Two roles, one each side of a transfer:

**Reactor thread** (owns): the listener, all control sockets, control TLS state
machines, command parsing, ALL replies on control channels, the client registry,
and transfer-job bookkeeping. There is no separate accept thread — the reactor
accepts.

**Worker threads** (pool, default 2, configurable): own transfer jobs only — the
data socket, data TLS session, transfer buffers, and the fs backend calls during
the transfer.

Rules:
- Only the reactor writes to control sockets. Workers never send replies; they
  post completion (outcome + optional error) to the reactor, which sends the
  final reply. **Enforced, not merely intended:** `opftp_client_send_raw`
  compares the calling thread against the reactor's identity
  (`opftp_thread_self`) and, off the reactor, queues the line on the server's
  pending-reply FIFO (guarded by the completion mutex) and wakes the pollset;
  the reactor drains it each iteration. Queued lines carry a client ref and the
  client generation, so a client that disconnected first gets no reply. This is
  what keeps the legacy data-callback executor — which runs old handlers that
  call `client_send_code` from a pool worker — from racing the reactor on one
  fd and one mbedtls session. This makes stale-226 suppression unambiguous: when ABOR cancels a
  transfer, the reactor sends `426 Connection closed; transfer aborted` for the
  transfer and `226` for the ABOR command itself, in order.
- **Completion ownership:** a job completion carries (client ref, generation).
  Workers detach from client state the moment they post completion — after
  posting, they touch nothing but their own job resources. The reactor holds
  the client ref until it has cleared the client's active-transfer state, then
  releases it. A per-client generation counter stamps jobs at dispatch; a
  completion whose generation no longer matches the client's current state is
  dropped (covers disconnect/reuse races). Only the reactor frees/rebinds
  client-visible state; **the reactor is the single closer of job fds** — the
  worker stops using the data socket and cancel pipe, then the reactor closes
  them in `opftp_datachan_complete`. Closing only on the reactor thread
  serializes fd frees with accept/socket allocation, eliminating fd-reuse
  races (the design originally had the worker close its own data fd; the
  sanitizers showed that frees must be single-threaded to be safe).
- **Cancellation (atomic + fd-safe):** `job->cancelled` is a C11 atomic flag;
  the reactor sets it and writes the job's dedicated cancel pipe. The worker
  polls {data fd, cancel pipe}; when the cancel pipe fires it aborts. The
  reactor NEVER calls shutdown/close on a data fd. Worker aborts only after
  observing cancellation or an I/O error.
  **PS3 has no cancel fd.** ps3dk's net layer provides neither `pipe()` nor
  `socketpair()`, and its `poll()` only accepts sockets — so there is nothing
  to wake the worker with. There, cancellation is the atomic flag alone,
  re-checked whenever the worker's poll times out; that timeout is capped at
  `OPFTP_PS3_CANCEL_MS` (200ms), which is the PS3 abort-latency ceiling. Host
  builds are immediate.
- **Data-channel connect runs on the worker,** not the reactor
  (`opftp_datachan_connect_job`): the PASV accept and the PORT connect both
  wait on a remote peer, so doing them on the reactor let one slow or absent
  client stall every other connection. The reactor does only the non-blocking
  part — the PORT/EPRT bounce check, a pure address compare — so a rejected
  target still replies 425 before any 150. PASV listener ownership moves from
  the client to the job at dispatch.
- One active transfer per client. A second data-channel request while one is
  in flight → `450 Another data transfer in progress`.
- Bounded job queue (default 2×workers). Saturation → `425 Cannot open data
  connection` (client stays connected). `421` is reserved for actual control-
  service shutdown.
- TLS handshakes are non-blocking everywhere:
  - control: reactor advances handshake on POLLIN/POLLOUT; 10s deadline → 421 close.
  - data: worker advances on poll + `MBEDTLS_ERR_SSL_WANT_READ/WANT_WRITE` retry;
    10s deadline → abort transfer.
- ABOR: `ABOR` command or OOB data (`POLLPRI` on control socket — ftplib sends
  OOB) → reactor sets cancellation + cancel pipe as above; worker aborts, posts
  completion. Reactor replies 426 + 226 in order.
- Client lifetime: refcount. Worker holds a ref during a job. The reactor frees
  the client when refs drop to 0. Disconnect mid-transfer: reactor removes the
  client from the registry and closes the control socket; the worker completes
  with an error, posts completion (reactor drops the stale generation), and its
  ref is released. Workers never touch a freed client.
- **Lifecycle modes:** `opftp_server_start` (background reactor thread) and
  `opftp_server_run_loop` (reactor on the calling thread, blocks) are mutually
  exclusive — calling the other while one is active returns an error.
  `opftp_server_stop` stops accepting, cancels jobs, and drains workers,
  returning once drained or the stop timeout (default 5s, configurable)
  expires. `opftp_server_destroy` waits for workers; if a worker exceeds the
  drain grace it returns an error and the server object remains alive (no
  leak) — the caller retries after the transfer completes. Destruction is
  never forced while workers are live.
- **Stop/startup synchronization:** `stop()` from another thread waits on a
  lifecycle condvar until `start`/`run_loop` has finished initialization
  (`ready`), so it never touches mutexes or the pollset mid-creation. The
  `stopping`/`started`/`reactor_running`/`reactor_done` flags are C11 atomics;
  the `s->pollset` pointer is published/cleared under the completion mutex so
  worker wakes and shutdown cannot race pollset creation/destruction (fd-reuse
  safety across restarts).
- **Pollset:** stable-slot implementation — removed entries are marked free and
  their slots reused; entries are never moved, so handles stay valid for the
  lifetime of their fd (swap-last removal invalidated handles and caused a
  use-after-free on disconnect).

## New public API (include/openps3ftp/openps3ftp.h)

```c
typedef struct opftp_server opftp_server_t;
typedef struct opftp_client opftp_client_t;

/* backend-neutral open flags (backends map to native) */
enum {
    OPFTP_O_RDONLY = 1, OPFTP_O_WRONLY = 2, OPFTP_O_RDWR = 3,
    OPFTP_O_CREAT = 0x40, OPFTP_O_TRUNC = 0x200, OPFTP_O_APPEND = 0x400,
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
    uint32_t uid, gid;    /* LIST prints these; 0 → "1 1" */
} opftp_dirent_t;

/* filesystem backend (replaces ftpio_*). May be called concurrently by
 * workers; backends must be internally thread-safe. Backends are const
 * singletons; the per-server root is applied by a rooted wrapper (below). */
typedef struct opftp_fs {
    void* ctx;
    int   (*open)(void* ctx, const char* path, int flags, uint16_t mode, int* fd);
    /* mode is the creation mode, honored when OPFTP_O_CREAT is set */
    int   (*close)(void* ctx, int fd);
    ssize_t (*read)(void* ctx, int fd, void* buf, size_t n);             /* partial ok; 0=eof */
    ssize_t (*write)(void* ctx, int fd, const void* buf, size_t n);      /* partial ok */
    int64_t (*seek)(void* ctx, int fd, int64_t off, int whence);         /* SEEK_SET/CUR/END; 64-bit (ps3 off_t is 32-bit) */
    int   (*fstat)(void* ctx, int fd, opftp_stat_t* st);
    int   (*stat)(void* ctx, const char* path, opftp_stat_t* st);        /* follows symlinks */
    int   (*opendir)(void* ctx, const char* path, void** dir);
    int   (*readdir)(void* ctx, void* dir, opftp_dirent_t* de);          /* 1=entry 0=eof <0=err; metadata REQUIRED; never yields "." or ".." */
    int   (*closedir)(void* ctx, void* dir);
    int   (*mkdir)(void* ctx, const char* path, uint16_t mode);
    int   (*rmdir)(void* ctx, const char* path);
    int   (*unlink)(void* ctx, const char* path);
    int   (*rename)(void* ctx, const char* oldp, const char* newp);
    int   (*chmod)(void* ctx, const char* path, uint16_t mode);
    int   (*utimes)(void* ctx, const char* path, int64_t mtime);         /* set mtime (unix seconds) */
} opftp_fs_t;

/* built-in backends (const singletons, no root state) */
extern const opftp_fs_t opftp_fs_posix;   /* host linux; symlink containment via realpath */
const opftp_fs_t* opftp_fs_ps3(void);     /* ps3 sysfs; compiled only on ps3 */

/* Per-server rooted wrapper: the server wraps the configured backend with the
 * configured root ("/" default). The wrapper receives canonical absolute paths
 * from the core, verifies they stay under root, maps them, and forwards to the
 * base backend. Backends never see root state directly. */

/* callbacks */
typedef bool (*opftp_auth_fn)(void* ctx, const char* user, const char* pass);
typedef void (*opftp_hook_fn)(opftp_client_t*, void* ctx); /* connect/disconnect */
typedef void (*opftp_log_fn)(int level, const char* msg);

typedef struct opftp_callbacks {
    opftp_auth_fn auth;       void* auth_ctx;
    opftp_hook_fn connect;    void* connect_ctx;
    opftp_hook_fn disconnect; void* disconnect_ctx;
    opftp_log_fn  log;        /* NULL = no logging, and no stderr fallback */
    int log_level;            /* 0..3; gates the callback */
    /* Called on the reactor thread after the default command table is built,
     * before accepting clients: lets an application (the legacy shim) install
     * or override handlers. NULL = unused. */
    void (*after_commands)(opftp_server_t* s, void* ctx);
    void* after_commands_ctx;
    bool skip_banner;         /* connect hook sends its own welcome */
} opftp_callbacks_t;

/* server lifecycle */
opftp_server_t* opftp_server_create(const opftp_callbacks_t* cb);
void opftp_server_set_port(opftp_server_t*, uint16_t port);            /* default 2121 */
void opftp_server_set_fs(opftp_server_t*, const opftp_fs_t* fs);       /* default posix/ps3 */
void opftp_server_set_root(opftp_server_t*, const char* root);         /* default "/" */
void opftp_server_set_workers(opftp_server_t*, int n);                 /* default 2 */
void opftp_server_set_stop_timeout(opftp_server_t*, int seconds);      /* default 5 */
int  opftp_server_set_tls(opftp_server_t*, const char* cert_pem, const char* key_pem);
void opftp_server_set_require_tls(opftp_server_t*, bool);              /* default false */
void opftp_server_set_allow_foreign_port(opftp_server_t*, bool);       /* default false */
void opftp_server_set_allow_stop(opftp_server_t*, bool);               /* register STOP; default false */
int  opftp_server_start(opftp_server_t*);    /* 0 ok; errno-style code; exclusive with run_loop */
int  opftp_server_stop(opftp_server_t*);     /* stop accepting, cancel jobs, drain; 0 when drained, -ETIMEDOUT if timeout */
void opftp_server_destroy(opftp_server_t*);  /* waits for workers; no-op if stop timed out (object stays alive; call stop again) */
int  opftp_server_run_loop(opftp_server_t*); /* reactor on calling thread until stop; exclusive with start */
uint16_t opftp_server_bound_port(opftp_server_t*);  /* actual port (ephemeral: set_port(0)) */

/* Thread-safe status snapshot for a UI (the PS3 OSD polls this): connected
 * clients, their cwd/user, and live per-transfer byte progress. Callable from
 * any thread; the reactor refreshes the cache while running, and clears
 * `started` when the loop exits (so a self-shutdown via STOP is observable). */
int opftp_server_snapshot(opftp_server_t*, opftp_snapshot_t* out);

/* A STOP request issued from within the reactor thread (e.g. the STOP command
 * hook) only SCHEDULES shutdown — the reactor applies it on the next loop
 * iteration after the current command completes. It never waits on itself. */

/* client context (valid inside connect/disconnect hooks and command context) */
const struct sockaddr* opftp_client_peer(opftp_client_t*);   /* IPv4 or IPv6 */
const char* opftp_client_user(opftp_client_t*);
const char* opftp_client_cwd(opftp_client_t*);
void* opftp_client_userdata(opftp_client_t*);
void  opftp_client_userdata_set(opftp_client_t*, void*);
void  opftp_client_send(opftp_client_t*, const char* msg);    /* raw line, \r\n added */
void  opftp_client_send_reply(opftp_client_t*, int code, const char* msg);
void  opftp_client_disconnect(opftp_client_t*);
```

Design rules:
- `opftp_` prefix everywhere; no platform-type macros (old `ftpstat`/`ftpdirent`
  macros are gone — the core defines its own structs).
- Backend vtable takes `ctx` — no global singletons.
- Errors: `-1` + `errno` (POSIX values; the ps3 backend maps cell errors → errno).
- `ssize_t`/`bool` from `<sys/types.h>`/`<stdbool.h>`; file offsets are
  explicit `int64_t` in the vtable, because ps3dk's `off_t` is 32-bit.
- All strings in/out of the core are UTF-8 (RFC 2640), byte-preserving.

## Path resolution (core, not fs)

- The core maintains a per-client canonical absolute path. Every path argument
  is resolved by the core: absolute → clean; relative → resolve against cwd;
  components `.` skipped, `..` pops (clamps at root), `//` collapsed; trailing
  slash flag preserved for MKD/RMD semantics. No `realpath()` in the core.
- Root: `opftp_server_set_root`, default `/`. `..` above root clamps to root.
- **Parent validation:** for STOR, MKD, RNTO and other target-creating commands,
  after resolution the core stats the resolved parent directory and returns
  `550` when it does not exist — nonexistent intermediate components never
  reach the backend.
- Backends receive only canonical absolute paths and enforce containment
  themselves (defense in depth). Host backend checks `realpath` containment
  (dev-only; PS3 has no symlinks, so the TOCTOU window is host-test-only).
- Old `get_absolute_path` behavior lives in the shim untouched, for legacy
  handlers that call it.

## Protocol support

Commands: ABOR ACCT ALLO APPE CDUP CWD DELE EPSV EPRT FEAT HELP LIST MDTM
MKD MODE NLST NOOP OPTS PASS PASV PORT PWD REST RETR RMD RNFR RNTO SIZE
SITE (CHMOD, UTIME) STAT STOR STRU SYST TYPE USER XCUP XCWD XMKD XPWD XRMD
+ MLSD/MLST/MFMT/STOU (RFC 3659)
+ CPFR/CPTO (server-side copy, worker job)
+ AUTH/PBSZ/PROT (RFC 4217)
+ STOP (openps3ftp custom, remote shutdown).

**STOP is opt-in and off by default**: it is registered only when the
application calls `opftp_server_set_allow_stop(s, true)`. Otherwise any
logged-in client could shut the service down. (Legacy consumers are
unaffected — the old `feat/site` registers its own STOP through the shim,
and legacy registrations shadow core defaults.)

- Data channel: PASV/PORT (IPv4), EPSV/EPRT (IPv6, RFC 2428), dual-stack
  listener (AF_INET6 with V4MAPPED, or dual sockets on ps3 if V4MAPPED is
  unavailable — probed at build time).
- TYPE I / A; STRU F; MODE S; REST + APPE/RETR; SIZE/MDTM on files.
- CPFR/CPTO: server-side copy (e.g. USB -> HDD on PS3). CPFR validates the
  source (350/550) and accepts a **file or a directory tree** — a PS3 game is
  a directory, which is what issue #9 asked for. CPTO dispatches an
  OPFTP_JOB_COPY worker job so large copies don't block the reactor;
  250/550/451 arrive with the completion. Rules:
  - **Source and destination must differ** (`550`). The destination is opened
    `O_TRUNC`, so copying a file onto itself would erase it before reading a
    byte. Checked on the reactor before any fd opens, and again in the worker.
  - **Destination must not be inside the source tree** (`550`) — a recursive
    copy would descend into what it is writing.
  - Tree walk is iterative (a ps3 worker stack is 128KB) and depth-limited to
    32. Existing destination directories are merged into, not an error.
  - A failed single-file copy unlinks a destination it created; a destination
    that already existed is left alone. A **partial tree is not rolled back** —
    recursively deleting a client-named path on error is the worse failure
    mode. The client removes it.
  - Progress: `total` is the file size for a single file, and 0 (unknown) for a
    tree — sizing a tree up front means walking it twice.
- UTF-8: control channel 8-bit clean; FEAT advertises UTF8; OPTS UTF8 ON.
- LIST/NLST: `-rwxr-xr-x 1 uid gid size Mon DD HH:MM name` — mirrors the old
  format (uid/gid from stat, "1 1" when backend reports 0).
- Reply texts: keep the existing FTP_* strings from include/const.h verbatim
  (clients and tests depend on the established wording).

## TLS (explicit FTPS, RFC 4217) — P0 contract

- **Library: mbedtls 2.28.10 (LTS, TLS 1.2 max), vendored** at
  `third_party/mbedtls` with the ps3dev/PS3Libraries patch applied
  (`scripts/018-mbedTLS-2.28.10.sh`). Built by our CMake for BOTH host and ps3
  from the same source with one pinned `MBEDTLS_CONFIG_FILE` — host tests
  exercise the exact code that ships on PS3. TLS 1.0/1.1 disabled.
- **Entropy:** platform shim — ps3: `sysGetRandomNumber` (already wired by the
  port patch); host: `getrandom()`. One entropy context per server (mutex-
  guarded); **one DRBG per TLS session** (control session DRBG + data session
  DRBG), each seeded at session start from the server entropy context — no
  shared DRBG state, no locks on the data path.
- **Allocator:** default calloc/free (newlib malloc is thread-safe on ppu).
- **BIO:** custom `f_send`/`f_recv` over our sockets (never `mbedtls_net_*`).
  Mapping: `EAGAIN` → `MBEDTLS_ERR_SSL_WANT_READ/WANT_WRITE`; EOF → 0;
  `ECONNRESET` → `MBEDTLS_ERR_NET_CONN_RESET`; other → negative. Sockets are
  non-blocking; poll drives retries (see concurrency model).
- **AUTH byte transition:** the reactor's control read buffer may already hold
  bytes past the AUTH TLS CRLF. Those bytes feed the TLS handshake first
  (leftover-buffer-first read path) — nothing is discarded.
- **Handshake deadlines:** 10s control (reactor) / 10s data (worker); client
  disconnect mid-handshake → clean teardown, no reply (or 421 if timed out).
- **Certificates:** the server copies the PEM strings (ownership transferred,
  freed on destroy). No cert generation in v1 — the app supplies a self-signed
  cert (FileZilla/FlashFXP accept with a one-time prompt).
- **Policy:** `require_tls` knob → plain connections rejected with 534.
  `AUTH TLS` only; no TLS-PSK, no anonymous.
- **Build:** `OPFTP_TLS` off → AUTH/PBSZ/PROT return 502; library still builds.
- Legacy shim: control-channel TLS is transparent (shim replies flow through
  the core). Legacy `data_callback` transfers are **plaintext-only** — the old
  callback drives the raw socket itself and knows nothing about TLS. When
  `PROT P` is active `client_data_start` refuses (the legacy callers then send
  their own 425), rather than sending the transfer in the clear to a client
  expecting encryption.

## Internal module layout (src/)

```
src/opftp.h             internal shared declarations + shared reply texts
src/server.c            lifecycle, reactor loop, accept, stop/drain, snapshot
src/pollset.c           poll wrapper: fds + wake pipe (stable slots)
src/client.c            per-connection state, control I/O, refcount,
                        worker→reactor reply queue
src/tls.h tls.c         TLS sessions over our own BIO (never mbedtls_net_*)
src/dispatch.c          command registry: open-addressed map name → handler
src/commands.c          built-in handlers (see "Protocol support")
src/datachan.c          PASV/PORT/EPSV/EPRT setup, job queue, worker pool,
                        worker-side connect, completion replies
src/transfer.c          worker-side transfer loops (plain + TLS) + server copy
src/resolve.c           canonical path resolution + root policy
src/listing.c           LIST/NLST/MLSD formatting
src/utf8.c              UTF-8 validation (byte-preserving)
src/fs_posix.c          host backend (realpath containment)
src/fs_ps3.c            ps3 sysfs backend (only built for ps3dk)
src/fs_rooted.c         per-server root wrapper around any backend
src/fs_mem.c            in-memory backend for unit tests
src/thread.c            mutex/cond/thread/thread-identity portability layer
app/                    PS3 app: headless server + NoRSX on-screen display
third_party/mbedtls/    vendored mbedtls 2.28.10 + patch (OPFTP_TLS)
```

## Security

- **PORT/EPRT:** the data connection must connect from the control channel
  peer's address. Foreign hosts rejected (425); `allow_foreign_port` knob
  restores the old permissive behavior (default off).
- **PASV/EPSV:** advertises the control connection's local address; the
  incoming data connection must originate from the control peer (vsftpd-style;
  knob to relax).
- **Queue saturation** → 425 (client stays connected); **transfer limits** →
  one per client (450).
- **Path traversal:** core resolver + rooted backends (see above).

## Legacy compatibility shim (legacy/)

Purpose: old consumers (multiMAN/webMAN/IRISMAN + old feat/base code + old
main.cpp) link against the new library unchanged.

Layout: the OLD headers stayed in `include/` (adapted only for the ps3dk
header-collision fixes) — legacy consumers already compile with `-Iinclude`,
so moving them would have broken the very builds the shim exists to preserve.
The new public API sits beside them under `include/openps3ftp/`.
`legacy/src/` implements the old headers on the new core:

| Old API | New core |
|---|---|
| `server_init/run/stop/free` | create/start; `run` = `opftp_server_run_loop` (blocking, old semantics); stop = stop+drain |
| `command_register/call` | core dispatch via adapter; **legacy registrations take precedence over core defaults** |
| `command_register_connect/disconnect` | opftp hooks fanning out to the legacy arrays |
| `client_send_code/message/...` | opftp_client_send_reply/send (reactor-thread safe by construction) |
| `client_get/set_cvar` | per-client KV map stored in the core userdata slot |
| `client_data_start/end`, `client_pasv_enter` | worker job + data_callback compat executor (below) |
| `client_socket_event/disconnect` | data-channel readiness registration / disconnect request |
| `ftpio_*` | fs backend vtable; shim ships its own linux/ps3 backends using the old io.c logic |
| `ftpstat/ftpdirent` macros | converted at the vtable boundary |
| `ThreadPool`, `sys_thread_*`, AVL, PTTree | kept as-is (self-contained, already portable) |
| `get_absolute_path` + util helpers | kept as-is (legacy handlers depend on old semantics) |

- **Facades:** legacy `struct Server/Client/Command` are side tables mirroring
  the real objects (fields like `port`, `socket`, `running` populated on demand).
- **data_callback executor:** a ThreadPool job that populates the legacy
  `Client` fields (`socket_data`, `cb_data`, `socket_pasv`) and runs the legacy
  callback, which drives its own poll/read/write on the raw data socket
  (plaintext only). Cancellation: ABOR shuts down the data socket, the
  callback's poll wakes with POLLERR/HUP/NVAL, the callback exits, the executor
  posts completion. Executor owns close/free of the data socket and job.
  - Replies the legacy callback emits go through `client_send_code`, which
    lands on the core's off-reactor reply queue (see concurrency rules) — the
    executor never touches the control socket itself.
  - **Disconnect ordering matters.** `shim_on_disconnect` runs on the reactor
    and ends up freeing the legacy `Client`, its mutex, and (via the legacy
    disconnect callbacks) the client's cvars — all of which the executor may
    still be using. So it cancels the executor and waits for it *before*
    calling `command_call_disconnect`. The handshake is `shim_client->
    job_active`, an atomic the executor clears as its very last action, after
    its final use of `client`; it is deliberately not read under
    `client->mutex`, since that mutex is about to be destroyed.
  - The shim's client table reuses freed slots. A high-water mark would have
    capped the server at `SHIM_MAX_CLIENTS` connections for the lifetime of the
    process rather than that many concurrent ones.
- **Symbol surface:** the shim defines every function the old `include/*.h`
  declare — enforced by the `symbols` ctest (`tests/check_symbols.py`), which
  parses the legacy headers and diffs them against `nm -g --defined-only` on
  the archive. A compile-only smoke test cannot catch a dropped symbol, since
  it only references the few it happens to call. `usleep`/`closesocket` are
  exempt: `common.h` forward-declares them, but the platform supplies them.
  One addition was needed: `sys_thread_cond_timedwait` in
  `include/sys.thread.h`, which the legacy ThreadPool calls.
  Archive names preserved: `libopenps3ftp_psl1ght.a` (legacy consumers link
  `-lopenps3ftp_psl1ght`); new API additionally ships as `libopenps3ftp.a`
  (same objects, both archives emitted by the build).
- **Precedence:** if a legacy app registers handlers via the shim, those shadow
  core commands; core commands are the defaults. Documented, deterministic.

## Build

CMake (new): host build + `ctest`; ps3dk cross build via toolchain file
(`-DCMAKE_TOOLCHAIN_FILE=cmake/ps3dk.cmake`, defines `OPFTP_PS3`, links
`-lmbedtls -lmbedx509 -lmbedcrypto` from PORTLIBS when TLS on). Outputs:
`libopenps3ftp.a` + `libopenps3ftp_psl1ght.a` (compat alias). The PS3 app
(`app/`, new API) builds against the installed lib via `app/Makefile`
(`make -C app EBOOT.BIN`); the legacy-consumer smoke test is built by CMake.
The old Makefiles (`lib/`, `bin/`, `external/`) were removed in P6 —
the shim absorbed the old library implementation and the new app replaces
the old binaries.

PS3 feature probes (compile-time, in `src/opftp.h`): C11 atomics availability,
poll semantics, 64-bit off_t, V4MAPPED — each probed and recorded in build
output.

## Verification

- **Host (P1):** ctest; integration via python3 ftplib (plain, IPv6 from P2,
  TLS from P3); unit tests for resolve/listing/parse/utf8 (assert-based, no
  framework). **P1 includes the concurrency tests:** OOB and in-band ABOR,
  queue saturation → 425, one-transfer-per-client → 450, disconnect mid-
  transfer, stop/drain races, PORT bounce (foreign source rejected), REST/
  APPE, concurrent transfers, multi-client. ASan/UBSan on all host tests plus
  a **TSan variant** of the concurrency tests (thread sanitizer is the only
  tool that catches the ownership races the design guards against).
- **ps3dk (P4):** clean cross-build of lib + app ELF; ftp.elf/EBOOT.BIN;
  runtime not verifiable (no console) — flagged to user.
- **Shim (P5):** the old `feat/` sources (and the legacy psl1ght main, since
  removed in P6) compile against the shim headers; legacy smoke test app
  (old API) runs on host; `symbols` ctest for the symbol surface; full ps3dk
  build still clean. Caveat on "unchanged": `feat/base/base.c` carries one
  edit, `sizeof(struct sockaddr_in*)` → `sizeof(struct sockaddr_in)` — a real
  under-allocation bug in the old code, not a shim accommodation.
- **Legacy concurrency (P5):** the legacy suite drives cases ftplib alone
  never produces — control-channel commands *during* a data transfer (two
  threads, one control socket), disconnect mid-transfer (executor still live
  while the reactor tears the client down; ASan is the gate), and more
  sequential connections than the shim's client table has slots.

## Phases and gates

- P0 design — gate: oracle review sign-off (this revision).
- P1 core, host: reactor, pollset, dispatch, commands, datachan, transfer,
  resolve, listing, fs_posix + mem backend + tests. Gate: ctest green incl.
  sanitizers.
- P2 IPv6 (EPSV/EPRT, dual-stack) + UTF-8 + OPTS + raw-socket tests. Gate:
  ctest incl. IPv6 loopback + abort/bounce tests.
- P3 TLS: vendored mbedtls, BIO shim, AUTH/PBSZ/PROT, require_tls. Gate:
  FTP_TLS integration + TLS raw-socket tests green.
- P4 ps3: fs_ps3, thread port, toolchain file, feature probes, EBOOT app.
  Gate: clean cross-build; ELF produced.
- P5 shim: legacy headers/impl, adapters, executor; old feat/main compile
  unchanged; legacy smoke test; symbol-surface check. Gate: all green + ps3dk
  build still clean.
- P6 cleanup: old lib/, bin/, external/ deleted (code absorbed by shim or new
  app); docs updated (README.md, this file). Gate: fresh host + ps3 builds and
  full ctest still green after the removal.

## Post-P6 changes

Kept here so this file stays the contract rather than a snapshot of P0:

- **Status snapshot + PS3 OSD.** `opftp_server_snapshot` publishes connected
  clients and live transfer progress; `app/` renders it over NoRSX. The
  reactor refreshes the cache at ~10Hz while a transfer is active and 1Hz
  idle, and clears `started` on loop exit.
- **RFC 3659 commands** MLSD/MLST/MFMT plus STOU and SITE UTIME.
- **Throughput work:** LIST batches lines into one send instead of one send
  per entry; STOR receives with `MSG_WAITALL` so each disk write is a full
  chunk; Nagle is off on data sockets; TLS session tickets are enabled.
- **Worker-side data connect** (see concurrency rules).
- `ACCT`/`ALLO` answer `202` rather than `502`: some clients send them
  unprompted, and a hard error reads as a failure.
