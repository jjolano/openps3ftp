#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef CELL_SDK
#include <cell.h>
#include <sys/poll.h>
#endif

#ifdef PSL1GHT_SDK
#include <net/net.h>
#include <net/poll.h>
#include <sys/file.h>

#define MAX_PATH 1056
#define MAX_NAME 256

int closesocket(int socket);
#define socketclose(a)		closesocket(a)
#define socketpoll(a,b,c)	poll(a,b,c)
#endif

#ifdef LINUX
#define __USE_FILE_OFFSET64

#include <poll.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

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

#ifndef _NO_VSH_EXPORTS_
#include "vsh_exports.h"
#endif
