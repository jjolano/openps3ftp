/* site.cpp: SITE-specific commands. Depends on base. */

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"
#include "site.hpp"

namespace site
{
	namespace cmd
	{
		void exit(FTP::Client* client, std::string cmd, std::string params)
		{
			client->send_code(421, "Shutting down...");
			client->server->stop();
		}

		void chmod(FTP::Client* client, std::string cmd, std::string params)
		{
			std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
			bool* auth = (bool*) client->get_cvar("auth");

			if(!*auth)
			{
				client->send_code(530, "Not logged in");
				return;
			}

			if(params.empty())
			{
				client->send_code(501, "Bad syntax.");
				return;
			}

			std::pair<std::string, std::string> chmod_data;
			chmod_data = FTP::Utilities::parse_command_string(params.c_str());

			if(chmod_data.first.empty() || chmod_data.second.empty())
			{
				client->send_code(501, "Bad syntax.");
				return;
			}

			std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), chmod_data.second);

			mode_t mode = strtoul(chmod_data.first.c_str(), 0, 8);

			if(FTP::IO::chmod(path, mode) == 0)
			{
				client->send_code(200, "Permissions changed.");
			}
			else
			{
				client->send_code(550, "Cannot set permissions.");
			}
		}
	};

	FTP::Command get_commands(void)
	{
		FTP::Command command;

		command.register_command("CHMOD", site::cmd::chmod);
		command.register_command("EXIT", site::cmd::exit);

		return command;
	}
};
