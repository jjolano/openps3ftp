#pragma once

#ifndef APP_VERSION
#define APP_VERSION     "4.1"
#endif

#ifndef WELCOME_MSG
#define WELCOME_MSG		"Welcome to OpenPS3FTP v" APP_VERSION "!"
#endif

#define TMP_DIR			"/dev_hdd0/tmp/ftp"
#define LISTEN_BACKLOG  10
#define CMD_BUFFER      2 * 1024
#define DATA_BUFFER     64 * 1024
#define IO_BUFFER		DATA_BUFFER * 4

#define MAX_PATH_LEN	1024
#define MAX_FNAME_LEN	256

#define AIO_ACTIVE      1
#define AIO_WAITING		2
#define AIO_READY		0
