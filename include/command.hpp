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
			std::vector<connect_callback> connect_map;
			std::vector<disconnect_callback> disconnect_map;
		
		public:
			void import(FTP::Command* ext_command);

			void register_command(std::string name, command_callback callback);
			void register_connect_callback(connect_callback callback);
			void register_disconnect_callback(disconnect_callback callback);

			bool call_command(std::pair<std::string, std::string> command, FTP::Client* client);
			void call_connect(FTP::Client* client);
			void call_disconnect(FTP::Client* client);
	};
};
