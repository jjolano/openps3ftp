#pragma once

#include <inttypes.h>

#define APP_VERSION		"4.2-prx"
#define WELCOME_MSG		"ftp server v" APP_VERSION " by jjolano"

#define FTP_150			"Accepted data connection."

#define FTP_200			"OK."
#define FTP_202			"Already logged in."
#define FTP_215			"UNIX Type: L8"
#define FTP_221			"Bye."
#define FTP_226			"Transfer complete."
#define FTP_226A		"Transfer aborted."
#define FTP_227			"Entering Passive Mode (%hu,%hu,%hu,%hu,%hu,%hu)"
#define FTP_230			"Successfully logged in as %s."
#define FTP_230A		"Successfully logged in."
#define FTP_250			"File operation successful."
#define FTP_257			"\"%s\""

#define FTP_331			"Username %s OK. Password required."
#define FTP_350			"Restarting at %" PRIu64 "."
#define FTP_350A		"Ready for next command."

#define FTP_425			"Cannot open data connection."
#define FTP_450			"Another data transfer is already in progress."
#define FTP_451			"Data transfer error (network)."
#define FTP_452			"Data transfer error (system)."

#define FTP_501			"Bad command syntax."
#define FTP_502			"Command not implemented."
#define FTP_503			"Bad command usage."
#define FTP_504			"Parameter not accepted."
#define FTP_530			"Not logged in."
#define FTP_550			"Cannot access specified file or directory."
#define FTP_554			"Invalid REST restart point."

#define BUFFER_CONTROL	1 * 1024
#define BUFFER_COMMAND	1 * 1024
#define BUFFER_DATA		128 * 1024

#define MAX_PATH		CELL_FS_MAX_MP_LENGTH + CELL_FS_MAX_FS_PATH_LENGTH + 1
