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

#define CREDITS			APP_NAME " version " APP_VERSION " by " APP_AUTHOR "\nAcknowledgements:\n- atreyu187 (tester)\n- @ooPo\n- @NeoSabin\n- @GregoryRasputin\n- deroad (@Wargio)\n\nAnd of course, thank *you* for using " APP_NAME "."

// Server types
#define DATA_TYPE_DIR	1
#define DATA_TYPE_FILE	2

#define DATA_EVENT_SEND	(POLLOUT | POLLWRNORM)
#define DATA_EVENT_RECV	(POLLIN | POLLRDNORM)

// Server defines
#define CMDBUFFER		1024
#define LISTEN_BACKLOG	10
#define DATA_BUFFER		16384
