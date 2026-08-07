/*
 * TLS support (explicit FTPS, RFC 4217) via vendored mbedtls.
 * Custom BIO over our sockets; no mbedtls_net_*.
 */
#include "opftp.h"
#include "tls.h"

#ifdef OPFTP_TLS

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ticket.h>
#include <mbedtls/error.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

#define TLS_PENDING 1024

struct opftp_tls_ctx {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    mbedtls_ssl_config conf;
    mbedtls_ssl_ticket_context tickets;  /* TLS session tickets (RFC 5077) */
    void* rng_mutex;   /* guards drbg (handshakes only) */
    bool enabled;
};

struct opftp_tls_session {
    mbedtls_ssl_context ssl;
    int fd;
    int64_t deadline_ms;          /* handshake deadline, wall ms */
    char pending[TLS_PENDING];    /* read-ahead bytes before handshake */
    size_t pending_len;
};

static int64_t now_ms(void)
{
    return opftp_now_ms();
}

static int rng_wrapper(void* p, unsigned char* out, size_t len)
{
    struct opftp_tls_ctx* ctx = p;
    int rc = -1;
    opftp_mutex_lock(ctx->rng_mutex);
    rc = mbedtls_ctr_drbg_random(&ctx->drbg, out, len);
    opftp_mutex_unlock(ctx->rng_mutex);
    return rc;
}


/* ---- custom BIO ---- */

static int bio_recv(void* p, unsigned char* buf, size_t len)
{
    struct opftp_tls_session* t = p;
    if (t->pending_len > 0) {
        size_t n = len < t->pending_len ? len : t->pending_len;
        memcpy(buf, t->pending, n);
        memmove(t->pending, t->pending + n, t->pending_len - n);
        t->pending_len -= n;
        return (int) n;
    }
    ssize_t n = recv(t->fd, buf, len, 0);
    if (n > 0)
        return (int) n;
    if (n == 0)
        return 0;                       /* EOF */
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return -1;                          /* any negative = fatal */
}

static int bio_send(void* p, const unsigned char* buf, size_t len)
{
    struct opftp_tls_session* t = p;
    ssize_t n = send(t->fd, buf, len, MSG_NOSIGNAL);
    if (n > 0)
        return (int) n;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return -1;
}

/* ---- server context ---- */

int opftp_tls_ctx_init(struct opftp_tls_ctx** out,
                       const char* cert_pem, const char* key_pem)
{
    if (!out || !cert_pem || !key_pem)
        return -EINVAL;

    struct opftp_tls_ctx* ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;
    ctx->rng_mutex = opftp_mutex_create();
    if (!ctx->rng_mutex) { free(ctx); return -ENOMEM; }

    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->drbg);
    mbedtls_x509_crt_init(&ctx->cert);
    mbedtls_pk_init(&ctx->key);
    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_ssl_ticket_init(&ctx->tickets);

    int rc = mbedtls_ctr_drbg_seed(&ctx->drbg, mbedtls_entropy_func,
                                   &ctx->entropy, NULL, 0);
    if (rc != 0)
        goto fail;

    rc = mbedtls_x509_crt_parse(&ctx->cert,
                                (const unsigned char*) cert_pem,
                                strlen(cert_pem) + 1);
    if (rc != 0)
        goto fail;

    rc = mbedtls_pk_parse_key(&ctx->key,
                              (const unsigned char*) key_pem,
                              strlen(key_pem) + 1, NULL, 0);
    if (rc != 0)
        goto fail;

    rc = mbedtls_ssl_config_defaults(&ctx->conf,
                                     MBEDTLS_SSL_IS_SERVER,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0)
        goto fail;

    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&ctx->conf, rng_wrapper, ctx);

    /* TLS 1.2 only (modern clients reject 1.0/1.1 by default) */
    mbedtls_ssl_conf_min_version(&ctx->conf,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&ctx->conf,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    rc = mbedtls_ssl_conf_own_cert(&ctx->conf, &ctx->cert, &ctx->key);
    if (rc != 0)
        goto fail;

    /* RFC 5077 session tickets: a PROT P data connection per file would
     * otherwise pay a full RSA handshake each time. Stateless (ticket
     * key is generated at init); works with FileZilla and curl. */
    rc = mbedtls_ssl_ticket_setup(&ctx->tickets, rng_wrapper, ctx,
                                  MBEDTLS_CIPHER_AES_256_GCM, 86400);
    if (rc != 0)
        goto fail;
    mbedtls_ssl_conf_session_tickets_cb(&ctx->conf,
                                        mbedtls_ssl_ticket_write,
                                        mbedtls_ssl_ticket_parse,
                                        &ctx->tickets);
    mbedtls_ssl_conf_session_tickets(&ctx->conf,
                                     MBEDTLS_SSL_SESSION_TICKETS_ENABLED);

    ctx->enabled = true;
    *out = ctx;
    return 0;

fail:
    opftp_tls_ctx_free(ctx);
    return -EINVAL;
}

