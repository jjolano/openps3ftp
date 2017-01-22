#include <net/socket.h>

#define sfd(socket)     ((socket)&~SOCKET_FD_MASK)

void server_start(void* arg);

#ifdef __cplusplus
extern "C" {
#endif
	int closesocket(int socket);
#ifdef __cplusplus
}
#endif
