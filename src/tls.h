/*
 * TLS support (explicit FTPS, RFC 4217) via vendored mbedtls.
 *
 * The server never uses mbedtls_net_*: sockets are driven by the
 * reactor/workers, and mbedtls talks through custom BIO callbacks
 * (f_send/f_recv over the fd), so poll semantics are preserved.
 *
 * Compiled always; without OPFTP_TLS every function is a stub.
 */
#ifndef OPFTP_TLS_H
#define OPFTP_TLS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

struct opftp_server;

/* Per-server TLS context: entropy, DRBG, cert, key, ssl config. */
struct opftp_tls_ctx;

/* Per-connection/per-transfer TLS session over one fd. */
struct opftp_tls_session;

/* ---- server context ---- */

/* Parse cert/key PEM (both required) and prepare the server config.
 * Returns 0 or a negative errno-style code. */
int opftp_tls_ctx_init(struct opftp_tls_ctx** out,
                       const char* cert_pem, const char* key_pem);
void opftp_tls_ctx_free(struct opftp_tls_ctx* ctx);

/* ---- session ---- */

/* Create a session over `fd`. `pending`/`pending_len` carry bytes that
 * were already read from the socket before the TLS handshake started
 * (the AUTH TLS line's read-ahead) — they are fed to mbedtls first.
 * Deadline is wall-clock ms for the handshake (0 = none). */
int opftp_tls_session_create(struct opftp_tls_ctx* ctx, int fd,
                             const char* pending, size_t pending_len,
                             int64_t deadline_ms,
                             struct opftp_tls_session** out);
void opftp_tls_session_free(struct opftp_tls_session* t);

/* Two "which way do I have to wait?" encodings, because the handshake
 * functions return a small positive step code while read/write return
 * a byte count. Same meaning, different sign space — name both rather
 * than sprinkling -2/-3/1/2 across the callers. */
#define OPFTP_TLS_WANT_READ     (-2)   /* opftp_tls_read/write */
#define OPFTP_TLS_WANT_WRITE    (-3)
#define OPFTP_TLS_HS_DONE         0    /* handshake / close_notify */
#define OPFTP_TLS_HS_WANT_READ    1
#define OPFTP_TLS_HS_WANT_WRITE   2

/* Advance the handshake. OPFTP_TLS_HS_DONE, OPFTP_TLS_HS_WANT_READ,
 * OPFTP_TLS_HS_WANT_WRITE, or <0 on error. Caller polls accordingly. */
int opftp_tls_handshake(struct opftp_tls_session* t);

/* Read/write application data. >0 bytes, 0 = clean EOF (read),
 * OPFTP_TLS_WANT_READ / OPFTP_TLS_WANT_WRITE, -1 = error.
 * The caller retries on poll readiness. */
ssize_t opftp_tls_read(struct opftp_tls_session* t, void* buf, size_t len);
ssize_t opftp_tls_write(struct opftp_tls_session* t, const void* buf, size_t len);

/* Send the TLS close_notify (polite shutdown before closing the fd).
 * OPFTP_TLS_HS_* as above, -1 = error. */
int opftp_tls_close_notify(struct opftp_tls_session* t);

#endif /* OPFTP_TLS_H */
