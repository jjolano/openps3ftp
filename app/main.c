/*
 * OpenPS3FTP — new headless PS3 app.
 * Runs the opftp server on port 2121 rooted at "/".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openps3ftp/openps3ftp.h>
#include <sys/systime.h>    /* sysSleep; no sys/timer.h in stock PSL1GHT */

#ifdef OPFTP_PS3
/* ps3dk routes net calls through libnet module imports, which RPCS3
 * HLEs as return-0 TODO stubs (socket()→fd 0, bind/listen "succeed"
 * via stubs, poll on fd 0 → POLLNVAL → busy spin). RPCS3 implements
 * the raw lv2 syscalls (700-716) instead, so bridge every net call
 * to them. Struct layouts and poll/SO option values match RPCS3's
 * sys_net definitions; fds carry SOCKET_FD_MASK like ps3dk's own
 * libnet (socket/accept set it, everything else strips it), and
 * SYS_NET errno is translated to ps3dk errno. */
#include <errno.h>
#include <net/poll.h>
#include <ppu-lv2.h>

#define FD(x) ((x) & ~SOCKET_FD_MASK)

/* SYS_NET errno (RPCS3 sys_net.h values) → ps3dk errno constants. */
static int net_errno(int e)
{
    switch (e) {
    case 1:  return EPERM;       case 2:  return ENOENT;
    case 4:  return EINTR;       case 9:  return EBADF;
    case 11: return EDEADLK;     case 12: return ENOMEM;
    case 13: return EACCES;      case 16: return EBUSY;
    case 17: return EEXIST;      case 20: return ENOTDIR;
    case 21: return EISDIR;      case 22: return EINVAL;
    case 24: return EMFILE;      case 28: return ENOSPC;
    case 32: return EPIPE;       case 35: return EAGAIN;   /* EWOULDBLOCK */
    case 36: return EINPROGRESS; case 37: return EALREADY;
    case 38: return ENOTSOCK;    case 39: return EDESTADDRREQ;
    case 40: return EMSGSIZE;    case 41: return EPROTOTYPE;
    case 42: return ENOPROTOOPT; case 43: return EPROTONOSUPPORT;
    case 44: return ENOTSUP;     case 45: return EOPNOTSUPP;
    case 46: return EPFNOSUPPORT; case 47: return EAFNOSUPPORT;
    case 48: return EADDRINUSE;  case 49: return EADDRNOTAVAIL;
    case 50: return ENETDOWN;    case 51: return ENETUNREACH;
    case 52: return ENETRESET;   case 53: return ECONNABORTED;
    case 54: return ECONNRESET;  case 55: return ENOBUFS;
    case 56: return EISCONN;     case 57: return ENOTCONN;
    case 58: return ENOTSUP;     case 60: return ETIMEDOUT;
    case 61: return ECONNREFUSED; case 62: return ELOOP;
    case 63: return ENAMETOOLONG; case 64: return EHOSTDOWN;
    case 65: return EHOSTUNREACH; case 66: return ENOTEMPTY;
    default: return ENOTSUP;
    }
}

static int net_ret(int ret)
{
    if (ret >= 0)
        return ret;
    errno = net_errno(-ret);
    return -1;
}

int socket(int family, int type, int protocol)
{
    lv2syscall3(713, (u64)family, (u64)type, (u64)protocol);
    int ret = net_ret((s32)p1);
    return ret < 0 ? -1 : (ret | SOCKET_FD_MASK);
}

int bind(int fd, const struct sockaddr* addr, socklen_t addrlen)
{
    lv2syscall3(701, (u64)FD(fd), (u64)addr, (u64)addrlen);
    return net_ret((s32)p1);
}

int listen(int fd, int backlog)
{
    lv2syscall2(706, (u64)FD(fd), (u64)backlog);
    return net_ret((s32)p1);
}

int accept(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
    lv2syscall3(700, (u64)FD(fd), (u64)addr, (u64)addrlen);
    int ret = net_ret((s32)p1);
    return ret < 0 ? -1 : (ret | SOCKET_FD_MASK);
}

int connect(int fd, const struct sockaddr* addr, socklen_t addrlen)
{
    lv2syscall3(702, (u64)FD(fd), (u64)addr, (u64)addrlen);
    return net_ret((s32)p1);
}

int getsockname(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
    lv2syscall3(704, (u64)FD(fd), (u64)addr, (u64)addrlen);
    return net_ret((s32)p1);
}

