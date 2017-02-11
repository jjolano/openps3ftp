/* command.cpp: FTP commands. */

#include "command.hpp"

namespace FTP
{
	void Command::import(FTP::Command* ext_command)
	{
		command_map.insert(ext_command->command_map.begin(), ext_command->command_map.end());
		connect_map.insert(connect_map.end(), ext_command->connect_map.begin(), ext_command->connect_map.end());
		disconnect_map.insert(disconnect_map.end(), ext_command->disconnect_map.begin(), ext_command->disconnect_map.end());
	}

	void Command::register_command(std::string name, command_callback callback)
	{
		command_map.insert(std::make_pair(name, callback));
	}

	void Command::register_connect_callback(connect_callback callback)
	{
		connect_map.push_back(callback);
	}

	void Command::register_disconnect_callback(disconnect_callback callback)
	{
		disconnect_map.push_back(callback);
	}

	bool Command::call_command(std::pair<std::string, std::string> command, FTP::Client* client)
	{
		using namespace std;

		map<string, command_callback>::iterator command_map_it;
		command_map_it = command_map.find(command.first);

		if(command_map_it != command_map.end())
		{
			(command_map_it->second)(client, command.first, command.second);
			return true;
		}

		return false;
	}

	void Command::call_connect(FTP::Client* client)
	{
		using namespace std;

		vector<connect_callback>::iterator connect_map_it;
		
		for(connect_map_it = connect_map.begin(); connect_map_it != connect_map.end(); ++connect_map_it)
		{
			(*connect_map_it)(client);
		}
	}

	void Command::call_disconnect(FTP::Client* client)
	{
		using namespace std;

		vector<disconnect_callback>::iterator disconnect_map_it;
		
		for(disconnect_map_it = disconnect_map.begin(); disconnect_map_it != disconnect_map.end(); ++disconnect_map_it)
		{
			(*disconnect_map_it)(client);
		}
	}
};
