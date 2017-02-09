/* command.cpp: FTP commands. */

#pragma once

#include "command.hpp"

namespace FTP
{
	void Command::import_commands(FTP::Command* ext_command)
	{
		command_map.insert(ext_command->command_map.begin(), ext_command->command_map.end());
	}

	void Command::register_command(std::string name, command_callback callback)
	{
		command_map.insert(std::make_pair(name, callback));
	}

	bool Command::call_command(std::pair<std::string, std::string> command, FTP::Client* client)
	{
		using namespace std;

		map<string, command_callback> command_map_it;
		command_map_it = command_map.find(command.first);

		if(command_map_it != command_map.end())
		{
			(*command.second)(client, command.first, command.second);
			return true;
		}

		return false;
	}
};
