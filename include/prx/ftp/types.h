#pragma once

struct Client;
struct Server;
struct Command;

typedef bool (*data_callback)(struct Client*);
typedef void (*command_callback)(struct Client*, const char[32], const char*);
typedef void (*connect_callback)(struct Client*);
typedef void (*disconnect_callback)(struct Client*);

#ifdef CELL_SDK
#define ftpstat		struct CellFsStat
#define ftpdirent	struct CellFsDirent
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
