/* common.hpp: Common includes and functions for FTP. */

#pragma once

#define __USE_FILE_OFFSET64

#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <iterator>
#include <algorithm>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef PSL1GHT_SDK
#define PS3
/* PSL1GHT includes */
#include <net/net.h>
#include <sys/thread.h>
#include <sys/file.h>
#include <sys/memory.h>

/* PSL1GHT defines */
extern "C" int closesocket(int socket);
#define close(a)		closesocket(a)
#define poll(a,b,c)		netPoll(a,b,c)

#define PFD(a)			((a) & ~SOCKET_FD_MASK)
#define OFD(a)			(a | SOCKET_FD_MASK)

#define d_ino			1
#endif

#ifdef CELL_SDK
#define PS3
/* CELL SDK includes */
#include <cell.h>
#include <sys/ppu_thread.h>
#include <sys/syscall.h>
#include <sys/poll.h>
#include <sys/memory.h>

/* CELL SDK defines */
#define close(a)		socketclose(a)
#define poll(a,b,c)		socketpoll(a,b,c)
#define d_ino			1
#endif

#if (!defined(PS3) || defined(LINUX))
#ifndef LINUX
#define LINUX
#endif
/* Linux includes */
#include <poll.h>
#include <fcntl.h>
#include <dirent.h>

#define d_ino			dirent.d_ino
#endif

#ifndef PSL1GHT_SDK
#define PFD(a) (a)
#define OFD(a) (a)
#endif

#include "const.hpp"
#include "types.hpp"
#include "util.hpp"
#include "io.hpp"
