// Comment below to disable use of libsysfs functions when compiling.
#define _USE_SYSFS_

// Enables a workaround on PSL1GHT's somewhat poor performing poll function.
#define _USE_FASTPOLL_

// Requires sysfs - might improve performance?
#define _USE_IOBUFFERS_

#define CELL_FS_SUCCEEDED	0
#define CELL_FS_EBUSY		(-31)

#ifdef __cplusplus
extern "C" {
#endif
int closesocket(int socket);
#ifdef __cplusplus
}
#endif

#define close(a)						closesocket(a)

#ifdef _USE_SYSFS_
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

#ifdef _USE_FASTPOLL_
#define poll(a,b,c)						fast_poll(a,b,c)

#define FD(socket)						((socket)&~SOCKET_FD_MASK)
#define OFD(socket)						(socket|SOCKET_FD_MASK)
#else
#define FD(socket)						(socket)
#define OFD(socket)						(socket)
#endif
