#pragma once

/*
// If compiling with the official SDK..?
#define _PS3SDK_

// Comment below to disable use of libsysfs functions when compiling.
#define _USE_SYSFS_

// Enables a workaround on PSL1GHT's somewhat poor performing poll function.
#define _USE_FASTPOLL_

// Requires sysfs - might improve performance?
#ifdef _USE_SYSFS_
#define _USE_IOBUFFERS_
#endif
*/

#if defined(_USE_SYSFS_) && !defined(_PS3SDK_)
#define sysLv2FsStat					sysFsStat
#define sysLv2FsCloseDir				sysFsClosedir
#define sysLv2FsClose					sysFsClose
#define sysLv2FsReadDir					sysFsReaddir
#define sysLv2FsWrite					sysFsWrite
#define sysLv2FsRead					sysFsRead
#define sysLv2FsRmdir					sysFsRmdir
#define sysLv2FsMkdir					sysFsMkdir
#define sysLv2FsOpenDir					sysFsOpendir
#define sysLv2FsOpen(a,b,c,d,e,f)		sysFsOpen(a,b,c,e,f)
#define sysLv2FsLSeek64(a,b,c,d)		sysFsLseek(a,(s32)b,c,d)
#define sysLv2FsFStat					sysFsFstat
#define sysLv2FsUnlink					sysFsUnlink
#define sysLv2FsChmod					sysFsChmod
#endif

#if defined(_USE_FASTPOLL_) && !defined(_PS3SDK_)
#define poll(a,b,c)						fast_poll(a,b,c)

#define FD(socket)						((socket)&~SOCKET_FD_MASK)
#define OFD(socket)						(socket|SOCKET_FD_MASK)
#else
#define FD(socket)						(socket)
#define OFD(socket)						(socket)
#endif

#ifdef _PS3SDK_
#include <netex/net.h>

int _sys_net_initialize_network(void)
{
	return sys_net_initialize_network();
}

#define netInitialize					_sys_net_initialize_network
#define netDeinitialize					sys_net_finalize_network
#define poll(a,b,c)						socketpoll(a,b,c)

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;

#define close							socketclose

#define sysFSAio						CellFsAio
#define sysFSStat						CellFsStat
#define sysFSDirent						CellFsDirent

#define sysLv2FsStat					cellFsStat
#define sysLv2FsCloseDir				cellFsClosedir
#define sysLv2FsClose					cellFsClose
#define sysLv2FsReadDir					cellFsReaddir
#define sysLv2FsWrite					cellFsWrite
#define sysLv2FsRead					cellFsRead
#define sysLv2FsRmdir					cellFsRmdir
#define sysLv2FsMkdir					cellFsMkdir
#define sysLv2FsOpenDir					cellFsOpendir
#define sysLv2FsOpen(a,b,c,d,e,f)		cellFsOpen(a,b,c,e,f)
#define sysLv2FsLSeek64					cellFsLseek
#define sysLv2FsFStat					cellFsFstat
#define sysLv2FsUnlink					cellFsUnlink
#define sysLv2FsChmod					cellFsChmod
#define sysLv2FsRename					cellFsRename
#define sysFsAioWrite					cellFsAioWrite

#define sysThreadExit					sys_ppu_thread_exit

#define SYS_O_WRONLY					CELL_FS_O_WRONLY
#define SYS_O_RDONLY					CELL_FS_O_RDONLY
#define SYS_O_CREAT						CELL_FS_O_CREAT
#define SYS_O_TRUNC						CELL_FS_O_TRUNC
#define SYS_O_APPEND					CELL_FS_O_APPEND

#define S_ISDIR(m)						(((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m)						(((m) & S_IFMT) == S_IFLNK)

#else
#define CELL_FS_SUCCEEDED	0
#define CELL_FS_EBUSY		(-31)
#endif
