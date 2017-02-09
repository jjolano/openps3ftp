/* command.hpp: FTP commands. */

#pragma once

#include "common.hpp"
#include "client.hpp"

namespace FTP
{
	class Command
	{
		private:
			std::map<std::string, command_callback> command_map;
		
		public:
			void import_commands(FTP::Command* ext_command);
			void register_command(std::string name, command_callback callback);
			bool call_command(std::pair<std::string, std::string> command, FTP::Client* client);
	};
};
