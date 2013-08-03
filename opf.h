#ifndef OPF_DEFINES_H
#define OPF_DEFINES_H

// Application information
#define APP_NAME		"OpenPS3FTP"
#define APP_AUTHOR		"jjolano"
#define APP_VERSION		"3.0-rc3"

// dev_blind mount point
#define DB_MOUNTPOINT	"/dev_blind"

// Message boxes
#define RUN_MESSAGE		"This build of " APP_NAME " has not been tested extensively. Some features may not work properly. I (" APP_AUTHOR ") take no responsibility for any consequences that may occur to your system during the use of this software. You have been warned."

#define ERR_NOCONN		APP_NAME " requires a network connection. Please make sure that your system is connected to a network and then start " APP_NAME " again. " APP_NAME " will now exit."

#define DB_MOUNT_Q		"Do you want to mount dev_blind?"

#define DB_UNMOUNT_Q	"Do you want to unmount dev_blind?"

#define DB_MOUNT_S		"dev_blind was successfully mounted. Please note that dev_blind will not automatically unmount upon exiting " APP_NAME

#define DB_UNMOUNT_S	"dev_blind was successfully unmounted."

#endif /* OPF_DEFINES_H */

