#ifndef NETBRIDGE_H
#define NETBRIDGE_H

#include <sys/socket.h>
#include <net/poll.h>

int socket(int, int, int);
int bind(int, const struct sockaddr*, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr*, socklen_t*);
int connect(int, const struct sockaddr*, socklen_t);
int getsockname(int, struct sockaddr*, socklen_t*);
int getpeername(int, struct sockaddr*, socklen_t*);
int getsockopt(int, int, int, void*, socklen_t*);
int setsockopt(int, int, int, const void*, socklen_t);
int shutdown(int, int);
int closesocket(int);
ssize_t recv(int, void*, size_t, int);
ssize_t send(int, const void*, size_t, int);
int poll(struct pollfd[], nfds_t, int);
int inet_pton(int, const char*, void*);
const char* inet_ntop(int, const void*, char*, socklen_t);

#endif