int getpeername(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
    lv2syscall3(703, (u64)FD(fd), (u64)addr, (u64)addrlen);
    return net_ret((s32)p1);
}

int getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen)
{
    lv2syscall5(705, (u64)FD(fd), (u64)level, (u64)optname,
                (u64)optval, (u64)optlen);
    return net_ret((s32)p1);
}

int setsockopt(int fd, int level, int optname, const void* optval,
               socklen_t optlen)
{
    lv2syscall5(711, (u64)FD(fd), (u64)level, (u64)optname,
                (u64)optval, (u64)optlen);
    return net_ret((s32)p1);
}

int shutdown(int fd, int how)
{
    lv2syscall2(712, (u64)FD(fd), (u64)how);
    return net_ret((s32)p1);
}

int closesocket(int fd)
{
    lv2syscall1(714, (u64)FD(fd));
    return net_ret((s32)p1);
}

/* lv2 has no plain recv/send syscalls; recvfrom/sendto accept NULL
 * addr (RPCS3 requires addr/paddrlen both NULL, which holds here). */
ssize_t recv(int fd, void* buf, size_t len, int flags)
{
    lv2syscall6(707, (u64)FD(fd), (u64)buf, (u64)len, (u64)flags, 0, 0);
    return net_ret((s32)p1);
}

ssize_t send(int fd, const void* buf, size_t len, int flags)
{
    lv2syscall6(710, (u64)FD(fd), (u64)buf, (u64)len, (u64)flags, 0, 0);
    return net_ret((s32)p1);
}

int poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    int i;
    for (i = 0; i < nfds; i++)
        fds[i].fd = FD(fds[i].fd);
    lv2syscall3(715, (u64)fds, (u64)nfds, (u64)timeout);
    int ret = net_ret((s32)p1);
    for (i = 0; i < nfds; i++)
        fds[i].fd |= SOCKET_FD_MASK;
    return ret;
}

/* lv2 has no inet_pton/ntop syscalls and the libnet module HLE is
 * TODO; implement natively (the v4only build never sees v6). */
int inet_pton(int af, const char* src, void* dst)
{
    if (af != 2 /* AF_INET */ || !src || !dst)
        return 0;
    unsigned int a, b, c, d;
    if (sscanf(src, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255)
        return 0;
    ((unsigned char*) dst)[0] = a;
    ((unsigned char*) dst)[1] = b;
    ((unsigned char*) dst)[2] = c;
    ((unsigned char*) dst)[3] = d;
    return 1;
}

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size)
{
    if (af != 2 /* AF_INET */ || !src || !dst || size < 16)
        return NULL;
    const unsigned char* p = src;
    snprintf(dst, size, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    return dst;
}
#endif

#ifndef OPFTP_HEADLESS
#include "osd.h"
#endif

int main(void)
{
    opftp_server_t* s = opftp_server_create(NULL);
    if (!s) {
        printf("OpenPS3FTP: create failed\n");
        return 1;
    }
    opftp_server_set_port(s, 2121);
    opftp_server_set_root(s, "/");
    opftp_server_set_workers(s, 2);
#ifdef OPFTP_HEADLESS
    /* RPCS3's lv2 sys_net only supports AF_INET (it aborts on
     * AF_INET6); the headless variant is the RPCS3 test build. */
    opftp_server_set_v4only(s, true);
#endif
#ifndef OPFTP_PS3
    /* Host build has no OSD to quit from; allow remote shutdown. */
    opftp_server_set_allow_stop(s, true);
#endif

    int rc = opftp_server_start(s);
    if (rc != 0) {
        printf("OpenPS3FTP: start failed (%d)\n", rc);
        return 1;
    }
    printf("OpenPS3FTP: listening on port 2121\n");

#ifdef OPFTP_PS3
#ifndef OPFTP_HEADLESS
    /* Run the on-screen display on the main thread while the server
     * runs on its reactor thread. Returns when the user quits (hold
     * START for 2s) or the home button exit signal arrives. */
    opftp_osd_run(s, "v4.1");
#else
    /* Headless variant (RPCS3 harness): no OSD, no RSX. The reactor
     * runs on its own thread; keep the main thread alive until the
     * console is shut down. The STOP command can stop the server. */
    for (;;)
        sysSleep(1000);
#endif
#else
    /* Host build: run until the console is shut down; the STOP command
     * can be used to stop the server from an FTP client. */
    for (;;)
        sysSleep(1000);
#endif

    opftp_server_stop(s);
    opftp_server_destroy(s);
    return 0;
}
