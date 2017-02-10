/* app.cpp: App-specific FTP commands. */

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"

namespace feat
{
	namespace app
	{
		namespace cmd
		{
			void exit(FTP::Client* client, std::string cmd, std::string params)
			{
				client->send_code(421, "Shutting down...");
				client->server->stop();
			}
		};

		FTP::Command get_commands(void)
		{
			FTP::Command command;

			command.register_command("APP_EXIT", feat::app::cmd::exit);

			return command;
		}
	};
};
