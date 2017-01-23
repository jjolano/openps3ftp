#include <errno.h>
#include <sysutil/msg.h>

#define APP_VERSION     "4.0"

#define MOUNT_POINT     "/dev_blind"

#define LISTEN_BACKLOG  10
#define CMD_BUFFER      4096
#define DATA_BUFFER     64 * 1024 // 64K seems to be the sweet spot

#define MSG_OK          (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON)

#define _USE_SYSFS_

#if defined _USE_SYSFS_
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
#define sysLv2FsLSeek64(a,b,c,d)		sysFsLseek(a,(s64)b,c,d)
#define sysLv2FsFStat					sysFsFstat
#define sysLv2FsUnlink					sysFsUnlink
#define sysLv2FsChmod					sysFsChmod

#define AIO_ENABLED                     false
#define CELL_FS_SUCCEEDED               0
#define CELL_FS_EBUSY                   (-31)
#endif
