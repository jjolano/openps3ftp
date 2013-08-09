#ifndef OPF_DEFINES_H
#define OPF_DEFINES_H

#include <net/poll.h>

// Application information
#define APP_NAME		"OpenPS3FTP"
#define APP_AUTHOR		"jjolano"
#define APP_VERSION		"3.0"

// dev_blind mount point
#define DB_MOUNTPOINT	"/dev_blind"

// Message boxes
#define ERR_NOCONN		APP_NAME " requires a network connection. Please make sure that your system is connected to a network before starting " APP_NAME "."
#define DB_MOUNT_Q		"Do you want to mount dev_blind?\nDisclaimer: I cannot be held liable if you mess up and brick your system."
#define DB_UNMOUNT_Q	"Do you want to unmount dev_blind?"
#define DB_MOUNT_S		"dev_blind mounted at " DB_MOUNTPOINT "."
#define DB_UNMOUNT_S	"dev_blind was successfully unmounted."

#define CREDITS			APP_NAME " version " APP_VERSION " by " APP_AUTHOR " (Twitter: @" APP_AUTHOR ")\nAcknowledgements:\n- atreyu187 (tester)\n- @ooPo\n- @NeoSabin\n- @GregoryRasputin\n- deroad (@Wargio)\n\nOf course, thank *you* for using " APP_NAME "."

#define CHANGES			"Changes in " APP_NAME " version " APP_VERSION ":\n- Remote Play flag added for PSVita\n- Server now a single-threaded model instead of multi-threaded\n- Login detail requirement removed\n- Stability and performance greatly improved\n- dev_blind mounter integrated\n- Improved compatibility with all FTP clients\n\nSee ChangeLog.txt for complete changes and history."

// Server types
#define DATA_TYPE_LIST	0x0001
#define DATA_TYPE_MLSD	0x0002
#define DATA_TYPE_NLST	0x0004
#define DATA_TYPE_STOR	0x0008
#define DATA_TYPE_RETR	0x0010

#define FTP_DATA_EVENT_SEND	(POLLOUT | POLLWRNORM)
#define FTP_DATA_EVENT_RECV	(POLLIN | POLLRDNORM)

// Server defines
#define CMDBUFFER		1024
#define LISTEN_BACKLOG	10
#define DATA_BUFFER		32768	// you can tune this value, see what works best

#endif /* OPF_DEFINES_H */

