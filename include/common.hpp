/* common.hpp: Common includes and functions for FTP. */

#pragma once

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

#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef PSL1GHT_SDK
#define PS3
/* PSL1GHT includes */
#include <net/net.h>
#include <sys/thread.h>
#include <sys/file.h>

/* PSL1GHT defines */
extern "C" int closesocket(int socket);
#define close(a)		closesocket(a)
#define poll(a,b,c)		netPoll(a,b,c)
#endif

#ifdef CELL_SDK
#define PS3
/* CELL SDK includes */
#include <cell/cell.h>
#include <sys/ppu_thread.h>
#include <sys/syscall.h>
#include <poll.h>

/* CELL SDK defines */
#define close(a)		socketclose(a)
#define poll(a,b,c)		socketpoll(a,b,c)
#endif

#ifndef PS3
/* Linux includes */
#include <poll.h>
#endif

#include "const.hpp"
#include "types.hpp"
#include "util.hpp"
#include "io.hpp"
