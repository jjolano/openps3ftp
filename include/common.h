#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef _NTFS_SUPPORT_
#include "ntfs.h"
#include "ps3ntfs.h"

#ifndef _PS3NTFS_PRX_
#undef ps3ntfs_running
#define ps3ntfs_running() true
#endif

#endif

#ifdef CELL_SDK
#include <cell.h>
#include <sys/poll.h>

#ifndef _NO_VSH_EXPORTS_
#include "vsh_exports.h"

#define toupper(a) sys_toupper(a)
#else
#include <ctype.h>
#endif

#endif

#ifdef PSL1GHT_SDK
#include <ctype.h>

#include <net/net.h>
#include <net/poll.h>
#include <sys/file.h>

#define MAX_PATH 1056
#define MAX_NAME 256

extern int closesocket(int socket);
#define socketclose(a)		closesocket(a)
#define socketpoll(a,b,c)	poll(a,b,c)

#define	TCP_NODELAY	0x01
#else
#include <netinet/tcp.h>
#endif

#ifdef LINUX
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>

#include <linux/limits.h>

#define MAX_PATH PATH_MAX
#define MAX_NAME NAME_MAX

#define socketclose(a)		close(a)
#define socketpoll(a,b,c)	poll(a,b,c)
#endif

#include "types.h"
#include "const.h"
#include "util.h"
#include "io.h"

#include "avlutils.h"
#include "pftutils.h"

#define NTFS_FD_MASK 0x40000000

#ifdef __cplusplus
}
#endif
