/* const.hpp: Server constants. */

#pragma once

#define APP_VERSION		"4.2"
#define WELCOME_MSG		"ftp server v" APP_VERSION " by jjolano"

#define FTP_502			"Command not implemented."

#define BUFFER_CONTROL	1 * 1024

#ifndef PRX
#define BUFFER_DATA		128 * 1024
#else
#define BUFFER_DATA		64 * 1024
#endif
