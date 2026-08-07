/*
 * Per-connection client state: refcounted, reactor-owned control I/O.
 */
#include "opftp.h"
#include "tls.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#ifdef OPFTP_PS3
#include <net/poll.h>
#else
#include <poll.h>
#endif



struct opftp_client* opftp_client_new(struct opftp_server* s, int fd,
                                      const struct sockaddr* peer, socklen_t peerlen)
{
    struct opftp_client* c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->refs = 1;
    c->generation = 1;
    c->fd = fd;
    c->pasv_fd = -1;   /* 0 is a valid fd; "no PASV listener" must be -1 */
    c->server = s;
    strcpy(c->cwd, "/");
    if (peer && peerlen > 0 && peerlen <= sizeof(c->peer))
        memcpy(&c->peer, peer, peerlen);
    c->peerlen = (uint16_t) peerlen;
    return c;
}

void opftp_client_retain(struct opftp_client* c)
{
    if (c) c->refs++;
}

void opftp_client_release(struct opftp_client* c)
{
    if (!c) return;
    if (--c->refs == 0) {
        opftp_close_fd(c->fd);
        free(c);
    }
}

/* ---- control I/O (reactor thread only) ---- */

ssize_t opftp_client_read(struct opftp_client* c)
{
    /* make room: shift leftover to front */
    if (c->rpos > 0) {
        memmove(c->rbuf, c->rbuf + c->rpos, c->rlen - c->rpos);
        c->rlen -= c->rpos;
        c->rpos = 0;
    }
    if (c->rlen >= sizeof(c->rbuf))
        return -1;   /* line too long: bail (connection will be closed) */

    if (c->tls && !c->tls_handshaking) {
        ssize_t n = opftp_tls_read(c->tls, c->rbuf + c->rlen,
                                   sizeof(c->rbuf) - c->rlen);
        if (n > 0) {
            c->rlen += (unsigned) n;
            return n;
        }
        if (n == 0)
            return 0;              /* TLS close_notify / EOF */
        if (n == -2 || n == -3)    /* WANT_READ / WANT_WRITE: like EAGAIN */
            return -2;
        return -1;
    }

    ssize_t n = recv(c->fd, c->rbuf + c->rlen, sizeof(c->rbuf) - c->rlen, 0);
    if (n > 0) {
        c->rlen += (unsigned) n;
        return n;
    }
    if (n == 0)
        return 0;    /* EOF */
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return -2;   /* no data yet — not an error */
    return -1;
}

bool opftp_client_getline(struct opftp_client* c, char* out, size_t outsz)
{
    /* find CRLF or LF within the buffered bytes */
    unsigned i = c->rpos;
    while (i < c->rlen) {
        if (c->rbuf[i] == '\n') {
            size_t len = i - c->rpos;
            if (c->rbuf[i - 1] == '\r')
                len--;
            if (len >= outsz)
                len = outsz - 1;      /* truncate silently (defensive) */
            memcpy(out, c->rbuf + c->rpos, len);
            out[len] = '\0';
            c->rpos = i + 1;
            return true;
        }
        i++;
    }
    return false;
}

int opftp_client_send_raw(struct opftp_client* c, const char* s)
{
    size_t len = strlen(s);
    size_t off = 0;
    while (off < len) {
        if (c->tls && !c->tls_handshaking) {
            ssize_t n = opftp_tls_write(c->tls, s + off, len - off);
            if (n > 0) { off += (size_t) n; continue; }
            if (n == -2 || n == -3) {
                /* WANT_READ/WANT_WRITE: poll and retry, bounded */
                struct pollfd p = { .fd = c->fd,
                                    .events = (n == -3) ? POLLOUT : POLLIN };
                if (poll(&p, 1, 1000) <= 0)
                    return -1;
                continue;
            }
            return -1;
        }
        ssize_t n = send(c->fd, s + off, len - off, MSG_NOSIGNAL);
        if (n > 0) { off += (size_t) n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* reactor loop: socket is non-blocking; brief retry is fine
             * for small control replies, but never spin forever */
            struct pollfd p = { .fd = c->fd, .events = POLLOUT };
            if (poll(&p, 1, 1000) <= 0)
                return -1;
            continue;
        }
        return -1;
    }
    return 0;
}

/* Start the AUTH TLS handshake on the control channel. Any bytes
 * already buffered past the AUTH line feed the handshake first.
 * The 234 reply is sent by the reactor when the handshake completes.
 * Returns 0 on success. */
int opftp_client_start_tls(struct opftp_client* c)
{
    struct opftp_server* s = c->server;
    if (!s->tls || c->tls)
        return -1;
    if (c->tls_handshaking)
        return -1;

    /* leftover read-ahead bytes (past the AUTH TLS CRLF) */
    const char* pending = c->rpos < c->rlen ? c->rbuf + c->rpos : NULL;
    size_t pending_len = c->rpos < c->rlen ? c->rlen - c->rpos : 0;
    c->rpos = c->rlen = 0;

    int64_t deadline = opftp_now_ms() + 10000;

    struct opftp_tls_session* tls = NULL;
    if (opftp_tls_session_create(s->tls, c->fd, pending, pending_len,
                                 deadline, &tls) != 0)
        return -1;
    c->tls = tls;
    c->tls_handshaking = true;

    /* advance the handshake on POLLIN and POLLOUT */
    if (s->pollset && c->poll_handle >= 0)
        opftp_pollset_mod(s->pollset, c->poll_handle,
                          POLLIN | POLLPRI | POLLOUT);
    return 0;
}

/* ---- public client accessors ---- */

const struct sockaddr* opftp_client_peer(opftp_client_t* c)
{
    return (const struct sockaddr*) &c->peer;
}

const char* opftp_client_user(opftp_client_t* c)
{
    return c->user;
}

const char* opftp_client_cwd(opftp_client_t* c)
{
    return c->cwd;
}

void* opftp_client_userdata(opftp_client_t* c)
{
    return c->userdata;
}

void opftp_client_userdata_set(opftp_client_t* c, void* p)
{
    c->userdata = p;
}

void opftp_client_send(opftp_client_t* c, const char* msg)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s\r\n", msg ? msg : "");
    opftp_client_send_raw(c, buf);
}

void opftp_client_send_reply(opftp_client_t* c, int code, const char* msg)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%d %s\r\n", code, msg ? msg : "");
    opftp_client_send_raw(c, buf);
}

void opftp_client_disconnect(opftp_client_t* c)
{
    /* Reactor thread: flag for teardown; the loop removes the client
     * after the current command/hook returns. */
    c->disconnect_requested = true;
}

int opftp_reply(struct opftp_client* c, int code, const char* msg)
{
    opftp_client_send_reply(c, code, msg);
    return 0;
}
