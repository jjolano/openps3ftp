#pragma once

#include <net/poll.h>

// Application information
#define APP_NAME		"OpenPS3FTP"
#define APP_AUTHOR		"jjolano"
#define APP_VERSION		"3.0"

// dev_blind mount point
#define DB_MOUNTPOINT	"/dev_blind"

// Message boxes
#define ERR_NOCONN		APP_NAME " requires a network connection in order to function."

#define DB_MOUNT_Q		"Do you want to mount dev_blind?"
#define DB_UNMOUNT_Q	"Do you want to unmount dev_blind?"
#define DB_SUCCESS		"Operation was successful."

#define CREDITS			APP_NAME " v" APP_VERSION " by " APP_AUTHOR "\nAcknowledgements:\n- atreyu187 (tester)\n- coldlm (tester)\n- @MastaChaOS (tester)\n- @ooPo\n- @NeoSabin\n- @GregoryRasputin\n- deroad (@Wargio)\n\nAnd of course, thank *you* for using " APP_NAME "."

// Server types
#define DATA_EVENT_RECV	(POLLIN | POLLRDNORM)
#define DATA_EVENT_SEND	(POLLOUT | POLLWRNORM)

// Server defines
#define LISTEN_BACKLOG	10
#define CMD_BUFFER		4096
#define DATA_BUFFER		65536

// Possible disk IO performance boost using sysfs
// I've had trouble with sysfs since v2.2, so someone can recompile with/without
// sysfs by changing the option below.

#define _USE_SYSFS_

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
#define sysLv2FsLSeek64(a,b,c,d)		sysFsLseek(a,(s64)b,c,d)
#define sysLv2FsFStat					sysFsFstat
#define sysLv2FsUnlink					sysFsUnlink
#define sysLv2FsChmod					sysFsChmod
#endif
