/* feat.cpp: FEAT command. */

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"

namespace feat
{
	namespace cmd
	{
		void feat(FTP::Client* client, std::string cmd, std::string params)
		{
			client->send_multicode(211, "Features:");

			client->send_multimessage("REST STREAM");
			client->send_multimessage("SIZE");
			client->send_multimessage("MDTM");
			client->send_multimessage("TVFS");

			client->send_code(211, "End");
		}
	};

	FTP::Command get_commands(void)
	{
		FTP::Command command;

		command.register_command("FEAT", feat::cmd::feat);

		return command;
	}
};
