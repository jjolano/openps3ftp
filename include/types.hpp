/* types.h: Common type definitions. */

#pragma once

#include "common.hpp"

namespace FTP
{
	class Client;
	class Server;
	class Command;
};

typedef void (*command_callback)(FTP::Client* /*client*/, std::string /*command_name*/, std::string /*command_param*/);
typedef bool (*data_callback)(FTP::Client* /*client*/);

#ifdef CELL_SDK
#define stat	CellFsStat
#define dirent	CellFsDirent
#define mode_t	CellFsMode
#endif

#ifdef PSL1GHT_SDK
#define stat	sysFSStat
#define dirent	sysFSDirent
#define mode_t	int32_t
#endif