void opftp_tls_ctx_free(struct opftp_tls_ctx* ctx)
{
    if (!ctx)
        return;
    if (ctx->rng_mutex)
        opftp_mutex_destroy(ctx->rng_mutex);
    mbedtls_ssl_ticket_free(&ctx->tickets);
    mbedtls_ssl_config_free(&ctx->conf);
    mbedtls_pk_free(&ctx->key);
    mbedtls_x509_crt_free(&ctx->cert);
    mbedtls_ctr_drbg_free(&ctx->drbg);
    mbedtls_entropy_free(&ctx->entropy);
    free(ctx);
}

/* ---- session ---- */

int opftp_tls_session_create(struct opftp_tls_ctx* ctx, int fd,
                             const char* pending, size_t pending_len,
                             int64_t deadline_ms,
                             struct opftp_tls_session** out)
{
    if (!ctx || !ctx->enabled || !out)
        return -EINVAL;

    struct opftp_tls_session* t = calloc(1, sizeof(*t));
    if (!t)
        return -ENOMEM;
    t->fd = fd;
    t->deadline_ms = deadline_ms;
    if (pending && pending_len > 0) {
        size_t n = pending_len < sizeof(t->pending) ? pending_len
                                                    : sizeof(t->pending);
        memcpy(t->pending, pending, n);
        t->pending_len = n;
    }

    mbedtls_ssl_init(&t->ssl);
    int rc = mbedtls_ssl_setup(&t->ssl, &ctx->conf);
    if (rc != 0) {
        mbedtls_ssl_free(&t->ssl);
        free(t);
        return -EINVAL;
    }
    /* NOTE: mbedtls_ssl_set_bio signature is (p_bio, f_send, f_recv, ...) */
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);

    *out = t;
    return 0;
}

void opftp_tls_session_free(struct opftp_tls_session* t)
{
    if (!t)
        return;
    mbedtls_ssl_free(&t->ssl);
    free(t);
}

int opftp_tls_handshake(struct opftp_tls_session* t)
{
    if (!t)
        return -1;
    int rc = mbedtls_ssl_handshake(&t->ssl);
    if (rc == 0)
        return 0;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ)
        return 1;
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 2;
    if (t->deadline_ms > 0 && now_ms() > t->deadline_ms)
        return -1;   /* deadline exceeded */
    return -1;
}

ssize_t opftp_tls_read(struct opftp_tls_session* t, void* buf, size_t len)
{
    if (!t)
        return -1;
    int rc = mbedtls_ssl_read(&t->ssl, buf, len);
    if (rc > 0)
        return rc;
    if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;                       /* clean EOF */
    if (rc == MBEDTLS_ERR_SSL_WANT_READ)
        return -2;
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return -3;
    return -1;
}

ssize_t opftp_tls_write(struct opftp_tls_session* t, const void* buf, size_t len)
{
    if (!t)
        return -1;
    int rc = mbedtls_ssl_write(&t->ssl, buf, len);
    if (rc > 0)
        return rc;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ)
        return -2;
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return -3;
    return -1;
}

int opftp_tls_close_notify(struct opftp_tls_session* t)
{
    if (!t)
        return -1;
    int rc = mbedtls_ssl_close_notify(&t->ssl);
    if (rc == 0)
        return 0;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ)
        return 1;
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 2;
    return -1;
}

#else /* !OPFTP_TLS: stubs */

int opftp_tls_ctx_init(struct opftp_tls_ctx** out,
                       const char* cert_pem, const char* key_pem)
{
    (void) out; (void) cert_pem; (void) key_pem;
    return -ENOTSUP;
}

void opftp_tls_ctx_free(struct opftp_tls_ctx* ctx) { (void) ctx; }

int opftp_tls_session_create(struct opftp_tls_ctx* ctx, int fd,
                             const char* pending, size_t pending_len,
                             int64_t deadline_ms,
                             struct opftp_tls_session** out)
{
    (void) ctx; (void) fd; (void) pending; (void) pending_len;
    (void) deadline_ms; (void) out;
    return -ENOTSUP;
}

void opftp_tls_session_free(struct opftp_tls_session* t) { (void) t; }

int opftp_tls_handshake(struct opftp_tls_session* t) { (void) t; return -1; }

ssize_t opftp_tls_read(struct opftp_tls_session* t, void* buf, size_t len)
{ (void) t; (void) buf; (void) len; return -1; }

ssize_t opftp_tls_write(struct opftp_tls_session* t, const void* buf, size_t len)
{ (void) t; (void) buf; (void) len; return -1; }

int opftp_tls_close_notify(struct opftp_tls_session* t) { (void) t; return -1; }

#endif /* OPFTP_TLS */
