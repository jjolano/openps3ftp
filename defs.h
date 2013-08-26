#pragma once

#include <net/poll.h>

// Application information
#define APP_NAME		"OpenPS3FTP"
#define APP_AUTHOR		"jjolano"
#define APP_VERSION		"3.0"

// dev_blind mount point
#define DB_MOUNTPOINT	"/dev_blind"

// Message boxes
#define ERR_NOCONN_Q	APP_NAME " requires a network connection in order to function.\n\nRecheck network connection?"
#define DB_MOUNT_Q		"Do you want to mount dev_blind?\nDisclaimer: You access your flash at your own risk."
#define DB_UNMOUNT_Q	"Do you want to unmount dev_blind?"
#define DB_MOUNT_S		"dev_blind mounted at " DB_MOUNTPOINT "."
#define DB_UNMOUNT_S	"dev_blind was successfully unmounted."

#define CREDITS			APP_NAME " version " APP_VERSION " by " APP_AUTHOR "\nAcknowledgements:\n- atreyu187\n- coldlm\n- @MastaChaOS\n- @ooPo\n- @NeoSabin\n- @GregoryRasputin\n- deroad (@Wargio)\n\nAnd of course, thank *you* for using " APP_NAME "."

// Server types
#define DATA_TYPE_DIR	1
#define DATA_TYPE_FILE	2

#define DATA_EVENT_SEND	(POLLOUT | POLLWRNORM)
#define DATA_EVENT_RECV	(POLLIN | POLLRDNORM)

// Server defines
#define CMDBUFFER		4096
#define LISTEN_BACKLOG	10
#define DATA_BUFFER		65536

// Possible disk IO performance boost using sysfs
// I haven't had any luck with this since v2.2, as it *still* crashes the system
// upon usage. I don't know why it's not solved in PSL1GHT.
//#define _USE_SYSFS_

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
