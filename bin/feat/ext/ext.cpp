/* ext.cpp: Extended FTP commands. Depends on base. */

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"

namespace feat
{
	namespace ext
	{
		namespace cmd
		{
			void size(FTP::Client* client, std::string cmd, std::string params)
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
					client->send_code(501, "No filename specified.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				ftpstat stat;
				if(FTP::IO::stat(path, &stat) == 0)
				{
					std::stringstream size_ss;
					size_ss << stat.st_size;

					client->send_code(213, size_ss.str());
				}
				else
				{
					client->send_code(550, "Cannot access file");
				}
			}

			void mdtm(FTP::Client* client, std::string cmd, std::string params)
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
					client->send_code(501, "No filename specified.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				ftpstat stat;
				if(FTP::IO::stat(path, &stat) == 0)
				{
					char tstr[16];
					strftime(tstr, sizeof(tstr), "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

					client->send_code(213, tstr);
				}
				else
				{
					client->send_code(550, "Cannot access file");
				}
			}
		};

		FTP::Command get_commands(void)
		{
			FTP::Command command;

			command.register_command("SIZE", feat::ext::cmd::size);
			command.register_command("MDTM", feat::ext::cmd::mdtm);

			return command;
		}
	};
};
