/* types.h: Common type definitions. */

#pragma once

#include "common.hpp"

namespace FTP
{
	class Client;
	class Server;
	class Command;
};

typedef void (*command_callback)(FTP::Client* /*client*/, std::string /*command_name*/, std::string /*command_param*/);
typedef bool (*data_callback)(FTP::Client* /*client*/, int /*socket_data*/);
typedef void (*connect_callback)(FTP::Client* /*client*/);
typedef void (*disconnect_callback)(FTP::Client* /*client*/);

#ifdef CELL_SDK
#define ftpstat		CellFsStat
#define ftpdirent	CellFsDirent
#define mode_t		CellFsMode

#define O_CREAT		CELL_FS_O_CREAT
#define O_TRUNC		CELL_FS_O_TRUNC
#define O_APPEND	CELL_FS_O_APPEND
#define O_WRONLY	CELL_FS_O_WRONLY
#define O_RDONLY	CELL_FS_O_RDONLY
#endif

#ifdef PSL1GHT_SDK
#define ftpstat		sysFSStat
#define ftpdirent	sysFSDirent
#define mode_t		int32_t

#define O_CREAT		SYS_O_CREAT
#define O_TRUNC		SYS_O_TRUNC
#define O_APPEND	SYS_O_APPEND
#define O_WRONLY	SYS_O_WRONLY
#define O_RDONLY	SYS_O_RDONLY
#endif

#ifdef LINUX
#define ftpstat		struct stat
#define ftpdirent	struct dirent
#endif
