#pragma once

#define APP_VERSION		"4.2-prx"
#define WELCOME_MSG		"ftp server v" APP_VERSION " by jjolano"

#define FTP_502			"Command not implemented."

#define BUFFER_CONTROL	1 * 1024
#define BUFFER_DATA		128 * 1024
#define BUFFER_COMMAND	1 * 1024

#define MAX_PATH		CELL_FS_MAX_MP_LENGTH + CELL_FS_MAX_FS_PATH_LENGTH + 1
